#pragma once

#ifdef _WIN32
    #define socket_t SOCKET

    #define _WIN32_WINNT 0x0600

#else
    typedef int socket_t;
    #include <arpa/inet.h>


#endif

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    //#pragma comment(lib, "ws2_32.lib")
#else
    #include <sys/socket.h>
    #include <netdb.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

#include "user_settings.h"


#define WOLFSSL_DTLS
#include <string>

#include <wolfssl/options.h>
#include <wolfssl/ssl.h>
#include <wolfssl/wolfio.h>
#include <wolfssl/wolfcrypt/settings.h>
#include <wolfssl/wolfcrypt/error-crypt.h>
#include <wolfssl/openssl/bio.h>
#include <wolfssl/openssl/evp.h>

#include <stdexcept>
#include <memory>
#include <iostream>

#define MTU 1200

#define PORT 15923

#define VERSION "0.1.0"

const int FRAME_SIZE = 960; 
const int SAMPLE_RATE = 48000;
const size_t RING_SIZE = SAMPLE_RATE * 0.2;
const int BIT_RATE = 64000;

const int SERVER_TIME_OUT = 5;//в секундах

void error_handling(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
    //exit(EXIT_FAILURE);
}
//ниже дефайны для cmd

#define CONNECT 0//команда которая означает подключение к серверу



struct networkDataAudio {
    char cmd;
    uint64_t clientHash;
    uint32_t sequence;
    char username[33];
    char password[32];
    unsigned char channel;
    unsigned char frames[160];
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


std::string resolve_ip_or_dns(const std::string& host) {
    // 1) Проверяем, является ли строка IP-адресом (без сетевых запросов)
    struct sockaddr_storage ss;
    socklen_t ss_len = sizeof(ss);

    // Пробуем распарсить как IPv4
    if (inet_pton(AF_INET, host.c_str(), &((struct sockaddr_in*)&ss)->sin_addr) == 1) {
        return host;  // Можно вернуть как есть, либо нормализовать через inet_ntop
    }
    // Пробуем как IPv6
    if (inet_pton(AF_INET6, host.c_str(), &((struct sockaddr_in6*)&ss)->sin6_addr) == 1) {
        return host;
    }

    // 2) Не IP – разрешаем как DNS-имя
    struct addrinfo hints, *result = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // возвращать и IPv4, и IPv6
    hints.ai_socktype = SOCK_STREAM;  // не важно для разрешения
    hints.ai_protocol = 0;

    int err = getaddrinfo(host.c_str(), nullptr, &hints, &result);
    if (err != 0) {
#ifdef _WIN32
        // В Windows getaddrinfo возвращает код ошибки, но можно получить текст через gai_strerrorA
        throw std::runtime_error("DNS resolution failed for " + host + ": " + gai_strerror(err));
#else
        throw std::runtime_error("DNS resolution failed for " + host + ": " + gai_strerror(err));
#endif
    }

    // Умный указатель для автоматического освобождения памяти addrinfo
    struct AddrinfoDeleter {
        void operator()(struct addrinfo* p) const { if (p) freeaddrinfo(p); }
    };
    std::unique_ptr<struct addrinfo, AddrinfoDeleter> result_ptr(result);

    // Перебираем полученные адреса и берём первый
    char ip_str[INET6_ADDRSTRLEN];
    for (struct addrinfo* p = result; p != nullptr; p = p->ai_next) {
        void* addr = nullptr;
        int family = p->ai_family;

        if (family == AF_INET) {
            addr = &((struct sockaddr_in*)p->ai_addr)->sin_addr;
        } else if (family == AF_INET6) {
            addr = &((struct sockaddr_in6*)p->ai_addr)->sin6_addr;
        } else {
            continue; // не поддерживается
        }

        // Преобразуем бинарный адрес в строку
        const char* str = inet_ntop(family, addr, ip_str, sizeof(ip_str));
        if (str != nullptr) {
            return std::string(str);
        }
    }

    // Если ни один адрес не подошёл (хотя результат не пуст)
    throw std::runtime_error("No valid IP address found for " + host);
}

// Настройка кривых генерации ключей ECDHE (X25519 в приоритете)
inline void SetBrowserECCGroups(WOLFSSL_CTX* ctx) {
    // Используем массив встроенных ID wolfSSL напрямую:
    // WOLFSSL_ECC_X25519     -> x25519
    // WOLFSSL_ECC_SECP256R1  -> P-256 (prime256v1)
    int groups[] = { WOLFSSL_ECC_X25519, WOLFSSL_ECC_SECP256R1 };
    
    // Функция wolfSSL_CTX_set_groups принимает массив ID и его длину
    if (wolfSSL_CTX_set_groups(ctx, groups, 2) != WOLFSSL_SUCCESS) {
        std::cerr << "Критическая ошибка: Кривые X25519 или P-256 не поддерживаются этой сборкой wolfSSL!" << std::endl;
    }
}

// Настройка шифров Chrome/Firefox для DTLS 1.3
inline void SetBrowserDtls13Ciphers(WOLFSSL_CTX* ctx) {
    const char* chrome_firefox_dtls13_ciphers = 
        "TLS13-AES128-GCM-SHA256:"
        "TLS13-AES256-GCM-SHA384:"
        "TLS13-CHACHA20-POLY1305-SHA256";

    if (wolfSSL_CTX_set_cipher_list(ctx, chrome_firefox_dtls13_ciphers) != WOLFSSL_SUCCESS) {
        std::cerr << "Ошибка: Не удалось применить шифронаборы браузера для DTLS 1.3!" << std::endl;
        std::cerr << "Проверьте, включены ли HAVE_CHACHA и HAVE_AESGCM в user_settings.h" << std::endl;
    }
}
