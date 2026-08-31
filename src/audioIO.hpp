#pragma once

#include <cubeb/cubeb.h>
#include "LockFreeRingBuffer.hpp"
#include "network.hpp"

class audioInput {
    private:
        LockFreeRingBuffer buffer;
        LockFreeRingBuffer noiseCancellation;
        cubeb_stream_params input_params;
        cubeb_stream * stm;
        uint32_t rate;
        uint32_t latency;
        cubeb* ctx;

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
                data_micro, state_cb, &buffer);

            if (err != CUBEB_OK) {
                // Например, выбросить исключение
                throw std::runtime_error("cubeb_stream_init failed");
            }
        }

        void startRecord() {cubeb_stream_start(stm);}

        void stop() {cubeb_stream_stop(stm);}

        LockFreeRingBuffer& getBuffer() { return buffer;}

        ~audioInput() {
            stop();
            cubeb_stream_destroy(stm);
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