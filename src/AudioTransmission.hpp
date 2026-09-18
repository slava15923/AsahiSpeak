#pragma once
#include "network.hpp"
#include "LockFreeRingBuffer.hpp"
#include <thread>
#include <chrono>
#include <optional>
#include <opus.h>
#include "audio.hpp"
#include <queue>
#include <algorithm>

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

    void push(const uint64_t sequence_, std::unique_ptr<float []> data) {
        buffer.push(sequence_, std::move(data));
    }

    bool pop(std::unique_ptr<float []>& data) {
        return buffer.pop(data);
    }
private:
    const uint32_t clientHash;
    uint32_t mixerHash;
    std::string username;
    OpusDecoder* decoder;
    JitterBuffer<std::unique_ptr<float[]>> buffer;
    int error;

};

class AudioTransmission {
    private:
        
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

        std::unordered_map<uint32_t, std::unique_ptr<User> > users;
        std::mutex mixerMtx;

        std::mutex sslMtx;

        bool isReconect = false;

        NonBlockingUdpSocket sock;

        int error = 0;

        void connect() {
            using namespace std::chrono;

            auto deadline = steady_clock::now() + seconds(300);

            while (true) {
                if (stopFlag.load())
                    throw std::runtime_error("handshake aborted by stopFlag");

                if (steady_clock::now() > deadline)
                    throw std::runtime_error("handshake timeout");

                int ret = wolfSSL_connect(ssl);

                if (ret == WOLFSSL_SUCCESS) {
                    std::cout << "DTLS Handshake successfully completed!\n";
                    return;
                }

                int err = wolfSSL_get_error(ssl, ret);

                if (err == WOLFSSL_ERROR_WANT_READ || err == WOLFSSL_ERROR_WANT_WRITE) {
                    int timeout_sec = wolfSSL_dtls_get_current_timeout(ssl);
                    if (timeout_sec <= 0) timeout_sec = 1;

                    if (wolfSSL_dtls13_use_quick_timeout(ssl)) {
                        timeout_sec = std::max(1, timeout_sec / 4);
                    }

                    WaitMode mode = (err == WOLFSSL_ERROR_WANT_READ) ? WaitMode::Read : WaitMode::Write;
                    int sel = sock.wait_timeout(timeout_sec, 0, mode);

                    if (sel < 0) {
                        if (errno == EINTR) continue;
                        throw std::runtime_error("select() failed during handshake");
                    }

                    if (sel == 0) {
                        if (wolfSSL_dtls_got_timeout(ssl) != WOLFSSL_SUCCESS) {
                            throw std::runtime_error("DTLS handshake exceeded max retransmits");
                        }
                    }
                    continue;
                }

                if (err == WOLFSSL_ERROR_ZERO_RETURN) {
                    throw std::runtime_error("DTLS connection closed during handshake");
                }

                char errorString[256];
                wolfSSL_ERR_error_string_n(err, errorString, sizeof(errorString));
                std::cerr << "critical error DTLS handshake: " << errorString
                        << " (code " << err << ")\n";
                throw std::runtime_error("DTLS handshake failed permanently");
            }
        }


        int mixer() noexcept {
            std::cout << "start mixer()" << std::endl;
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
                std::unique_ptr<float[]> temp;
                

                {
                    std::lock_guard<std::mutex> lock(mixerMtx);
                    for(const auto& it : users) {
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
            std::cout << "end mixer()" << std::endl;
            return 0;
        }

        int readData() {
            std::cout << "start readData()" << std::endl;
            std::unique_ptr<float[]> frame;
            int bytes;
            int decoded;
            uint64_t clientHash;
            User* userptr;
            int err;
            
            while(running) {
                int wait_res = sock.wait_timeout(5, 0);

                if (wait_res == 0) {
                    std::cerr << "time out server" << std::endl;
                    //std::cerr << "reconect" << std::endl;
                    isReconect = true;
                    running = false;
                    break;
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
                        if (!users.count(clientHash)) users.emplace(clientHash, std::make_unique<User>(clientHash, receive->sequence, receive->username));

                        frame = std::make_unique<float[]>(FRAME_SIZE);
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
            std::cout << "end readData()" << std::endl;
            return 0;
        }

        int writeData() {
            std::cout << "start writeData()" << std::endl;
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
            std::cout << "end writeData()" << std::endl;
            return 0;
        }

        void controlThread() {
            std::cout << "start controlThread()" << std::endl;
            isReconect = true;

            while (!stopFlag) {
                if(isReconect){
                    ssl = wolfSSL_new(ctx);
                    isReconect = false;
                } else {
                    stopFlag = true;
                    break;
                }

                NonBlockingUdpSocket sock_;
                sock = std::move(sock_);

                if (ssl == nullptr) error_handling("wolfSSL_new failed");

                socket_t fd = sock.get_handle();
                
                wolfSSL_dtls_set_peer(ssl, (struct sockaddr*)&server_addr, sizeof(server_addr));
                wolfSSL_set_fd(ssl, fd);
                wolfSSL_dtls_set_mtu(ssl, MTU);

                wolfSSL_set_using_nonblock(ssl, 1);

                connect();

                running = true;
                write = std::thread(&AudioTransmission::writeData, this);
                read = std::thread(&AudioTransmission::readData, this);

                std::thread mixer_(&AudioTransmission::mixer, this);
                write.join();
                read.join();
                mixer_.join();
                wolfSSL_free(ssl);
            }
            std::cout << "end controlThread()" << std::endl;
        }

        void setClientVolume(uint32_t clientId, float gain) {
        //    jitter.SetClientGain(clientId, gain);
        }
        float clientVolume(uint32_t clientId) const {
        //    return jitter.GetClientGain(clientId);
            return 0.0f;//placeholder
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