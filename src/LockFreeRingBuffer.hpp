#pragma once

#include "readerwriterqueue.h"   // cameron314/readerwriterqueue

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <semaphore>
#include <condition_variable>

class LockFreeRingBuffer {
public:
    explicit LockFreeRingBuffer(size_t capacity_frames)
        : capacity(capacity_frames),
          queue(capacity_frames) {}

    ~LockFreeRingBuffer() = default;
    LockFreeRingBuffer(const LockFreeRingBuffer&) = delete;
    LockFreeRingBuffer& operator=(const LockFreeRingBuffer&) = delete;

    // ============================================================
    //  ЧТЕНИЕ (cubeb callback, RT-поток)
    //  Lock-free; sem_post без блокировки.
    // ============================================================
    size_t read(float* dst, size_t frames) noexcept {
        if (frames == 0) return 0;

        size_t n = 0;
        float sample;
        while (n < frames && queue.try_dequeue(sample)) {
            dst[n++] = sample;
        }

        if (n > 0) {
            // RT-safe сигнал писателю: освободилось место
            spaceReady.release();
        }
        return n;
    }

    // ============================================================
    //  ЗАПИСЬ БЕЗ ПЕРЕЗАПИСИ (микшер)
    //  Возвращает 0, если места нет. Всё-или-ничего.
    // ============================================================
    size_t writeNoOverwrite(const float* src, size_t frames) noexcept {
        if (frames == 0) return 0;
        if (frames > capacity) return 0;

        // Точная проверка места (SPSC + release/acquire в очереди => точно)
        const size_t sz = queue.size_approx();
        if (sz + frames > capacity) return 0;

        for (size_t i = 0; i < frames; ++i) {
            // При корректном size_approx не должно упасть;
            // подстраховка от рассинхронизации на weak-memory.
            if (!queue.try_enqueue(src[i])) {
                if (i > 0) dataReady.release();
                return i;
            }
        }

        dataReady.release();
        return frames;
    }

    // ============================================================
    //  ЗАПИСЬ С ПЕРЕЗАПИСЬЮ (микрофон, RT-поток)
    // ============================================================
    size_t write(const float* src, size_t frames) noexcept {
        if (frames == 0) return 0;
        if (frames > capacity) {
            src += (frames - capacity);
            frames = capacity;
        }

        size_t written = 0;
        for (size_t i = 0; i < frames; ++i) {
            if (queue.try_enqueue(src[i])) ++written;
            else break;
        }
        if (written > 0) dataReady.release();
        return written;
}

    // ============================================================
    //  ОЖИДАНИЕ МЕСТА ДЛЯ МИКШЕРА
    // ============================================================
    bool waitForSpace(size_t frames,
                      const std::atomic<bool>* stopFlag = nullptr,
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(50)) {
        if (frames == 0) return true;
        if (frames > capacity) return false;

        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (true) {
            if (stopFlag && stopFlag->load()) return false;

            const size_t sz = queue.size_approx();
            if (capacity - sz >= frames) return true;

            if (timeout == std::chrono::milliseconds::max()) {
                spaceReady.acquire();
            } else {
                const auto now = std::chrono::steady_clock::now();
                if (now >= deadline) return false;
                if (!spaceReady.try_acquire_for(deadline - now)) return true;
            }
        }
    }

    // ============================================================
    //  БЛОКИРУЮЩЕЕ ЧТЕНИЕ (writeData из recordBuffer)
    // ============================================================
    size_t readBlocking(float* dst, size_t frames,
                        std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        if (frames == 0) return 0;

        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (true) {
            if (queue.size_approx() >= frames) break;

            if (timeout == std::chrono::milliseconds::max()) {
                dataReady.acquire();
            } else {
                const auto now = std::chrono::steady_clock::now();
                if (now >= deadline) return 0;
                if (!dataReady.try_acquire_for(deadline - now)) return 0;
            }
        }

        // size >= frames => read вернёт ровно frames
        return read(dst, frames);
    }

    // ============================================================
    //  ВСПОМОГАТЕЛЬНОЕ
    // ============================================================
    size_t available() const noexcept {
        const size_t sz = queue.size_approx();
        return (sz > capacity) ? capacity : sz;
    }

    size_t freeSpace() const noexcept { return capacity - available(); }
    size_t getCapacity() const noexcept { return capacity; }

    // Разбудить всех, кто ждёт (при остановке)
    void notifyAll() {
        dataReady.release();
        spaceReady.release();
    }

    // Очистить буфер (можно звать из не-RT контекста)
    void reset() {
        float dummy;
        while (queue.try_dequeue(dummy)) {}
        // Бинарные семафоры сбрасывать не нужно — «лишний» токен
        // просто заставит ожидающего проснуться и перечитать size_approx().
    }

private:
    const size_t capacity;
    moodycamel::ReaderWriterQueue<float> queue;

    // Бинарные семафоры: не накапливают счётчики, только «есть событие/нет».
    // Актуальное состояние всегда проверяется через size_approx().
    std::binary_semaphore dataReady {0};
    std::binary_semaphore spaceReady{0};
};