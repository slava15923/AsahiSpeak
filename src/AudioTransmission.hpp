#pragma once
#include "network.hpp"
#include "LockFreeRingBuffer.hpp"
#include <thread>
#include <chrono>
#include <optional>

/*
    #ifdef _WIN32
        closesocket(sock);
        WSACleanup();
    #else
        close(sock);
    #endif
*/

class AudioTransmission {
    private:
        std::unique_ptr<networkDataAudio> receive;
        std::unique_ptr<networkDataAudio> send;

        socket_t sock;
        struct sockaddr_in server_addr;

        LockFreeRingBuffer* readBuffer;
        LockFreeRingBuffer* recordBuffer;

        WOLFSSL_CTX* ctx;
        //std::optional<WOLFSSL*> ssl;
        WOLFSSL* ssl;

        std::thread write;
        std::thread read;
        std::thread controlthread;

        std::atomic<bool> running;
        std::atomic<bool> statusReadData, statusWriteData;

        int readData() {
            //networkDataAudio* dataForReceive = new networkDataAudio;
            statusReadData = true;
            while(running) {
                int bytes = wolfSSL_read(ssl, receive.get(), sizeof(networkDataAudio));
                if (bytes > 0) {
                    //buffer[bytes] = '\0';
                    //printf("Received: readBuffer");
                    readBuffer->write(receive.get()->frames, receive.get()->sizeFrames);
                } else {
                    fprintf(stderr, "wolfSSL_read error or connection closed\n");
                    break;
                }
            }
            statusReadData = false;
            //delete dataForReceive;
            return 0;
        }

        int writeData() {
            statusWriteData = true;
            //networkDataAudio* dataForSend = new networkDataAudio;
            while(running) {
                send.get()->sizeFrames = recordBuffer->readBlocking(send.get()->frames, 882);
                if (wolfSSL_write(ssl, send.get(), sizeof(networkDataAudio)) != (int)sizeof(networkDataAudio)) {
                    fprintf(stderr, "wolfSSL_write failed\n");
                    //break;
                } else {
                    //printf("Sent: output networkDataAudio");
                }
                
            }
            statusWriteData = false;
            //delete dataForSend;
            return 0;
        }

        void controlThread() {

            ssl = wolfSSL_new(ctx);
            if (!ssl) error_handling("wolfSSL_new failed");
            wolfSSL_set_fd(ssl, sock);

            wolfSSL_dtls_set_peer(ssl, (struct sockaddr*)&server_addr, sizeof(server_addr));
            wolfSSL_dtls_set_mtu(ssl, MTU);
            
            //if (wolfSSL_dtls_cid_use(ssl) != WOLFSSL_SUCCESS) {
            //    fprintf(stderr, "wolfSSL_dtls_cid_use failed\n");
            //}
            // 4. Устанавливаем желаемый CID (предлагаем серверу)
            //if (wolfSSL_dtls_cid_set(ssl, cid, CID_LEN) != WOLFSSL_SUCCESS) {
            //    fprintf(stderr, "wolfSSL_dtls_cid_set failed\n");
            //}


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
            write.detach();
            read.detach();

            while(running) {std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            while(statusReadData || statusWriteData) {}
        }

    public:
        AudioTransmission(const char* ip, uint16_t port, 
            const char* username, const char* password, 
            int numchannel, LockFreeRingBuffer* recordBuffer_, 
            LockFreeRingBuffer* readBuffer_) 
            : recordBuffer(recordBuffer_), readBuffer(readBuffer_) {

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
            wolfSSL_CTX_load_system_CA_certs(ctx);

            
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
                while(statusReadData || statusWriteData) {}
                wolfSSL_free(ssl);
            }
        }



        void addServerSert(const char *file) {
            if (wolfSSL_CTX_load_verify_locations(ctx, file, 0) != SSL_SUCCESS) {
                fprintf(stderr, "Warning: CA certificates not loaded, trying without verification\n");
                wolfSSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, 0);
            }
        }
};