#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include "network.hpp"
#include <thread>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <condition_variable>
#include <queue>
#include <atomic>



/* Флаг для graceful shutdown */
static volatile int keep_running = 1;


int recvCallback(WOLFSSL* ssl, char* buf, int sz, void* ctx);
int sendCallback(WOLFSSL* ssl, char* buf, int sz, void* ctx);

/*
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, []{ return !q.empty(); }); // Ждём, пока очередь не станет непустой
*/
class SessionData {
public:
    WOLFSSL* ssl;
    struct sockaddr_in addr;
    socklen_t addr_len;
    uint64_t clientHash;
    socket_t server_fd;
    std::chrono::steady_clock::time_point last_used;
    bool handshake_done = false;   // <-- новый флаг
    char* buf;
    int sizeBuf = 5000;
    char errstr[256];
    std::string username;

    SessionData(const uint64_t& clientHash_, socket_t server_fd_,
                WOLFSSL_CTX* ctx, const struct sockaddr_in addr_)
        : clientHash(clientHash_), addr(addr_), server_fd(server_fd_), addr_len(sizeof(addr_)) {
        ssl = wolfSSL_new(ctx);
        if (!ssl) throw std::runtime_error("wolfSSL_new failed");

        wolfSSL_dtls_set_mtu(ssl, MTU);
        wolfSSL_set_using_nonblock(ssl, 1);

        wolfSSL_SetIOWriteCtx(ssl, this);
        wolfSSL_SetIOReadCtx(ssl, nullptr);
        buf = new char[sizeBuf]; 


        updateLastUsed();
    }

    ~SessionData() {
        if (ssl) wolfSSL_free(ssl);
    }

    void updateLastUsed() {
        last_used = std::chrono::steady_clock::now();
    }

    // Основной метод обработки входящего пакета
    bool processIncoming(const char* data, int sz) {
        if (wolfSSL_inject(ssl, data, sz) != WOLFSSL_SUCCESS) {
            return false;
        }
        updateLastUsed();

        if (!handshake_done) {
            int ret = wolfSSL_accept(ssl);
            if (ret == WOLFSSL_SUCCESS) {
                handshake_done = true;
                printf("DTLS handshake success for client %llu\n", (unsigned long long)clientHash);
                return true; 
            } else {
                int err = wolfSSL_get_error(ssl, ret);
                if (err != WOLFSSL_ERROR_WANT_READ && err != WOLFSSL_ERROR_WANT_WRITE) {
                    wolfSSL_ERR_error_string_n(err, errstr, sizeof(errstr));
                    fprintf(stderr, "DTLS accept error: %d (%s)\n", err, errstr);
                    return false;
                }
                return true;
            }
        }

        
        while (true) {
            int ret = wolfSSL_read(ssl, buf, sizeBuf);
            if (ret > 0) {
                networkDataAudio* audio = reinterpret_cast<networkDataAudio*>(buf);
                username = audio->username;
                wolfSSL_write(ssl, buf, ret);
                updateLastUsed();
            } else if (ret == 0) {
                return false;
            } else {
                int err = wolfSSL_get_error(ssl, ret);
                if (err == WOLFSSL_ERROR_WANT_READ || err == WOLFSSL_ERROR_WANT_WRITE) {
                    break;
                } else {
                    return false;
                }
            }
        }
        return true;
    }

    void retransmitHandshake() {
        if (!handshake_done) {
            wolfSSL_accept(ssl);// повторно отправит последнее сообщение
            updateLastUsed();
        }
    }

    int64_t lastUse() {
        auto now = std::chrono::steady_clock::now();
        auto seconds_passed = std::chrono::duration_cast<std::chrono::seconds>(now - last_used).count();
        return seconds_passed;
    }
    std::string& getUserName() {
        return username;
    }
};

std::unordered_map<uint64_t, std::unique_ptr<SessionData>> sessions;
std::vector<uint64_t> sessionsHash;

/* Обработчик сигнала SIGINT (Ctrl+C) */
void handle_sigint(int sig) {
    (void)sig;
    keep_running = 0;
    printf("\nShutting down server...\n");
}

int waitTimeaut(socket_t server_fd, int second, int microsecond) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(server_fd, &readfds);

    struct timeval timeout;
    timeout.tv_sec = second; // Тайм-аут на чтение 3 секунды
    timeout.tv_usec = microsecond;

    int select_ret = select(server_fd + 1, &readfds, NULL, NULL, &timeout);
    
    return select_ret;
}

uint64_t addr_hash(const struct sockaddr_in& addr) {
    uint64_t h = 0;
    h ^= (uint64_t)addr.sin_addr.s_addr << 32;
    h ^= (uint64_t)addr.sin_port;
    return h;
}


void mainUdpServer(const serverUdpData& cfg) {

    char buf[sizeof(networkDataAudio)+ 5000];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    while(true) {
        while(true) {
            int sel_ret = waitTimeaut(cfg.server_fd, 5, 0);
            if (sel_ret == -1) {
                perror("select");
                break;
            } else if (sel_ret == 0) {
                fprintf(stderr, "Тайм-аут ожидания первого пакета\n");
                break;  // или continue, если нужно ждать дальше
            }
            int n = recvfrom(cfg.server_fd, buf, sizeof(buf), 0, (struct sockaddr*)&client_addr, &client_len);

            if (n < 0) {
                perror("recvfrom");
                break;
            }


            uint64_t clientHash = addr_hash(client_addr);



            if(sessions.count(clientHash)) {
                sessions[clientHash].get()->processIncoming(buf, n);
                //std::cout << clientHash << "a" << std::endl;
            } else {
                sessions.insert({clientHash, std::make_unique<SessionData>(clientHash, cfg.server_fd, cfg.ctx, client_addr)});
                sessions[clientHash].get()->processIncoming(buf, n);
                sessionsHash.push_back(clientHash);

                printf("новый коннект: %s:%d\n",
                inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
                std::cout << clientHash << std::endl;
                std::cout << sessions[clientHash].get()->getUserName() << std::endl;
            }
        }
    }
}

int main() {
    #ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    #endif
    std::cout << SSL_ERROR_WANT_READ << std::endl;
    
    


    /* Перехват Ctrl+C для корректного завершения */
    //signal(SIGINT, handle_sigint);

    /* 1. Инициализация WolfSSL */
    wolfSSL_Debugging_ON();
    wolfSSL_Init();
    WOLFSSL_CTX* ctx = wolfSSL_CTX_new(wolfDTLSv1_2_server_method());
    //WOLFSSL_CTX *ctx = wolfSSL_CTX_new(wolfTLSv1_2_server_method());
    if (!ctx) {
        fprintf(stderr, "Ошибка создания DTLS контекста\n");
        return 1;
    }

    wolfSSL_CTX_SetIORecv(ctx, recvCallback);
    wolfSSL_CTX_SetIOSend(ctx, sendCallback);

    /* 2. Загрузка сертификата и ключа */
    if (wolfSSL_CTX_use_certificate_file(ctx, "server-cert.pem", SSL_FILETYPE_PEM) != SSL_SUCCESS)
        error_handling("Failed to load server certificate");
    if (wolfSSL_CTX_use_PrivateKey_file(ctx, "server-key.pem", SSL_FILETYPE_PEM) != SSL_SUCCESS)
        error_handling("Failed to load server private key");

    //int client_fd;
    int server_fd;
    struct sockaddr_in server_addr;
    socklen_t client_len = sizeof(struct sockaddr_in);

    /* (Опционально) можно задать список шифров, но оставим по умолчанию */

    /* 3. Создание TCP-сокета */
    server_fd = create_udp_socket();
    if (server_fd < 0) error_handling("socket creation failed");

    /* Разрешаем переиспользование адреса (полезно при перезапуске) */
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        /* Не критично, продолжаем */
    }

    int flags = fcntl(server_fd, F_GETFL, 0);
    if (flags == -1 || fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl failed");
        // обработка ошибки
    }


    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
        error_handling("bind failed");
    //if (listen(server_fd, 5) < 0)
    //    error_handling("listen failed");

    printf("Echo udp server is running on port %d (press Ctrl+C to stop)\n", PORT);

    serverUdpData udpData;
    udpData.ctx = ctx;
    //udpData.client_addr = client_addr;
    udpData.keep_running = 1;
    udpData.server_fd = server_fd;
    std::thread test(mainUdpServer, std::ref(udpData));
    //wolfSSL_Debugging_ON();
    test.join();
    




    /* 9. Завершение работы (освобождение ресурсов) */
    printf("Cleaning up...\n");
    wolfSSL_CTX_free(ctx);
    wolfSSL_Cleanup();
    close(server_fd);
    printf("Server stopped.\n");
    return 0;
}

int recvCallback(WOLFSSL* ssl, char* buf, int sz, void* ctx) {
    (void)ssl; (void)buf; (void)sz; (void)ctx;
    
    // Говорим библиотеке: "В сети пусто, читай то, что лежит во внутреннем буфере инжекта"
    return WOLFSSL_CBIO_ERR_WANT_READ; 
}

int sendCallback(WOLFSSL* ssl, char* buf, int sz, void* ctx) {
    SessionData* session = (SessionData*)wolfSSL_GetIOWriteCtx(ssl);
    if (!session) return WOLFSSL_CBIO_ERR_GENERAL;

    int ret = sendto(session->server_fd, buf, sz, 0,
                     (struct sockaddr*)&session->addr, session->addr_len);
    if (ret < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return WOLFSSL_CBIO_ERR_WANT_WRITE;
        return WOLFSSL_CBIO_ERR_GENERAL;
    }
    return ret;
}