#pragma once

#ifdef _WIN32
    typedef SOCKET socket_t;
#else
    typedef int socket_t;
    #include <arpa/inet.h>


#endif
#include "user_settings.h"
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

void error_handling(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
    //exit(EXIT_FAILURE);
}

typedef struct networkDataAudio_ {
    int sizeFrames;
    float frames[882];
} networkDataAudio;

socket_t create_tcp_socket() {
    socket_t sock;
    // Используйте AF_INET и SOCK_STREAM, которые одинаковы для всех платформ
    sock = socket(AF_INET, SOCK_STREAM, 0);
    // ... проверка на ошибку ...
    return sock;
}