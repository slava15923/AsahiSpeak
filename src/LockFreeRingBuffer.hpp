#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstring>
#include "audio.hpp"

class LockFreeRingBuffer {
public:
    LockFreeRingBuffer(size_t capacity_frames)
        : capacity(capacity_frames), buffer(new float[capacity_frames]) {
        read_idx.store(0, std::memory_order_relaxed);
        write_idx.store(0, std::memory_order_relaxed);
        count.store(0, std::memory_order_relaxed);

        filter.setBandPass(300, 2000, 44100);
        gain.setGain(1.5, 0); // увеличить громкость на 50% без плавности
        gate.init(0.005f, 0.001f, 0.05f, 44100.0f);

    }

    ~LockFreeRingBuffer() = default;

    // Записывает все frames, перезаписывая старые данные при заполнении.
    // Всегда возвращает frames (если frames > 0).
    size_t write(const float* src, size_t frames) {
        if (frames == 0) return 0;

        size_t w = write_idx.load(std::memory_order_relaxed);
        size_t r = read_idx.load(std::memory_order_acquire);
        size_t c = count.load(std::memory_order_acquire);

        size_t free_space = capacity - c;
        size_t overwrite = 0;
        if (frames > free_space) {
            overwrite = frames - free_space;
        }

        // Копируем данные в буфер
        size_t first_part = capacity - w;
        size_t copy1 = (frames < first_part) ? frames : first_part;
        memcpy(buffer.get() + w, src, copy1 * sizeof(float));
        if (frames > copy1) {
            memcpy(buffer.get(), src + copy1, (frames - copy1) * sizeof(float));
        }

        // Обновляем write_idx
        size_t new_w = (w + frames) % capacity;
        write_idx.store(new_w, std::memory_order_release);

        if (overwrite > 0) {
            // Сдвигаем read_idx вперёд на количество перезаписанных фреймов
            size_t new_r = (r + overwrite) % capacity;
            read_idx.store(new_r, std::memory_order_release);
            count.store(capacity, std::memory_order_release);
        } else {
            count.store(c + frames, std::memory_order_release);
        }
        //if(isNoiseGate) {
        //    noisegate.processBlock(buffer.get(), frames);
        //}
        // Уведомляем ожидающий поток, что появились новые данные

        //filter.processBlock(buffer.get(), frames);   // обрезаем частоты
        //gate.processBlock(buffer.get(), frames);
        //gain.processBlock(buffer.get(), frames);     // усиливаем
        

        cv.notify_one();

        return frames;
    }

    // Неблокирующее чтение – возвращает реально прочитанное количество (0 … frames)
    size_t read(float* dst, size_t frames) {
        if (frames == 0) return 0;

        size_t r = read_idx.load(std::memory_order_relaxed);
        size_t c = count.load(std::memory_order_acquire);
        size_t available = c;
        size_t to_read = (frames < available) ? frames : available;
        if (to_read == 0) return 0;

        size_t first_part = capacity - r;
        size_t copy = (to_read < first_part) ? to_read : first_part;
        memcpy(dst, buffer.get() + r, copy * sizeof(float));
        if (to_read > copy) {
            memcpy(dst + copy, buffer.get(), (to_read - copy) * sizeof(float));
        }

        size_t new_r = (r + to_read) % capacity;
        read_idx.store(new_r, std::memory_order_release);
        count.store(c - to_read, std::memory_order_release);

        return to_read;
    }

    // Блокирующее чтение – ждёт, пока в буфере не накопится как минимум frames фреймов,
    // затем читает ровно frames (или меньше, если за время ожидания произошла перезапись).
    // Возвращает количество реально прочитанных фреймов (обычно frames).
    // Если таймаут истёк – возвращает 0 (ничего не читает).
    size_t readBlocking(float* dst, size_t frames, std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
        if (frames == 0) return 0;

        std::unique_lock<std::mutex> lock(cv_mutex);
        bool ready = false;
        if (timeout == std::chrono::milliseconds::max()) {
            cv.wait(lock, [this, frames] {
                return count.load(std::memory_order_acquire) >= frames;
            });
            ready = true;
        } else {
            ready = cv.wait_for(lock, timeout, [this, frames] {
                return count.load(std::memory_order_acquire) >= frames;
            });
        }

        if (!ready) {
            return 0; // таймаут – не читаем
        }

        // Теперь count >= frames, но за время ожидания могла произойти перезапись,
        // поэтому всё равно используем обычное read (оно прочитает сколько сможет).
        // Обычно read вернёт frames, если только писатель не перезаписал данные,
        // которые мы собирались читать – тогда вернёт меньше.
        return read(dst, frames);
    }

    // Опционально: получить текущее количество доступных фреймов (без блокировки)
    size_t available() const {
        return count.load(std::memory_order_acquire);
    }

    // threshold - порог амплитуды (0..1), ниже которого звук глушится
    // attackTime - время открытия в секундах (обычно 0.001–0.01)
    // releaseTime - время закрытия в секундах (обычно 0.05–0.2)
    // sampleRate - частота дискретизации
    void onNoiseGate(float threshold, float attackTime, float releaseTime, float sampleRate) {
        //noisegate.init(threshold, attackTime, releaseTime, sampleRate);
        //isNoiseGate = true;
    }


private:
    const size_t capacity;
    std::unique_ptr<float[]> buffer;
    std::atomic<size_t> read_idx;
    std::atomic<size_t> write_idx;
    std::atomic<size_t> count; // текущее число фреймов в буфере

    std::mutex cv_mutex;
    std::condition_variable cv;
    BiquadFilter filter;
    GainControl gain;
    NoiseGate gate;

};
