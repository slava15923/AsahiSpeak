#pragma once

#include <string>
#include <stdexcept>
#include <cstring>
#include <iostream>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    
    // В Windows макросы и типы уже определены в winsock2.h
    typedef SOCKET socket_t;
#else
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <netdb.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>

    // Унифицируем макросы для Linux под стиль Windows
    typedef int socket_t;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
#endif

enum class WaitMode { Read, Write, Both };


inline static void close_socket(socket_t sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

inline static int get_network_error() {
#ifdef _WIN32
    return WSAGetLastError();
#endif
    return errno;
}

inline static bool error_is_would_block(int err) {
#ifdef _WIN32
    return err == WSAEWOULDBLOCK;
#else
    return err == EWOULDBLOCK || err == EAGAIN;
#endif
}

inline static bool set_socket_nonblocking(socket_t sock) {
#ifdef _WIN32
    unsigned long mode = 1;
    return ioctlsocket(sock, FIONBIO, &mode) == 0;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

class NonBlockingUdpSocket {
private:
    socket_t m_sock;

    // Запрещаем копирование во избежание двойного закрытия дескриптора
    NonBlockingUdpSocket(const NonBlockingUdpSocket&) = delete;
    NonBlockingUdpSocket& operator=(const NonBlockingUdpSocket&) = delete;

public:
    // Разрешаем перемещение (Move семантика)
    NonBlockingUdpSocket(NonBlockingUdpSocket&& other) noexcept : m_sock(other.m_sock) {
        other.m_sock = INVALID_SOCKET;
    }

    NonBlockingUdpSocket& operator=(NonBlockingUdpSocket&& other) noexcept {
        if (this != &other) {
            close();
            m_sock = other.m_sock;
            other.m_sock = INVALID_SOCKET;
        }
        return *this;
    }

    // Конструктор: создает UDP сокет и переводит его в неблокирующий режим
    NonBlockingUdpSocket() : m_sock(INVALID_SOCKET) {
        m_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_sock == INVALID_SOCKET) {
            throw std::runtime_error("Failed to create socket. Error: " + std::to_string(get_network_error()));
        }

        if (!set_socket_nonblocking(m_sock)) {
            close();
            throw std::runtime_error("Failed to set non-blocking. Error: " + std::to_string(get_network_error()));
        }
    }

    ~NonBlockingUdpSocket() {
        close();
    }

    // БИНД (Связывание с портом и IP)
    bool bind_to(int port, const std::string& ip = "0.0.0.0") {
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);

        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
            return false;
        }

        return bind(m_sock, (struct sockaddr*)&addr, sizeof(addr)) != SOCKET_ERROR;
    }

    // ОТПРАВКА (sendto)
    // Возвращает отправленные байты или -1, если буфер ОС переполнен/ошибка
    int send_to(const void* buffer, size_t size, const std::string& target_ip, int target_port) {
        struct sockaddr_in to_addr;
        std::memset(&to_addr, 0, sizeof(to_addr));
        to_addr.sin_family = AF_INET;
        to_addr.sin_port = htons(target_port);

        if (inet_pton(AF_INET, target_ip.c_str(), &to_addr.sin_addr) != 1) {
            return -1;
        }

        int res = sendto(m_sock, static_cast<const char*>(buffer), static_cast<int>(size), 0,
                         (struct sockaddr*)&to_addr, sizeof(to_addr));

        if (res == SOCKET_ERROR && error_is_would_block(get_network_error())) {
            return -1; // WouldBlock (ОС занята, попробуйте позже)
        }
        return res;
    }

    // ПРИЕМ (recvfrom)
    // Возвращает принятые байты или -1, если данных в сети пока нет (WouldBlock)/ошибка
    int receive_from(void* buffer, size_t size, std::string& out_ip, int& out_port) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        std::memset(&from_addr, 0, sizeof(from_addr));

        int res = recvfrom(m_sock, static_cast<char*>(buffer), static_cast<int>(size), 0,
                                      (struct sockaddr*)&from_addr, &from_len);

        if (res == SOCKET_ERROR) {
            return -1; // При любой ошибке (включая WouldBlock) возвращаем -1
        }

        char ip_str[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, sizeof(ip_str))) {
            out_ip = ip_str;
        }
        out_port = ntohs(from_addr.sin_port);

        return res;
    }

    // ЗАКРЫТИЕ
    void close() {
        if (m_sock != INVALID_SOCKET) {
            close_socket(m_sock);
            m_sock = INVALID_SOCKET;
        }
    }

    int wait_timeout(int seconds, int microseconds = 0, WaitMode mode = WaitMode::Read) const {
        if (m_sock == INVALID_SOCKET) return -1;

        fd_set rfds, wfds;
        fd_set* rp = nullptr;
        fd_set* wp = nullptr;

        if (mode == WaitMode::Read || mode == WaitMode::Both) {
            FD_ZERO(&rfds); FD_SET(m_sock, &rfds); rp = &rfds;
        }
        if (mode == WaitMode::Write || mode == WaitMode::Both) {
            FD_ZERO(&wfds); FD_SET(m_sock, &wfds); wp = &wfds;
        }

        struct timeval tv{ seconds, microseconds };

        #ifdef _WIN32
            int r = select(0, rp, wp, nullptr, &tv);
        #else
            int r = select(m_sock + 1, rp, wp, nullptr, &tv);
        #endif
            if (r == SOCKET_ERROR) return -1;
            return r > 0 ? 1 : 0;
    }

    // ГЕТТЕР СОКЕТА (getsocket) для wolfSSL
    socket_t get_handle() const { return m_sock; }
};

bool init_network() {
#if defined(_WIN32)
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#endif
    return true;
}

void cleanup_network() {
#if defined(_WIN32)
    WSACleanup();
#endif
}