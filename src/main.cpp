#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>

#include <SFML/Audio.hpp>
#include <cubeb/cubeb.h>
#include <string.h>

#include <cstdarg>
#include <cstdio>
#include "LockFreeRingBuffer.hpp"
#include "network.hpp"
#include "AudioTransmission.hpp"

#ifdef _WIN32
    #include <combaseapi.h>

#endif




//0,02с ЭТО 882 ФРЕЙМОВ






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
    //std::cout << "writeDataMicro" << nframes << std::endl;
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
    //std::cout << "writeDataDinamic" << nframes << std::endl;
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
    #ifdef _WIN32
        HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    #endif
    char* ip;
    uint16_t port;
    char* username;
    char* password;
    std::cout << argc << std::endl;

    if(argc < 3) {
        throw std::runtime_error("не хватает аргументов");
    }
    ip = argv[1];
    port = std::stoi(argv[2]);
    LockFreeRingBuffer recordBuffer(RING_SIZE);
    LockFreeRingBuffer noiseCancellation(RING_SIZE);
    LockFreeRingBuffer readBuffer(RING_SIZE);
    //recordBuffer.onNoiseGate(0.8, 0.001, 0.05, SAMPLE_RATE);

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


    #ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    #endif


    
    

    wolfSSL_Init();
    AudioTransmission udpClient(ip, port, "admin", "admin", 0, &recordBuffer, &readBuffer);

    udpClient.addServerSert("server-cert.pem");

    udpClient.startTransmission();

    std::this_thread::sleep_for(std::chrono::seconds(5));

    

    getchar();

    udpClient.stopTransmission();

    wolfSSL_Cleanup();





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
