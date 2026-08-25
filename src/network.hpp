#pragma once

#ifdef _WIN32
    typedef SOCKET socket_t;
#else
    typedef int socket_t;
    #include <arpa/inet.h>


#endif
#include "user_settings.h"


#define WOLFSSL_DTLS

#include <wolfssl/options.h> // Опции сборки wolfSSL
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

void error_handling(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
    //exit(EXIT_FAILURE);
}

//ниже дефайны для cmd

#define CONNECT 0//команда которая означает подключение к серверу



typedef struct networkDataAudio {
    char cmd;
    int sizeFrames;
    char username[8];
    char password[8];
    float frames[882];

    void setUsername(const char* nick) {

    }
};

typedef struct networkDataPacket {
    char cmd;
    char username[8];
    char password[8];
};

class serverUserData {
    
};

socket_t create_tcp_socket() {
    socket_t sock;
    // Используйте AF_INET и SOCK_STREAM, которые одинаковы для всех платформ
    sock = socket(AF_INET, SOCK_STREAM, 0);
    // ... проверка на ошибку ...
    return sock;
}

socket_t create_udp_socket() {
    socket_t sock;
    // Используйте AF_INET и SOCK_STREAM, которые одинаковы для всех платформ
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    // ... проверка на ошибку ...
    return sock;
}

struct serverUdpData
{
    bool keep_running;
    //socklen_t client_len = sizeof(client_addr);
    socket_t server_fd;
    struct sockaddr_in server_addr;
    WOLFSSL_CTX *ctx;
};
