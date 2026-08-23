#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>

#include <SFML/Audio.hpp>
#include <baudvine/ringbuf.h>
#include <cubeb/cubeb.h>
#include <string.h>

#include <cstdarg>
#include <cstdio>
#include "LockFreeRingBuffer.hpp"
#include "network.hpp"




//0,02с ЭТО 882 ФРЕЙМОВ

void Network(LockFreeRingBuffer& input, LockFreeRingBuffer& output, uint port, const char* ip);
int writeDataInSocket(WOLFSSL *ssl, LockFreeRingBuffer& recordBuffer);
int readDataInSocket(WOLFSSL *ssl, LockFreeRingBuffer& readBuffer);





const int SAMPLE_RATE = 44100;

const size_t RING_SIZE = SAMPLE_RATE * 5;  // 220500 фреймов




extern "C" void state_cb(cubeb_stream *stream, void *user_ptr, cubeb_state state) {
    printf("Состояние потока изменилось: %d\n", state);
    //return CUBEB_OK;
}


/*НЕ РАБОТАЕТ!!! функцию написала ии*/
extern "C" long data_fullduplex(cubeb_stream * stm, void * user,
             const void * input_buffer,  // Данные с микрофона
             void * output_buffer,       // Буфер для заполнения (динамики)
             long nframes) {             // Количество кадров для обработки

    const float * in = (const float*)input_buffer;
    float * out = (float*)output_buffer;
    for (int i = 0; i < nframes; ++i) {
        for (int c = 0; c < 2; ++c) { 
            out[i * 2 + c] = in[i];
        }
    }
    return nframes;
}

/*функция для работы с МОНО микрофоном в cubeb*/
extern "C" long data_micro(cubeb_stream * stm, void * user,
             const void * input_buffer,  // Данные с микрофона
             void * output_buffer,       // Буфер для заполнения (динамики)
             long nframes) {             // Количество кадров для обработки
    //std::cout << nframes << std::endl;
    LockFreeRingBuffer* recordBuffer = (LockFreeRingBuffer*)user;

    const float* in = static_cast<const float*>(input_buffer);
    recordBuffer->write(in, nframes);
    return nframes; // Возвращаем количество обработанных кадров
}

/*функция для работы с МОНО динамиками в cubeb*/
extern "C" long data_dinamic(cubeb_stream * stm, void * user,
             const void * input_buffer,  // Данные с микрофона
             void * output_buffer,       // Буфер для заполнения (динамики)
             long nframes) {             // Количество кадров для обработки

    LockFreeRingBuffer* recordBuffer = (LockFreeRingBuffer*)user;
    float* out = static_cast<float*>(output_buffer);
    int channels = 1; 

    size_t read = recordBuffer->read(out, nframes);

    if (read < (size_t)nframes) {
        // Заполняет нулями
        memset(out + read * channels, 0, (nframes - read) * channels * sizeof(float));
    }
    return nframes;
}




extern "C" void cubebCallback(const char *fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    fprintf(stderr, "Cubeb Log: %s\n", buffer);
}


void printHelp() {
std::string helpText = "-help - help command";

    std::cout << helpText << std::endl;
}

int main(int argc, char* argv[]) {
    LockFreeRingBuffer recordBuffer(RING_SIZE);
    LockFreeRingBuffer readBuffer(RING_SIZE);
    recordBuffer.onNoiseGate(1, 0.001, 0.05, SAMPLE_RATE);

    std::cout << "hello world" << std::endl;

    uint32_t rate;
    uint32_t latency_frames;

    cubeb * app_ctx;
    //cubeb_stream * stm = nullptr;

    cubeb_stream * stm_micro = nullptr;
    cubeb_stream * stm_dinamic = nullptr;

    //std::cout << cubeb_set_log_callback(CUBEB_LOG_VERBOSE, cubebCallback) << std::endl;

    
    std::cout << cubeb_init(&app_ctx, "Example Application", nullptr) << std::endl;

    std::cout << cubeb_get_preferred_sample_rate(app_ctx, &rate) << std::endl;

    

    // Параметры ВЫХОДА
    cubeb_stream_params output_params;
    output_params.format = CUBEB_SAMPLE_FLOAT32NE; 
    output_params.rate = SAMPLE_RATE;                    
    output_params.channels = 1;                  
    output_params.layout = CUBEB_LAYOUT_UNDEFINED;
    output_params.prefs = CUBEB_STREAM_PREF_NONE; 

    // Параметры ВХОДА
    cubeb_stream_params input_params;
    input_params.format = CUBEB_SAMPLE_FLOAT32NE;
    input_params.rate = SAMPLE_RATE;
    input_params.channels = 1;                  
    input_params.layout = CUBEB_LAYOUT_UNDEFINED;
    input_params.prefs = CUBEB_STREAM_PREF_NONE;

    std::cout << cubeb_get_min_latency(app_ctx, &output_params, &latency_frames) << std::endl;

    std::cout << latency_frames << std::endl;

    //cubeb_stream_init(app_ctx, &stm, "Test", NULL, &input_params, NULL, &output_params, latency_frames, data_fullduplex, state_cb, NULL);

    //cubeb_stream_start(stm);

    cubeb_stream_init(app_ctx, &stm_dinamic, "Test", NULL, 
        NULL, NULL, &output_params, latency_frames, 
        data_dinamic, state_cb, &readBuffer);

    cubeb_stream_init(app_ctx, &stm_micro, "Test", NULL, 
        &input_params, NULL, NULL, latency_frames, 
        data_micro, state_cb, &recordBuffer);

    cubeb_stream_start(stm_micro);
    cubeb_stream_start(stm_dinamic);
    Network(readBuffer,recordBuffer, 15923,"127.0.0.1");





    //while (true) { std::this_thread::sleep_for(std::chrono::seconds(100));}

    getchar();

    //cubeb_stream_stop(stm);
    cubeb_stream_stop(stm_dinamic);
    cubeb_stream_stop(stm_micro);

    //cubeb_stream_destroy(stm);
    cubeb_stream_destroy(stm_micro);
    cubeb_stream_destroy(stm_dinamic);

    cubeb_destroy(app_ctx);

    return 0;
}

void Network(LockFreeRingBuffer& readBuffer, LockFreeRingBuffer& recordBuffer, uint port, const char* ip) {

    #ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    #endif

    int sock = create_tcp_socket();
    struct sockaddr_in server_addr;
    //char buffer[sizeof(networkDataAudio)];
    //const char *msg = "Hello from WolfSSL client!\n";
    
    

    wolfSSL_Init();
    WOLFSSL_CTX *ctx = wolfSSL_CTX_new(wolfTLSv1_2_client_method());
    if (!ctx) error_handling("wolfSSL_CTX_new failed");

    wolfSSL_CTX_load_system_CA_certs(ctx);
    if (wolfSSL_CTX_load_verify_locations(ctx, "server-cert.pem", 0) != SSL_SUCCESS) {
        fprintf(stderr, "Warning: CA certificates not loaded, trying without verification\n");
        wolfSSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, 0);
    }

    if (sock < 0) error_handling("socket creation failed");

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0)
        error_handling("invalid server IP");

    while (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        error_handling("client connect failed");
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // 4. Создание SSL-объекта и установка дескриптора
    WOLFSSL *ssl = wolfSSL_new(ctx);
    if (!ssl) error_handling("wolfSSL_new failed");
    wolfSSL_set_fd(ssl, sock);


    while(wolfSSL_connect(ssl) != SSL_SUCCESS) {
        fprintf(stderr, "wolfSSL_connect failed\n");
        wolfSSL_free(ssl);
        close(sock);
        wolfSSL_CTX_free(ctx);
        wolfSSL_Cleanup();
        exit(EXIT_FAILURE);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    printf("TLS handshake successful\n");

    std::thread write(writeDataInSocket, ssl, std::ref(recordBuffer));
    std::thread read(readDataInSocket, ssl, std::ref(readBuffer));

    getchar();

    wolfSSL_free(ssl);
    #ifdef _WIN32
        closesocket(sock);
        WSACleanup();
    #else
        close(sock);
    #endif
    wolfSSL_CTX_free(ctx);
    wolfSSL_Cleanup();
}

int writeDataInSocket(WOLFSSL *ssl, LockFreeRingBuffer& recordBuffer) {
    networkDataAudio* dataForSend = new networkDataAudio;
    while(true) {
        dataForSend->sizeFrames = recordBuffer.readBlocking(dataForSend->frames, 882);
        if (wolfSSL_write(ssl, dataForSend, sizeof(networkDataAudio)) != (int)sizeof(networkDataAudio)) {
            fprintf(stderr, "wolfSSL_write failed\n");
            break;
        } else {
            //printf("Sent: output networkDataAudio");
        }
        
    }
    delete dataForSend;
    return 0;
}
int readDataInSocket(WOLFSSL *ssl, LockFreeRingBuffer& readBuffer) {
    networkDataAudio* dataForReceive = new networkDataAudio;
    while(true) {
        int bytes = wolfSSL_read(ssl, dataForReceive, sizeof(networkDataAudio));
        if (bytes > 0) {
            //buffer[bytes] = '\0';
            //printf("Received: readBuffer");
            readBuffer.write(dataForReceive->frames, dataForReceive->sizeFrames);
        } else {
            fprintf(stderr, "wolfSSL_read error or connection closed\n");
            break;
        }
    }
    delete dataForReceive;
    return 0;
}