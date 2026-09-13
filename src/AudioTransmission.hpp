#pragma once
#include "network.hpp"
#include "LockFreeRingBuffer.hpp"
#include <thread>
#include <chrono>
#include <optional>
#include <opus.h>
#include "audio.hpp"
#include <queue>
#include "Spinlock.hpp"
#include "ThreadRealTime.hpp"
#include "jitterbuffer.hpp"
#include <math.h>

class ClientIdMap {
public:
    uint32_t Get(uint32_t hash, int MAX_CLIENTS = 64) {
        auto it = map_.find(hash);
        if (it != map_.end()) return it->second;
        if (nextId_ >= MAX_CLIENTS) return UINT32_MAX;
        uint32_t id = nextId_++;
        map_[hash] = id;
        return id;
    }

    uint32_t Remove(uint32_t hash) {
        auto it = map_.find(hash);
        if (it == map_.end()) return UINT32_MAX;
        uint32_t id = it->second;
        map_.erase(it);
        return id;
    }

    // (опционально) true, если запись существует
    bool Contains(uint32_t hash) const {
        return map_.find(hash) != map_.end();
    }

    // (опционально) текущее число клиентов
    size_t Size() const { return map_.size(); }

private:
    std::unordered_map<uint32_t, uint32_t> map_;
    uint32_t nextId_ = 0;
};


static inline float fastTanh(float x) noexcept {
    // Saturate, чтобы не уходить в бесконечность
    if (x < -3.0f) return -1.0f;
    if (x >  3.0f) return  1.0f;

    const float x2 = x * x;
    // Padé(3,3) для tanh:
    //   tanh(x) ≈ x * (27 + x²) / (27 + 9·x²)
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

class User {
public:
    User(ClientIdMap& idMap_, const uint32_t& clientHash_, const char* username_) : idMap(idMap_), clientHash(clientHash_), username(username_) {
        mixerHash = idMap.Get(clientHash);
        std::cout << "подключился: " << username << std::endl;
        decoder = opus_decoder_create(SAMPLE_RATE, 1, &error);
        if (error != OPUS_OK) {
            std::cerr << "Ошибка создания декодера: " << opus_strerror(error) << std::endl;
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
private:
    ClientIdMap& idMap;
    const uint32_t clientHash;
    uint32_t mixerHash;
    std::string username;
    OpusDecoder* decoder;
    int error;

};

class AudioTransmission {
    private:
        std::unique_ptr<networkDataAudio> receive;
        std::unique_ptr<networkDataAudio> send;

        socket_t sock;
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

        std::unordered_map<uint32_t, std::shared_ptr<JitterBuffer<std::shared_ptr<float[]> > > > buffers;
        std::vector<uint64_t> indexBuffers;
        std::unordered_map<uint32_t, std::unique_ptr<User>> users;
        ClientIdMap idMap;
        std::mutex mixerMtx;

        
        

        int error = 0;

        int mixer() noexcept {
            auto Buf = std::make_unique<float[]>(FRAME_SIZE);
            int mixed = 0;

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
                    for(const auto& index : indexBuffers) {
                        if(buffers[index].get()->pop(temp)) {
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
            uint32_t clientHash;
            //q.reserve(50);
            User* userptr;
            
            

            while(running) {
                bytes = wolfSSL_read(ssl, receive.get(), sizeof(networkDataAudio));

                if (bytes == (int)sizeof(networkDataAudio)) {
                    clientHash = fnv1a_32(receive->username, strlen(receive->username));
                    if (!users.count(clientHash)) {
                        users.emplace(clientHash, std::make_unique<User>(idMap,clientHash, receive->username));
                        {
                            std::lock_guard<std::mutex> lock(mixerMtx);
                            buffers.emplace(clientHash, std::make_shared<JitterBuffer<std::shared_ptr<float[]>>>(receive->sequence));
                            indexBuffers.push_back(clientHash);
                        }
                        //printf("new user\n");
                    }
                    frame = std::make_shared<float[]>(FRAME_SIZE);
                    userptr = users[clientHash].get();
                    decoded = opus_decode_float(userptr->getOpusDecoder(), receive->frames, 160, frame.get(), FRAME_SIZE, 0);
                    if (decoded == (int)FRAME_SIZE) {
                        {
                            std::lock_guard<std::mutex> lock(mixerMtx);
                            buffers[clientHash].get()->push(receive->sequence, frame);
                        }
                    }
                } else if (bytes < 0) {
                    fprintf(stderr, "wolfSSL_read error\n");
                    //break;
                }
                
            }
            //delete dataForReceive;
            return 0;
        }

        int writeData() {
            uint32_t sendSeq_ = 0;
            auto tempPCMData = std::make_unique<float[]>(FRAME_SIZE);

            send->channel = numchannel;
            strncpy(send->username, username.c_str(), sizeof(send->username) - 1);
            send->username[sizeof(send->username) - 1] = '\0';

            int n;

            while (running) {
                n = recordBuffer.readBlocking(tempPCMData.get(), FRAME_SIZE);
                if (n != (int)FRAME_SIZE) continue;

                int encoded = opus_encode_float(encoder,
                                                tempPCMData.get(), FRAME_SIZE,
                                                send->frames, 160);
                if (encoded < 0) continue;

                send->sequence = sendSeq_++;   // ← инкремент после успешного encode

                if (wolfSSL_write(ssl, send.get(), sizeof(networkDataAudio))
                        != (int)sizeof(networkDataAudio)) {
                    fprintf(stderr, "wolfSSL_write failed\n");
                }
            }
            return 0;
        }

        void controlThread() {

            ssl = wolfSSL_new(ctx);
            if (!ssl) error_handling("wolfSSL_new failed");
            wolfSSL_set_fd(ssl, sock);

            wolfSSL_dtls_set_peer(ssl, (struct sockaddr*)&server_addr, sizeof(server_addr));
            wolfSSL_dtls_set_mtu(ssl, MTU);

            while(wolfSSL_connect(ssl) != SSL_SUCCESS) {
                fprintf(stderr, "wolfSSL_connect failed\n");
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

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

            sock = create_udp_socket();

            #ifdef _WIN32
                if (sock == INVALID_SOCKET) error_handling(" udp socket creation failed");
            #else
                if (sock < 0) error_handling(" udp socket creation failed");
            #endif
            
            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(port);

            if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0)
                error_handling("invalid server IP");

            receive = std::make_unique<networkDataAudio>();
            send = std::make_unique<networkDataAudio>();

            ctx = wolfSSL_CTX_new(wolfDTLSv1_2_client_method());
            if (!ctx) error_handling("wolfSSL_CTX_new failed");
            std::cout << "загрузка системных корневых сертификатов: " << wolfSSL_CTX_load_system_CA_certs(ctx) << std::endl;

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
                #ifdef _WIN32
                    closesocket(sock);
                #else
                    close(sock);
                #endif
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
};