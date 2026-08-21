#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <atomic>

#include <SFML/Audio.hpp>
#include <baudvine/ringbuf.h>
#include <cubeb/cubeb.h>
#include <string.h>
#include "audio/audioBuffer.hpp"
#include <cubeb_ringbuffer.h>

#include <cstdarg>
#include <cstdio>

const int SAMPLE_RATE = 44100;

// Глобальный кольцевой буфер (хранит float, моно)
//static cubeb_ringbuffer* g_ring = nullptr;
// Размер буфера: например, 5 секунд моно-данных
const size_t RING_SIZE = SAMPLE_RATE * 5;  // 220500 фреймов

class LockFreeRingBuffer {
public:
    LockFreeRingBuffer(size_t capacity_frames)
        : capacity(capacity_frames), buffer(new float[capacity_frames]) {
        read_idx.store(0, std::memory_order_relaxed);
        write_idx.store(0, std::memory_order_relaxed);
    }
    ~LockFreeRingBuffer() = default;

    // Возвращает количество реально записанных фреймов
    size_t write(const float* src, size_t frames) {
        size_t w = write_idx.load(std::memory_order_relaxed);
        size_t r = read_idx.load(std::memory_order_acquire);
        size_t used = (w >= r) ? (w - r) : (capacity - r + w);
        size_t free_space = capacity - used;
        size_t to_write = (frames < free_space) ? frames : free_space;
        if (to_write == 0) return 0;

        size_t first_part = capacity - w;
        size_t copy = (to_write < first_part) ? to_write : first_part;
        memcpy(buffer.get() + w, src, copy * sizeof(float));
        if (to_write > copy) {
            memcpy(buffer.get(), src + copy, (to_write - copy) * sizeof(float));
        }
        write_idx.store((w + to_write) % capacity, std::memory_order_release);
        return to_write;
    }

    // Возвращает количество реально прочитанных фреймов
    size_t read(float* dst, size_t frames) {
        size_t r = read_idx.load(std::memory_order_relaxed);
        size_t w = write_idx.load(std::memory_order_acquire);
        size_t available = (w >= r) ? (w - r) : (capacity - r + w);
        size_t to_read = (frames < available) ? frames : available;
        if (to_read == 0) return 0;

        size_t first_part = capacity - r;
        size_t copy = (to_read < first_part) ? to_read : first_part;
        memcpy(dst, buffer.get() + r, copy * sizeof(float));
        if (to_read > copy) {
            memcpy(dst + copy, buffer.get(), (to_read - copy) * sizeof(float));
        }
        read_idx.store((r + to_read) % capacity, std::memory_order_release);
        return to_read;
    }

private:
    const size_t capacity;
    std::unique_ptr<float[]> buffer;
    std::atomic<size_t> read_idx;
    std::atomic<size_t> write_idx;
};

baudvine::RingBuf<float, 1200*1*8> recordBuffer;


extern "C" void state_cb(cubeb_stream *stream, void *user_ptr, cubeb_state state) {
    printf("Состояние потока изменилось: %d\n", state);
    //return CUBEB_OK;
}

extern "C" long data_fullduplex(cubeb_stream * stm, void * user,
             const void * input_buffer,  // Данные с микрофона
             void * output_buffer,       // Буфер для заполнения (динамики)
             long nframes) {             // Количество кадров для обработки

    // input_buffer и output_buffer — это массивы float (или short).
    // В примере мы просто копируем вход в выход (эффект "эхо" с нулевой задержкой).
    const float * in = (const float*)input_buffer;
    float * out = (float*)output_buffer;
    for (int i = 0; i < nframes; ++i) {
        for (int c = 0; c < 2; ++c) { // Для стерео
            // Копируем моно-вход в оба канала стерео-выхода
            out[i * 2 + c] = in[i];
        }
    }
    return nframes; // Возвращаем количество обработанных кадров
}


extern "C" long data_micro(cubeb_stream * stm, void * user,
             const void * input_buffer,  // Данные с микрофона
             void * output_buffer,       // Буфер для заполнения (динамики)
             long nframes) {             // Количество кадров для обработки
    //std::cout << nframes << std::endl;


    for(int i = 0; i < nframes; i++) recordBuffer.push_back(((const float*)input_buffer)[i]);
    return nframes; // Возвращаем количество обработанных кадров
}

extern "C" long data_dinamic(cubeb_stream * stm, void * user,
             const void * input_buffer,  // Данные с микрофона
             void * output_buffer,       // Буфер для заполнения (динамики)
             long nframes) {             // Количество кадров для обработки


    for(int i = 0; i < nframes; i++) {
        //
        //recordBuffer.push_back();
        while (!recordBuffer.empty()) {}
        const float& a = recordBuffer.front();
        std::cout << a << std::endl;
        ((float*)output_buffer)[2*i] = a;
        ((float*)output_buffer)[2*i + 1] = a;
        recordBuffer.pop_front();
    }
    return nframes; // Возвращаем количество обработанных кадров
}




extern "C" void cubebCallback(const char *fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    fprintf(stderr, "Cubeb Log: %s\n", buffer);
}

int main() {
    //g_ring = cubeb_ringbuffer_create(4096 * 2, sizeof(float)); // 4096 фреймов, запас
    std::cout << "hello world" << std::endl;

    uint32_t rate;
    uint32_t latency_frames;

    cubeb * app_ctx;
    cubeb_stream * stm = nullptr;

    cubeb_stream * stm_micro = nullptr;
    cubeb_stream * stm_dinamic = nullptr;

    //std::cout << cubeb_set_log_callback(CUBEB_LOG_VERBOSE, cubebCallback) << std::endl;

    
    std::cout << cubeb_init(&app_ctx, "Example Application", nullptr) << std::endl;

    std::cout << cubeb_get_preferred_sample_rate(app_ctx, &rate) << std::endl;

    

    // Параметры ВЫХОДА (динамики)
    cubeb_stream_params output_params;
    output_params.format = CUBEB_SAMPLE_FLOAT32NE; // Используем 32-битный float[reference:13]
    output_params.rate = rate;                    // Устанавливаем полученную частоту[reference:14]
    output_params.channels = 2;                   // Стерео[reference:15]
    output_params.layout = CUBEB_LAYOUT_UNDEFINED;// Не указываем строгую схему каналов[reference:16]
    output_params.prefs = CUBEB_STREAM_PREF_NONE; // Без дополнительных предпочтений[reference:17]

    // Параметры ВХОДА (микрофон)
    cubeb_stream_params input_params;
    input_params.format = CUBEB_SAMPLE_FLOAT32NE;
    input_params.rate = rate;
    input_params.channels = 1;                   // Моно[reference:18]
    input_params.layout = CUBEB_LAYOUT_UNDEFINED;
    input_params.prefs = CUBEB_STREAM_PREF_NONE;

    std::cout << cubeb_get_min_latency(app_ctx, &output_params, &latency_frames) << std::endl;

    std::cout << latency_frames << std::endl;

    //cubeb_stream_init(app_ctx, &stm, "Test", NULL, &input_params, NULL, &output_params, latency_frames, data_fullduplex, state_cb, NULL);

    //cubeb_stream_start(stm);

    cubeb_stream_init(app_ctx, &stm_dinamic, "Test", NULL, 
        NULL, NULL, &output_params, latency_frames, 
        data_dinamic, state_cb, NULL);

    cubeb_stream_init(app_ctx, &stm_micro, "Test", NULL, 
        &input_params, NULL, NULL, latency_frames, 
        data_micro, state_cb, NULL);

    cubeb_stream_start(stm_micro);
    cubeb_stream_start(stm_dinamic);



    //while (true) { std::this_thread::sleep_for(std::chrono::seconds(100));}

    getchar();

    //cubeb_stream_stop(stm);
    cubeb_stream_stop(stm_micro);
    cubeb_stream_stop(stm_dinamic);

    //cubeb_stream_destroy(stm);
    cubeb_stream_destroy(stm_micro);
    cubeb_stream_destroy(stm_dinamic);

    cubeb_destroy(app_ctx);

    return 0;
}