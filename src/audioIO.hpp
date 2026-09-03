#pragma once

#include <cubeb/cubeb.h>
#include "LockFreeRingBuffer.hpp"
#include "network.hpp"
#include <filesystem>
#include <audio.hpp>
#include <renamenoise.h>
#include <array>

class RNNoiseDenoiser {
public:
    RNNoiseDenoiser() : state_(renamenoise_create(NULL), renamenoise_destroy) {}

    void processFrame(const float* input, float* output) {
        float in_scaled[960];
        for (size_t i = 0; i < FRAME_SIZE_; ++i) {
            in_scaled[i] = input[i] * PCM_SCALE;
        }
        renamenoise_process_frame(state_.get(), output, in_scaled);
        // Выходной буфер теперь содержит значения в диапазоне [-32768, 32767]
        // Если нужно вернуть [-1, 1], поделите:
        for (size_t i = 0; i < FRAME_SIZE_; ++i) {
            output[i] /= PCM_SCALE;
        }
    }

private:
    // 使用智能指针管理资源
    unsigned int PCM_SCALE = 32768;
    int FRAME_SIZE_ = 480;
    std::unique_ptr<ReNameNoiseDenoiseState, decltype(&renamenoise_destroy)> state_;
};




class audioInput {
    private:
        LockFreeRingBuffer buffer;
        LockFreeRingBuffer noiseCancellation;
        cubeb_stream_params input_params;
        cubeb_stream * stm;
        uint32_t rate;
        uint32_t latency;
        cubeb* ctx;
        std::thread noiseThread;

        /*функция для работы с МОНО микрофоном в cubeb*/
        static long data_micro(cubeb_stream * stm, void * user,
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
        static void state_cb(cubeb_stream *stream, void *user_ptr, cubeb_state state) {
            printf("Состояние потока изменилось: %d\n", state);
            //return CUBEB_OK;
        }

        void audio_processing_worker_thread() {
            RNNoiseDenoiser noise;
            const int frame_size = 480;
            auto temp1 = std::make_unique<float[]>(frame_size);
            auto temp2 = std::make_unique<float[]>(frame_size);

            while(true) {
                if(noiseCancellation.readBlocking(temp1.get(),frame_size) == frame_size) {
                    noise.processFrame(temp1.get(), temp2.get());
                    buffer.write(temp2.get(), frame_size);
                }
            }
        }

    public:
        audioInput(cubeb* ctx_, const uint32_t& rate_, const uint32_t& latency_) : ctx(ctx_), rate(rate_), latency(latency_),
        buffer(RING_SIZE), noiseCancellation(RING_SIZE) {
            input_params.format = CUBEB_SAMPLE_FLOAT32NE;
            input_params.rate = rate;
            input_params.channels = 1;                  
            input_params.layout = CUBEB_LAYOUT_UNDEFINED;
            input_params.prefs = CUBEB_STREAM_PREF_NONE;

            int err = cubeb_stream_init(ctx, &stm, "Test", NULL, 
                &input_params, NULL, NULL, latency, 
                data_micro, state_cb, &noiseCancellation);

            if (err != CUBEB_OK) {
                throw std::runtime_error("cubeb_stream_init failed");
            }
        }

        void startRecord() {
            cubeb_stream_start(stm);
            noiseThread = std::thread(&audioInput::audio_processing_worker_thread,this);
        }

        void stop() {cubeb_stream_stop(stm);}

        LockFreeRingBuffer& getBuffer() { return buffer;}

        ~audioInput() {
            stop();
            cubeb_stream_destroy(stm);
            //renamenoise_destroy(st);
        }
};


class audioOut {
    private:
        LockFreeRingBuffer buffer;
        cubeb_stream_params params;
        cubeb_stream * stm;
        uint32_t rate;
        uint32_t latency;
        cubeb* ctx;

        /*функция для работы с МОНО микрофоном в cubeb*/
        static long data_dinamic(cubeb_stream * stm, void * user,
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

        static void state_cb(cubeb_stream *stream, void *user_ptr, cubeb_state state) {
            printf("Состояние потока изменилось: %d\n", state);
            //return CUBEB_OK;
        }

    public:
        audioOut(cubeb* ctx_, const uint32_t& rate_, const uint32_t& latency_) : ctx(ctx_), rate(rate_), latency(latency_),
        buffer(RING_SIZE) {
        params.format = CUBEB_SAMPLE_FLOAT32NE; 
        params.rate = rate;                    
        params.channels = 1;                  
        params.layout = CUBEB_LAYOUT_UNDEFINED;
        params.prefs = CUBEB_STREAM_PREF_NONE; 

            int err = cubeb_stream_init(ctx, &stm, "Test", NULL, 
                NULL, NULL, &params, latency, 
                data_dinamic, state_cb, &buffer);

            if (err != CUBEB_OK) {
                // Например, выбросить исключение
                throw std::runtime_error("cubeb_stream_init failed");
            }
        }

        void startRead() {cubeb_stream_start(stm);}

        void stop() {cubeb_stream_stop(stm);}

        LockFreeRingBuffer& getBuffer() { return buffer;}

        ~audioOut() {
            stop();
            cubeb_stream_destroy(stm);
        }
};