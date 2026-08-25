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

#define PORT 15923
#define BUFFER_SIZE 102400

/* Флаг для graceful shutdown */
static volatile int keep_running = 1;

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


void mainUdpServer(const serverUdpData& cfg) {

    char buf[sizeof(networkDataAudio)];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    while(true) {
        while(true) {

            int sel_ret = waitTimeaut(cfg.server_fd, 3, 0);
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
                //close(cfg.server_fd);
                //wolfSSL_CTX_free(cfg.ctx);
                break;
            }
            printf("Получен первый пакет от %s:%d\n",
            inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

            //handle_dtls_client(cfg.ctx, cfg.server_fd, &client_addr, client_len);
            WOLFSSL* ssl = wolfSSL_new(cfg.ctx);

            //if (wolfSSL_dtls_set_timeout_init(ssl, 1) != SSL_SUCCESS) {
                // Обработка ошибки
            //}
            //if (wolfSSL_dtls_set_timeout_max(ssl, 2) != SSL_SUCCESS) {
                // Обработка ошибки
            //}

            

            if (!ssl) {
                fprintf(stderr, "Ошибка создания SSL-объекта\n");
                //close(cfg.sock);
                //wolfSSL_CTX_free(cfg.ctx);
                break;
            }

            //int ret = wolfSSL_set_timeout(ssl, 3);

            wolfSSL_dtls_set_using_nonblock(ssl, 1);

            wolfSSL_set_fd(ssl, cfg.server_fd);

            

            //if (wolfSSL_dtls_set_peer(ssl, &client_addr, sizeof(client_addr)) != SSL_SUCCESS) {
            //    fprintf(stderr, "Ошибка wolfSSL_dtls_set_peer\n");
            //    wolfSSL_free(ssl);
                //close(sock);
                //wolfSSL_CTX_free(ctx);
            //    break;
            //}

            wolfSSL_dtls_set_mtu(ssl, 5000);

            while (1) {

                int sel_ret = waitTimeaut(cfg.server_fd, 3, 0);
                if (sel_ret == -1) {
                    perror("select");
                    break;
                } else if (sel_ret == 0) {
                    fprintf(stderr, "Тайм-аут рукопожатия\n");
                    break;
                }

                int ret = wolfSSL_accept(ssl);
                if (ret == SSL_SUCCESS) {
                    printf("DTLS рукопожатие успешно завершено\n");
                    // Получаем адрес клиента (опционально)
                    struct sockaddr_in peer_addr;
                    socklen_t peer_len = sizeof(peer_addr);
                    if (wolfSSL_dtls_get_peer(ssl, (struct sockaddr*)&peer_addr, &peer_len) == SSL_SUCCESS) {
                        printf("Клиент: %s:%d\n", inet_ntoa(peer_addr.sin_addr), ntohs(peer_addr.sin_port));
                    }
                    break; // выходим из цикла рукопожатия
                } else {
                    int err = wolfSSL_get_error(ssl, ret);
                    if (err == SSL_ERROR_WANT_READ) {
                        // Данных ещё нет – продолжаем ждать
                        continue;
                    } else {
                        fprintf(stderr, "Ошибка DTLS рукопожатия: %d\n", err);
                        wolfSSL_free(ssl);
                        break;
                    }
                }
            }

            printf("DTLS рукопожатие успешно завершено\n");

            while (1) {

                int sel_ret = waitTimeaut(cfg.server_fd, 3, 0);

                if (sel_ret == -1) {
                    perror("select");
                    break;
                } else if (sel_ret == 0) {
                    fprintf(stderr, "Тайм-аут ожидания данных\n");
                    // Обработка тайм-аута: можно выйти из цикла или продолжить
                    break;
                }
                int ret = wolfSSL_read(ssl, buf, sizeof(buf));
                //std::cout << ret << std::endl;
                if (ret <= 0) {
                    fprintf(stderr, "Ошибка чтения или соединение закрыто\n");
                    break;
                } else {
                    int err = wolfSSL_get_error(ssl, ret);
                    if (err == SSL_ERROR_WANT_READ) {
                        // Данных ещё нет – продолжаем ждать (повторный select)
                        continue;
                    } else {
                        //fprintf(stderr, "Ошибка wolfSSL_read: %d\n", err); //оно всегда при подключении выдаёт ошибку 0, но всё работает, так что я хз что за ошибка
                        //break;
                    }
                }
                    //buffer[ret] = '\0';
                    //printf("Получено: %s\n", buffer);

                    /* Эхо-ответ */
                wolfSSL_write(ssl, buf, ret);
            }
            wolfSSL_clear(ssl);
            wolfSSL_free(ssl);
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
    wolfSSL_Init();
    WOLFSSL_CTX* ctx = wolfSSL_CTX_new(wolfDTLS_server_method());
    //WOLFSSL_CTX *ctx = wolfSSL_CTX_new(wolfTLSv1_2_server_method());
    if (!ctx) {
        fprintf(stderr, "Ошибка создания DTLS контекста\n");
        return 1;
    }

    /* 2. Загрузка сертификата и ключа */
    if (wolfSSL_CTX_use_certificate_file(ctx, "server-cert.pem", SSL_FILETYPE_PEM) != SSL_SUCCESS)
        error_handling("Failed to load server certificate");
    if (wolfSSL_CTX_use_PrivateKey_file(ctx, "server-key.pem", SSL_FILETYPE_PEM) != SSL_SUCCESS)
        error_handling("Failed to load server private key");

    //int client_fd;
    int server_fd;
    struct sockaddr_in server_addr;
    socklen_t client_len = sizeof(struct sockaddr_in);
    char buffer[BUFFER_SIZE];

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

    #ifdef _WIN32
        u_long mode = 1;
        if (ioctlsocket(server_fd, FIONBIO, &mode) != 0) {
            perror("ioctlsocket failed");
            // обработка ошибки
        }
    #else
        int flags = fcntl(server_fd, F_GETFL, 0);
        if (flags == -1 || fcntl(server_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            perror("fcntl failed");
            // обработка ошибки
        }
    #endif

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
    test.join();
    




    /* 9. Завершение работы (освобождение ресурсов) */
    printf("Cleaning up...\n");
    wolfSSL_CTX_free(ctx);
    wolfSSL_Cleanup();
    close(server_fd);
    printf("Server stopped.\n");
    return 0;
}