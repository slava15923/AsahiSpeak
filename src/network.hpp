#pragma once

#ifdef _WIN32
    typedef SOCKET socket_t;

    #define _WIN32_WINNT 0x0600

#else
    typedef int socket_t;
    #include <arpa/inet.h>


#endif
#include "user_settings.h"


#define WOLFSSL_DTLS

#include <wolfssl/options.h>   // Рекомендуется подключать первым для согласованности настроек[reference:0][reference:1]
#include <wolfssl/ssl.h>       // Основной заголовок для работы с SSL/TLS
#include <wolfssl/wolfio.h>    // Важно! Содержит определения для пользовательских I/O колбэков[reference:2][reference:3]
#include <wolfssl/wolfcrypt/settings.h> // Настройки криптографии, часто требуется неявно
#include <wolfssl/wolfcrypt/error-crypt.h> // Для кодов ошибок, если используете wolfCrypt напрямую
#include <wolfssl/openssl/bio.h>

#define MTU 5000

#define PORT 15923

const int FRAME_SIZE = 960; 
const int SAMPLE_RATE = 48000;
const size_t RING_SIZE = SAMPLE_RATE * 0.2;
const int BIT_RATE = 64000;

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
    uint channel;
    float frames[882];

    void setUsername(const char* nick) {

    }
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
