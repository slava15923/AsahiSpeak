#pragma once
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstring>
#include <memory>

class LockFreeRingBuffer {
public:
    LockFreeRingBuffer(size_t capacity_frames)
        : capacity(capacity_frames), buffer(new float[capacity_frames]) {
        read_idx = 0;
        write_idx = 0;
        count = 0;
    }

    ~LockFreeRingBuffer() = default;

    // Записывает все frames, перезаписывая старые данные при заполнении.
    // Всегда возвращает frames (если frames > 0).
    size_t write(const float* src, size_t frames) {
        if (frames == 0) return 0;

        // Сначала копируем данные (без блокировки) – используем текущий write_idx
        size_t w = write_idx; // писатель один, можно читать без синхронизации
        size_t first_part = capacity - w;
        size_t copy1 = (frames < first_part) ? frames : first_part;
        memcpy(buffer.get() + w, src, copy1 * sizeof(float));
        if (frames > copy1) {
            memcpy(buffer.get(), src + copy1, (frames - copy1) * sizeof(float));
        }

        // Обновляем индексы под мьютексом
        std::lock_guard<std::mutex> lock(mutex);
        size_t new_w = (w + frames) % capacity;
        write_idx = new_w;

        size_t free_space = capacity - count;
        size_t overwrite = 0;
        if (frames > free_space) {
            overwrite = frames - free_space;
        }
        if (overwrite > 0) {
            read_idx = (read_idx + overwrite) % capacity;
            count = capacity;
        } else {
            count += frames;
        }

        cv.notify_one();
        return frames;
    }

    // Неблокирующее чтение – возвращает реально прочитанное количество (0 … frames)
    size_t read(float* dst, size_t frames) {
        if (frames == 0) return 0;

        size_t r, to_read;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (count == 0) return 0;
            to_read = (frames < count) ? frames : count;
            r = read_idx;
            read_idx = (r + to_read) % capacity;
            count -= to_read;
        }

        // Копируем данные вне мьютекса
        size_t first_part = capacity - r;
        size_t copy = (to_read < first_part) ? to_read : first_part;
        memcpy(dst, buffer.get() + r, copy * sizeof(float));
        if (to_read > copy) {
            memcpy(dst + copy, buffer.get(), (to_read - copy) * sizeof(float));
        }

        return to_read;
    }

    // Блокирующее чтение – ждёт, пока в буфере не накопится как минимум frames фреймов,
    // затем читает ровно frames (или меньше, если за время ожидания произошла перезапись).
    // Возвращает количество реально прочитанных фреймов (обычно frames).
    // Если таймаут истёк – возвращает 0 (ничего не читает).
    size_t readBlocking(float* dst, size_t frames, std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) {
        if (frames == 0) return 0;

        std::unique_lock<std::mutex> lock(mutex);
        bool ready = false;
        if (timeout == std::chrono::milliseconds::max()) {
            cv.wait(lock, [this, frames] { return count >= frames; });
            ready = true;
        } else {
            ready = cv.wait_for(lock, timeout, [this, frames] { return count >= frames; });
        }

        if (!ready) {
            return 0;
        }

        // После пробуждения у нас уже захвачен мьютекс
        size_t r = read_idx;
        size_t to_read = (frames < count) ? frames : count; // может быть меньше из-за перезаписи
        read_idx = (r + to_read) % capacity;
        count -= to_read;
        lock.unlock();

        // Копируем данные
        size_t first_part = capacity - r;
        size_t copy = (to_read < first_part) ? to_read : first_part;
        memcpy(dst, buffer.get() + r, copy * sizeof(float));
        if (to_read > copy) {
            memcpy(dst + copy, buffer.get(), (to_read - copy) * sizeof(float));
        }

        return to_read;
    }

    // Получить текущее количество доступных фреймов (без блокировки – но с мьютексом)
    size_t available() const {
        std::lock_guard<std::mutex> lock(mutex);
        return count;
    }

private:
    const size_t capacity;
    std::unique_ptr<float[]> buffer;
    size_t read_idx = 0;
    size_t write_idx = 0;
    size_t count = 0;

    mutable std::mutex mutex;
    std::condition_variable cv;
};