#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <wolfssl/ssl.h>
#include <wolfssl/wolfcrypt/error-crypt.h>

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

/* Универсальная функция для вывода ошибок */
void error_handling(const char *msg) {
    fprintf(stderr, "Error: %s\n", msg);
    exit(EXIT_FAILURE);
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];

    /* Перехват Ctrl+C для корректного завершения */
    //signal(SIGINT, handle_sigint);

    /* 1. Инициализация WolfSSL */
    wolfSSL_Init();
    WOLFSSL_CTX *ctx = wolfSSL_CTX_new(wolfTLSv1_2_server_method());
    if (!ctx) error_handling("wolfSSL_CTX_new failed");

    /* 2. Загрузка сертификата и ключа */
    if (wolfSSL_CTX_use_certificate_file(ctx, "server-cert.pem", SSL_FILETYPE_PEM) != SSL_SUCCESS)
        error_handling("Failed to load server certificate");
    if (wolfSSL_CTX_use_PrivateKey_file(ctx, "server-key.pem", SSL_FILETYPE_PEM) != SSL_SUCCESS)
        error_handling("Failed to load server private key");

    /* (Опционально) можно задать список шифров, но оставим по умолчанию */

    /* 3. Создание TCP-сокета */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
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
    if (listen(server_fd, 5) < 0)
        error_handling("listen failed");

    printf("Echo server is running on port %d (press Ctrl+C to stop)\n", PORT);

    /* 4. Основной цикл приёма соединений */
    while (keep_running) {
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (!keep_running) break; // если выходим, то не ругаемся
            perror("accept");
            continue;
        }

        printf("New connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        /* 5. Создание SSL-объекта для клиента */
        WOLFSSL *ssl = wolfSSL_new(ctx);
        if (!ssl) {
            fprintf(stderr, "wolfSSL_new failed\n");
            close(client_fd);
            continue;
        }
        wolfSSL_set_fd(ssl, client_fd);

        /* 6. TLS-рукопожатие (серверная сторона) */
        if (wolfSSL_accept(ssl) != SSL_SUCCESS) {
            int err = wolfSSL_get_error(ssl, 0);
            char err_buf[80];
            wolfSSL_ERR_error_string(err, err_buf);
            fprintf(stderr, "wolfSSL_accept error: %s\n", err_buf);
            wolfSSL_free(ssl);
            close(client_fd);
            continue;
        }

        /* 7. Эхо-цикл для данного клиента */
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

        /* 8. Очистка для текущего клиента */
        wolfSSL_free(ssl);
        close(client_fd);
        printf("Connection closed\n");
    }

    /* 9. Завершение работы (освобождение ресурсов) */
    printf("Cleaning up...\n");
    wolfSSL_CTX_free(ctx);
    wolfSSL_Cleanup();
    close(server_fd);
    printf("Server stopped.\n");
    return 0;
}