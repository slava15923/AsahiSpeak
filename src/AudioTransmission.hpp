#pragma once
#include "network.hpp"
#include "LockFreeRingBuffer.hpp"
#include <thread>
#include <chrono>
#include <optional>
#include <opus.h>
#include "audio.hpp"
#include <queue>

#include "jitterbuffer.hpp"
#include <math.h>
#include <unordered_map>
#include "udpNonBlocking.hpp"

std::atomic_flag isMuted;
float noSound[FRAME_SIZE] = {0};

static inline float fastTanh(float x) noexcept {
    if (x < -3.0f) return -1.0f;
    if (x >  3.0f) return  1.0f;

    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

class User {
public:
    User(const uint32_t& clientHash_, const uint64_t& startSequence, const char* username_) : clientHash(clientHash_), username(username_), buffer(startSequence) {
        std::cout << "connect: " << username << std::endl;
        decoder = opus_decoder_create(SAMPLE_RATE, 1, &error);
        if (error != OPUS_OK) {
            std::cerr << "error create decoder: " << opus_strerror(error) << std::endl;
        }
    }
    ~User() {}
    uint32_t getClientHash() {
        return clientHash;
    }
    uint32_t getMixerHash() {
        return mixerHash;
    }
    OpusDecoder* getOpusDecoder() {
        return decoder;
    }

    void push(const uint64_t sequence_, std::shared_ptr<float []> data) {
        buffer.push(sequence_, data);
    }

    bool pop(std::shared_ptr<float []>& data) {
        return buffer.pop(data);
    }
private:
    const uint32_t clientHash;
    uint32_t mixerHash;
    std::string username;
    OpusDecoder* decoder;
    JitterBuffer<std::shared_ptr<float[]>> buffer;
    int error;

};

class AudioTransmission {
    private:
        NonBlockingUdpSocket sock;
        std::unique_ptr<networkDataAudio> receive;
        std::unique_ptr<networkDataAudio> send;

        struct sockaddr_in server_addr;

        LockFreeRingBuffer& readBuffer;
        LockFreeRingBuffer& recordBuffer;

        WOLFSSL_CTX* ctx;
        WOLFSSL* ssl;

        std::thread write;
        std::thread read;
        std::thread controlthread;

        std::atomic<bool> running = false;
        std::atomic<bool> stopFlag = false;

        OpusEncoder* encoder;
        OpusDecoder* decoder;

        std::string username;

        int numchannel;

        std::unordered_map<uint32_t, std::shared_ptr<User> > users;
        std::mutex mixerMtx;

        std::mutex sslMtx;
        

        
        

        int error = 0;
        void connect() {
            bool handshake_done = false;

            while (!handshake_done) {
            int ret = wolfSSL_connect(ssl);

            if (ret == WOLFSSL_SUCCESS) {
                std::cout << "DTLS Handshake successfully completed!" << std::endl;
                handshake_done = true;
            } 
            else {
                int err = wolfSSL_get_error(ssl, ret);

                if (err == WOLFSSL_ERROR_WANT_READ) {
                    int wait_res = sock.wait_timeout(1, 0); 
                    if (wait_res == -1) {
                        throw std::runtime_error("Socket error during handshake wait.");
                    }
                } 
                else if (err == WOLFSSL_ERROR_WANT_WRITE) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                } 
                else {
                    char errorString[80];
                    wolfSSL_ERR_error_string(err, errorString);
                    std::cerr << "critical error DTLS Handshake: " << errorString << " (code " << err << ")\n";
                    
                    throw std::runtime_error("DTLS handshake failed permanently.");
                }
            }
        }}

        int mixer() noexcept {
            auto Buf = std::make_unique<float[]>(FRAME_SIZE);
            int mixed = 0;
            std::vector<std::shared_ptr<float[]>> localBuffer;

            while (running) {
                if (!readBuffer.waitForSpace(FRAME_SIZE, &stopFlag)) {
                    if (stopFlag.load()) break;
                    continue;
                }

                float* dst = Buf.get();
                std::fill(dst, dst + FRAME_SIZE, 0.0f);
                std::shared_ptr<float[]> temp;
                

                {
                    std::lock_guard<std::mutex> lock(mixerMtx);
                    for(const auto it : users) {
                        User* userptr = it.second.get();
                        if(userptr->pop(temp)) {
                            for(int i = 0; i < FRAME_SIZE; i++) {
                                dst[i] += temp.get()[i];
                            }
                            mixed++;
                        }
                    }
                }
                

                if (mixed) {
                    float inv = 0.95f;
                    for (size_t i = 0; i < FRAME_SIZE; ++i)
                        dst[i] = fastTanh(dst[i]);
                }

                readBuffer.writeNoOverwrite(dst, FRAME_SIZE);

                mixed = 0;
            }
            return 0;
        }

        int readData() {
            std::shared_ptr<float[]> frame;
            int bytes;
            int decoded;
            uint64_t clientHash;
            //q.reserve(50);
            User* userptr;
            int err;
            
            

            while(running) {
                int wait_res = sock.wait_timeout(5, 0);

                if (wait_res == 0) {
                    std::cerr << "time out server" << std::endl;
                    std::cerr << "reconect" << std::endl;
                    continue; 
                } else if (wait_res == -1) {
                    std::cerr << "system socket error" << std::endl;
                    break;
                }

                {
                    std::lock_guard<std::mutex> lock(sslMtx);
                    bytes = wolfSSL_read(ssl, receive.get(), sizeof(networkDataAudio));
                    err = wolfSSL_get_error(ssl, bytes);
                }
                if(bytes > 0) {
                    if (bytes == (int)sizeof(networkDataAudio)) {
                        clientHash = receive.get()->clientHash;
                        if (!users.count(clientHash)) users.emplace(clientHash, std::make_shared<User>(clientHash, receive->sequence, receive->username));

                        frame = std::make_shared<float[]>(FRAME_SIZE);
                        userptr = users[clientHash].get();
                        decoded = opus_decode_float(userptr->getOpusDecoder(), receive->frames, 160, frame.get(), FRAME_SIZE, 0);
                        if (decoded == (int)FRAME_SIZE) {
                            {
                                std::lock_guard<std::mutex> lock(mixerMtx);
                                users[clientHash].get()->push(receive->sequence, std::move(frame));
                            }
                        }
                    } else if(bytes == (int)sizeof(pongPacket)) {
                        continue;
                    }
                } else {

                    if (err == WOLFSSL_ERROR_WANT_READ) {
                        continue;
                    } 
                    else if (err == WOLFSSL_ERROR_WANT_WRITE) {
                        continue;
                    } 
                    else {
                        char errorString[80];
                        wolfSSL_ERR_error_string(err, errorString);
                        fprintf(stderr, "wolfSSL_read critical error: %s (code %d)\n", errorString, err);
                        break; 
                    }
                }
                
            }
            //delete dataForReceive;
            return 0;
        }

        int writeData() {
            int err;
            int res;
            uint32_t sendSeq_ = 0;
            auto tempPCMData = std::make_unique<float[]>(FRAME_SIZE);

            send->channel = numchannel;
            strncpy(send->username, username.c_str(), sizeof(send->username) - 1);
            send->username[sizeof(send->username) - 1] = '\0';

            int n;

            while (running) {
                bool packet_sent = false;
                n = recordBuffer.readBlocking(tempPCMData.get(), FRAME_SIZE);
                if (n != (int)FRAME_SIZE) continue;

                int encoded = opus_encode_float(encoder,
                                                tempPCMData.get(), FRAME_SIZE,
                                                send->frames, 160);
                if (encoded < 0) continue;

                send->sequence = sendSeq_++;

                while (running && !packet_sent) {
                    
                    {
                        std::lock_guard<std::mutex> lock(sslMtx);
                        res = wolfSSL_write(ssl, send.get(), sizeof(networkDataAudio));
                        err = wolfSSL_get_error(ssl, res);
                    }

                    if (res == (int)sizeof(networkDataAudio)) {
                        packet_sent = true;
                    } 
                    else {

                        if (err == WOLFSSL_ERROR_WANT_WRITE) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        } 
                        else if (err == WOLFSSL_ERROR_WANT_READ) {
                            sock.wait_timeout(0, 1000); 
                        } 
                        else {
                            char errorString[256];
                            wolfSSL_ERR_error_string(err, errorString);
                            fprintf(stderr, "wolfSSL_write критическая ошибка: %s (код %d)\n", errorString, err);
                            
                            running = false;
                            break;
                        }
                    }
                }
            }
            return 0;
        }

        void controlThread() {

            ssl = wolfSSL_new(ctx);
            if (!ssl) error_handling("wolfSSL_new failed");
            wolfSSL_set_fd(ssl, sock.get_handle());

            wolfSSL_dtls_set_peer(ssl, (struct sockaddr*)&server_addr, sizeof(server_addr));
            wolfSSL_dtls_set_mtu(ssl, MTU);

            wolfSSL_set_using_nonblock(ssl, 1);

            connect();

            printf("TLS handshake successful\n");

            if (wolfSSL_dtls_cid_is_enabled(ssl) == 1) {
                printf("CID успешно согласован!\n");
            }
            running = true;
            write = std::thread(&AudioTransmission::writeData, this);
            read = std::thread(&AudioTransmission::readData, this);
            //ThreadRealTime mixer_(80, &AudioTransmission::mixer, this);
            std::thread mixer_(&AudioTransmission::mixer, this);
            write.detach();
            read.detach();
            mixer_.detach();

            while(running) {std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            //while(statusReadData || statusWriteData) {}
        }

        void setClientVolume(uint32_t clientId, float gain) {
        //    jitter.SetClientGain(clientId, gain);
        }
        float clientVolume(uint32_t clientId) const {
        //    return jitter.GetClientGain(clientId);
        }

    public:
        AudioTransmission(const char* ip, uint16_t port, 
            const char* username_, const char* password, 
            int numchannel_, LockFreeRingBuffer& recordBuffer_, 
            LockFreeRingBuffer& readBuffer_) 
            : recordBuffer(recordBuffer_), readBuffer(readBuffer_), username(username_), numchannel(numchannel_) {
            
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(port);

            if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0)
                error_handling("invalid server IP");

            receive = std::make_unique<networkDataAudio>();
            send = std::make_unique<networkDataAudio>();

            ctx = wolfSSL_CTX_new(wolfDTLSv1_3_client_method());
            if (!ctx) error_handling("wolfSSL_CTX_new failed");
            std::cout << "status load system certificates: " << wolfSSL_CTX_load_system_CA_certs(ctx) << std::endl;

            SetBrowserECCGroups(ctx);
            SetBrowserDtls13Ciphers(ctx);

            encoder = opus_encoder_create(SAMPLE_RATE, 1, OPUS_APPLICATION_AUDIO, &error);
            if (error != OPUS_OK) {
                std::cerr << "Ошибка создания энкодера: " << opus_strerror(error) << std::endl;
            }

            opus_encoder_ctl(encoder, OPUS_SET_BITRATE(64000));

            opus_encoder_ctl(encoder, OPUS_SET_VBR(0));

            opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(0));

            decoder = opus_decoder_create(SAMPLE_RATE, 1, &error);
            if (error != OPUS_OK) {
                std::cerr << "Ошибка создания декодера: " << opus_strerror(error) << std::endl;
            }



            
        }

        ~AudioTransmission() {
            wolfSSL_CTX_free(ctx);
        }
        //запускает передачу данных на udp сервер
        void startTransmission() {
            controlthread = std::thread(&AudioTransmission::controlThread, this);
        }

        void stopTransmission() {
            if (running) {
                running = false;
                //while(statusReadData || statusWriteData) {}
                wolfSSL_free(ssl);
            }
        }



        void addServerSert(const char *file) {
            if (wolfSSL_CTX_load_verify_locations(ctx, file, 0) != SSL_SUCCESS) {
                //
                throw std::runtime_error("CA certificates not loaded, trying without verification");
            }
        }
        
        void offCertVerify() {
            wolfSSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, 0);
        }

        void mute() {
            isMuted.test_and_set();
        }
        void unmute() {
            isMuted.clear();
        }
};