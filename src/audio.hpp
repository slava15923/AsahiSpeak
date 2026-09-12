#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cassert>
#include <string>   // для std::string в setParam/getParam

// -------------------------------------------------------------------
// Базовый класс для всех алгоритмов шумоподавления
// -------------------------------------------------------------------
class AudioDenoiser {
public:
    virtual ~AudioDenoiser() = default;

    // Основной метод: обрабатывает входной буфер (указатель на float) 
    // и записывает результат в выходной буфер. Размер буферов – numSamples.
    virtual void process(const float* input, float* output, size_t numSamples) = 0;

    // Установка/получение параметров (опционально)
    virtual void setParam(const std::string& name, float value) {}
    virtual float getParam(const std::string& name) const { return 0.0f; }
};

// -------------------------------------------------------------------
// 1. Спектральное вычитание (упрощённая версия без БПФ – демонстрация)
//    В реальности требует оконного БПФ и обработки спектра.
// -------------------------------------------------------------------
class SpectralSubtractionDenoiser : public AudioDenoiser {
private:
    float noiseFloor_;      // оценка уровня шума (константа для простоты)
    float oversubFactor_;   // коэффициент перевычитания

public:
    SpectralSubtractionDenoiser(float noiseFloor = 0.01f, float oversub = 1.0f)
        : noiseFloor_(noiseFloor), oversubFactor_(oversub) {}

    void process(const float* input, float* output, size_t numSamples) override {
        // Для демонстрации: простейшее пороговое подавление (аналог спектрального вычитания)
        // В реальном алгоритме нужны: окна, БПФ, вычитание шума в спектре, обратное БПФ.
        for (size_t i = 0; i < numSamples; ++i) {
            float val = input[i];
            if (std::abs(val) < noiseFloor_) {
                output[i] = 0.0f;
            } else {
                float sign = (val >= 0) ? 1.0f : -1.0f;
                float reduced = std::abs(val) - oversubFactor_ * noiseFloor_;
                if (reduced < 0.0f) reduced = 0.0f;
                output[i] = sign * reduced;
            }
        }
    }

    void setParam(const std::string& name, float value) override {
        if (name == "noiseFloor") noiseFloor_ = value;
        else if (name == "oversubFactor") oversubFactor_ = value;
    }
};

// -------------------------------------------------------------------
// 2. Фильтр Винера (спектральный) – упрощённая временная версия
//    Использует локальную оценку мощности сигнала и шума.
// -------------------------------------------------------------------
class WienerFilterDenoiser : public AudioDenoiser {
private:
    float noisePowerEstimate_;   // оценка мощности шума
    float smoothingFactor_;      // для обновления оценки

public:
    WienerFilterDenoiser(float noisePower = 0.001f, float smoothing = 0.9f)
        : noisePowerEstimate_(noisePower), smoothingFactor_(smoothing) {}

    void process(const float* input, float* output, size_t numSamples) override {
        const size_t frameSize = 128; // размер окна для оценки локальной мощности
        for (size_t i = 0; i < numSamples; ++i) {
            // Вычисляем локальную мощность в окне вокруг i
            float localPower = 0.0f;
            size_t start = (i > frameSize/2) ? i - frameSize/2 : 0;
            size_t end = std::min(numSamples, i + frameSize/2);
            for (size_t j = start; j < end; ++j) {
                localPower += input[j] * input[j];
            }
            localPower /= static_cast<float>(end - start);

            // Обновляем оценку шума (например, по минимуму)
            if (localPower < noisePowerEstimate_) {
                noisePowerEstimate_ = smoothingFactor_ * noisePowerEstimate_ + (1 - smoothingFactor_) * localPower;
            }

            // Вычисляем коэффициент Винера: gain = SNR / (SNR + 1)
            float snr = (localPower - noisePowerEstimate_) / (noisePowerEstimate_ + 1e-9f);
            float gain = snr / (snr + 1.0f);
            if (gain < 0.0f) gain = 0.0f;
            output[i] = input[i] * gain;
        }
    }
};

// -------------------------------------------------------------------
// 3. Адаптивный LMS-фильтр (для демонстрации принципа)
//    Пытается предсказать текущий отсчёт по предыдущим.
//    Внутренние буферы хранятся в std::vector, но это деталь реализации.
// -------------------------------------------------------------------
class AdaptiveLMSDenoiser : public AudioDenoiser {
private:
    std::vector<float> weights_;    // весовые коэффициенты
    std::vector<float> buffer_;     // буфер предыдущих отсчётов
    float mu_;                      // шаг адаптации
    size_t filterOrder_;            // порядок фильтра

public:
    AdaptiveLMSDenoiser(size_t order = 8, float mu = 0.01f)
        : filterOrder_(order), mu_(mu) {
        weights_.assign(order, 0.0f);
        buffer_.assign(order, 0.0f);
    }

    void process(const float* input, float* output, size_t numSamples) override {
        for (size_t n = 0; n < numSamples; ++n) {
            // Сдвиг буфера
            for (size_t i = filterOrder_ - 1; i > 0; --i) {
                buffer_[i] = buffer_[i - 1];
            }
            buffer_[0] = input[n];

            // Вычисляем выход фильтра (предсказание)
            float y = 0.0f;
            for (size_t i = 0; i < filterOrder_; ++i) {
                y += weights_[i] * buffer_[i];
            }

            // Ошибка = вход - предсказание (предполагаем, что это чистый сигнал)
            float error = input[n] - y;
            output[n] = error;

            // Обновление весов по LMS
            for (size_t i = 0; i < filterOrder_; ++i) {
                weights_[i] += 2.0f * mu_ * error * buffer_[i];
                weights_[i] = std::clamp(weights_[i], -1.0f, 1.0f); // ограничение для устойчивости
            }
        }
    }
};

class DoubleBuffer {
public:
    explicit DoubleBuffer(size_t size);
    ~DoubleBuffer() = default;

    // Получить указатель на текущий активный буфер для записи (без блокировки)
    float* getWriteBuffer() noexcept;

    // Переключить буферы: возвращает указатель на старый активный (с данными),
    // активным становится бывший свободный (обнулённый). Вызывается под мьютексом.
    float* swap();

    // Вернуть буфер в пул свободных (после обнуления). Вызывается под мьютексом.
    void releaseBuffer(float* buffer);

    // Обнулить оба буфера (для инициализации)
    void clear();

private:
    std::unique_ptr<float[]> bufA;
    std::unique_ptr<float[]> bufB;
    std::atomic<float*> active;   // текущий буфер для записи
    float* free;                  // свободный буфер (обнулён, защищён мьютексом)
    std::mutex mtx;               // защищает переключение и возврат
    size_t size;
};

DoubleBuffer::DoubleBuffer(size_t size) : size(size) {
    bufA = std::make_unique<float[]>(size);
    bufB = std::make_unique<float[]>(size);
    clear();
    active = bufA.get();
    free = bufB.get();
}

float* DoubleBuffer::getWriteBuffer() noexcept {
    return active.load(std::memory_order_acquire);
}

float* DoubleBuffer::swap() {
    std::lock_guard<std::mutex> lock(mtx);
    float* oldActive = active.load(std::memory_order_relaxed);
    // Меняем местами: активным становится свободный, свободным – старый активный
    active.store(free, std::memory_order_release);
    free = oldActive;
    // Теперь free указывает на старый активный (который нужно обнулить после использования)
    // Возвращаем oldActive для записи в readBuffer
    return oldActive;
}

void DoubleBuffer::releaseBuffer(float* buffer) {
    std::lock_guard<std::mutex> lock(mtx);
    // Предполагается, что buffer уже обнулён вызывающим
    free = buffer;
}

void DoubleBuffer::clear() {
    std::fill(bufA.get(), bufA.get() + size, 0.0f);
    std::fill(bufB.get(), bufB.get() + size, 0.0f);
}