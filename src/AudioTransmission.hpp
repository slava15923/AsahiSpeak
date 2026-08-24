#pragma once
#include "network.hpp"
#include "LockFreeRingBuffer.hpp"
#include <thread>
#include <chrono>

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

        int sock;
        struct sockaddr_in server_addr;

        LockFreeRingBuffer* readBuffer;
        LockFreeRingBuffer* recordBuffer;

        WOLFSSL_CTX* ctx;
        WOLFSSL *ssl;

        std::thread write;
        std::thread read;

        int readData() {
            //networkDataAudio* dataForReceive = new networkDataAudio;
            while(true) {
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
            //delete dataForReceive;
            return 0;
        }

        int writeData() {
            //networkDataAudio* dataForSend = new networkDataAudio;
            while(true) {
                send.get()->sizeFrames = recordBuffer->readBlocking(send.get()->frames, 882);
                if (wolfSSL_write(ssl, send.get(), sizeof(networkDataAudio)) != (int)sizeof(networkDataAudio)) {
                    fprintf(stderr, "wolfSSL_write failed\n");
                    //break;
                } else {
                    //printf("Sent: output networkDataAudio");
                }
                
            }
            //delete dataForSend;
            return 0;
        }

    public:
        AudioTransmission(const char* ip, uint16_t port, 
            const char* username, const char* password, 
            int numchannel, LockFreeRingBuffer* recordBuffer_, 
            LockFreeRingBuffer* readBuffer_) 
            : recordBuffer(recordBuffer_), readBuffer(readBuffer_) {

            sock = create_udp_socket();
            if (sock < 0) error_handling(" udp socket creation failed");

            server_addr.sin_family = AF_INET;
            server_addr.sin_port = htons(port);

            if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0)
                error_handling("invalid server IP");

            receive = std::make_unique<networkDataAudio>();
            send = std::make_unique<networkDataAudio>();

            ctx = wolfSSL_CTX_new(wolfDTLS_client_method());
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
            ssl = wolfSSL_new(ctx);
            if (!ssl) error_handling("wolfSSL_new failed");
            wolfSSL_set_fd(ssl, sock);

            wolfSSL_dtls_set_peer(ssl, (struct sockaddr*)&server_addr, sizeof(server_addr));
            wolfSSL_dtls_set_mtu(ssl, 5000);

            while(wolfSSL_connect(ssl) != SSL_SUCCESS) {
                fprintf(stderr, "wolfSSL_connect failed\n");
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }

            printf("TLS handshake successful\n");
            write = std::thread(&AudioTransmission::writeData, this);
            read = std::thread(&AudioTransmission::readData, this);


        }

        void stopTransmission() {
            wolfSSL_free(ssl);
        }



        void addServerSert(const char *file) {
            if (wolfSSL_CTX_load_verify_locations(ctx, file, 0) != SSL_SUCCESS) {
                fprintf(stderr, "Warning: CA certificates not loaded, trying without verification\n");
                wolfSSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, 0);
            }
        }
};