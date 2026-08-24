#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include "network.hpp"
#include <thread>
#include <vector>

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


void mainUdpServer(const serverUdpData& cfg) {

    char buf[sizeof(networkDataAudio)];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while(true) {
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

        if (wolfSSL_dtls_set_timeout_init(ssl, 1) != SSL_SUCCESS) {
            // Обработка ошибки
        }
        if (wolfSSL_dtls_set_timeout_max(ssl, 2) != SSL_SUCCESS) {
            // Обработка ошибки
        }

        if (!ssl) {
            fprintf(stderr, "Ошибка создания SSL-объекта\n");
            //close(cfg.sock);
            //wolfSSL_CTX_free(cfg.ctx);
            break;
        }

        wolfSSL_set_fd(ssl, cfg.server_fd);

        

        if (wolfSSL_dtls_set_peer(ssl, &client_addr, sizeof(client_addr)) != SSL_SUCCESS) {
            fprintf(stderr, "Ошибка wolfSSL_dtls_set_peer\n");
            wolfSSL_free(ssl);
            //close(sock);
            //wolfSSL_CTX_free(ctx);
            break;
        }

        wolfSSL_dtls_set_mtu(ssl, 5000);

        if (wolfSSL_accept(ssl) != SSL_SUCCESS) {
            fprintf(stderr, "Ошибка DTLS рукопожатия\n");
            wolfSSL_free(ssl);
            //close(sock);
            //wolfSSL_CTX_free(ctx);
            break;
        }

        printf("DTLS рукопожатие успешно завершено\n");

        while (1) {
            int ret = wolfSSL_read(ssl, buf, sizeof(buf));
            if (ret <= 0) {
                fprintf(stderr, "Ошибка чтения или соединение закрыто\n");
                break;
            }
                //buffer[ret] = '\0';
                //printf("Получено: %s\n", buffer);

                /* Эхо-ответ */
            wolfSSL_write(ssl, buf, ret);
        }
    }


    /* 4. Основной цикл приёма соединений */
    /*sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[sizeof(networkDataAudio)];
    while (cfg.keep_running) {
        int client_fd = accept(cfg.server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (!keep_running) break; // если выходим, то не ругаемся
            perror("accept");
            continue;
        }

        printf("New connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));


        WOLFSSL *ssl = wolfSSL_new(cfg.ctx);
        if (!ssl) {
            fprintf(stderr, "wolfSSL_new failed\n");
            close(client_fd);
            continue;
        }
        wolfSSL_set_fd(ssl, client_fd);

        if (wolfSSL_accept(ssl) != SSL_SUCCESS) {
            int err = wolfSSL_get_error(ssl, 0);
            char err_buf[80];
            wolfSSL_ERR_error_string(err, err_buf);
            fprintf(stderr, "wolfSSL_accept error: %s\n", err_buf);
            wolfSSL_free(ssl);
            close(client_fd);
            continue;
        }


        int bytes;
        while ((bytes = wolfSSL_read(ssl, buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes] = '\0';
            //printf("Received: %s", buffer); // сообщение может не заканчиваться на \n, но для наглядности
            if (wolfSSL_write(ssl, buffer, bytes) != bytes) {
                fprintf(stderr, "wolfSSL_write failed\n");
                break;
            }
        }
        if (bytes < 0) {
            int err = wolfSSL_get_error(ssl, 0);
            char err_buf[80];
            wolfSSL_ERR_error_string(err, err_buf);
            fprintf(stderr, "wolfSSL_read error: %s\n", err_buf);
        } else if (bytes == 0) {
            printf("Client closed connection\n");
        }
    }*/
}

int main() {
    #ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    #endif
    
    


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