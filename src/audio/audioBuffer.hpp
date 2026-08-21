#pragma once

#ifndef asahiAudioDefine

#define asahiAudioDefine

#include <cubeb/cubeb.h>
#include <baudvine/ringbuf.h>


#define MONO 1
#define STEREO 2

#include <memory.h>


    class audioBuffer {
        private:
            uint32_t sizeBuff_; //длина буффера в байтах
            float* buff_;
            long nframes_ = -15923;
            uint8_t type_ = 0;//0 - не назначен; 1 - mono; 2 - stereo;

        public:
            //возвращает размер буффера в байтах
            inline const uint32_t getBuffSize() const noexcept {
                return sizeBuff_;
            }

            /*
            эта функция КОПИРУЕТ БУФФЕР в себя
            buff_ - буффер
            */
            inline void setBuff(float* buffer) noexcept {
                memcpy(buff_, buffer, sizeBuff_);

            }

            /*
            type_ ---- 0 не назначен; - 1 - mono; 2 - stereo;
            */
            inline const uint8_t getType() const noexcept {
                if (type_ == 0) {
                    fprintf(stderr, "буффер пустой\n");
                    //exit(1);
                }
                return type_;
            }

            inline const long getNframes() const noexcept {
                return nframes_;
            }


            /*
            вернёт буффер
            */
            inline const float* getBuff() const noexcept {
                if (type_ == 0) {
                    fprintf(stderr, "буффер пустой\n");
                    exit(1);
                }
                return buff_;
            }
            /*
            long nframes_ = количество фреймов
            type_ = 1 - mono; 2 - stereo;
            */
            audioBuffer(const long& nframes, uint32_t& type) noexcept {
                type_ = type;
                nframes_ = nframes;
                buff_ = new float[nframes*type];
                sizeBuff_ = nframes*type*sizeof(float);//выделяется буффер
            }
            ~audioBuffer() {
                delete buff_;
            }
    };

#endif