#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>

#include <cubeb/cubeb.h>
#include <string.h>

#include <cstdarg>
#include <cstdio>
#include "LockFreeRingBuffer.hpp"
#include "network.hpp"
#include "AudioTransmission.hpp"
#include <audioIO.hpp>
#include "gui/gui.hpp"



#ifdef _WIN32
    #include <combaseapi.h>

#endif




//0,02с ЭТО 882 ФРЕЙМОВ

float noSound[FRAME_SIZE] = {0};






extern "C" void state_cb(cubeb_stream *stream, void *user_ptr, cubeb_state state) {
    printf("Состояние потока изменилось: %d\n", state);
    //return CUBEB_OK;
}


extern "C" long data_fullduplex(cubeb_stream * stm, void * user,
             const void * input_buffer,
             void * output_buffer,
             long nframes) {

    const float * in = (const float*)input_buffer;
    float * out = (float*)output_buffer;
    for (int i = 0; i < nframes; ++i) {
        for (int c = 0; c < 2; ++c) { 
            out[i * 2 + c] = in[i];
        }
    }
    
    return nframes;
}


extern "C" long data_micro(cubeb_stream * stm, void * user,
             const void * input_buffer,
             void * output_buffer,
             long nframes) {
    //std::cout << nframes << std::endl;
    LockFreeRingBuffer* recordBuffer = (LockFreeRingBuffer*)user;

    const float* in = static_cast<const float*>(input_buffer);
    if(recordBuffer->getStatusMute()) {
        recordBuffer->write(noSound, nframes);
    } else {
        recordBuffer->write(in, nframes);
    }
    //std::cout << "writeDataMicro" << nframes << std::endl;
    return nframes;
}

/*функция для работы с МОНО динамиками в cubeb*/
extern "C" long data_dinamic(cubeb_stream * stm, void * user,
             const void * input_buffer,  // Данные с микрофона
             void * output_buffer,       // Буфер для заполнения (динамики)
             long nframes) {             // Количество кадров для обработки
    //std::cout << nframes << std::endl;
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
    #ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    #endif

    std::cout << "hello world" << std::endl;

    uint32_t rate;
    uint32_t latency_frames;

    cubeb * app_ctx;
    
    std::cout << cubeb_init(&app_ctx, "Example Application", nullptr) << std::endl;

    std::cout << cubeb_get_preferred_sample_rate(app_ctx, &rate) << std::endl;

    cubeb_stream_params params;
    params.format = CUBEB_SAMPLE_FLOAT32NE;
    params.rate = SAMPLE_RATE;
    params.channels = 1;
    params.layout = CUBEB_LAYOUT_UNDEFINED;
    params.prefs = CUBEB_STREAM_PREF_NONE;
    std::cout << cubeb_get_min_latency(app_ctx, &params, &latency_frames) << std::endl;

    std::cout << latency_frames << std::endl;

    wolfSSL_Init();
    
    audioInput micro(app_ctx, SAMPLE_RATE, latency_frames);
    micro.startRecord();

    audioOut dinamic(app_ctx, SAMPLE_RATE, latency_frames);
    dinamic.startRead();
        
    AudioTransmission udpClient(micro.getBuffer(), dinamic.getBuffer());

    GUI gui("test", udpClient);

        //udpClient.addServerSert("server-cert.pem");
        //if(data.serverCertVerifi) udpClient.offCertVerify();

        //udpClient.startTransmission("127.0.0.1",15923,"0","0",0);

    getchar();

    wolfSSL_Cleanup();

    cubeb_destroy(app_ctx);

    return 0;
}
