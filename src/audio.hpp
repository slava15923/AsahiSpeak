#pragma once
#include <cmath>
#include <algorithm>
#include <atomic>

class NoiseGate {
public:
    // threshold - порог амплитуды (0..1), ниже которого звук глушится
    // attackTime - время открытия в секундах (обычно 0.001–0.01)
    // releaseTime - время закрытия в секундах (обычно 0.05–0.2)
    // sampleRate - частота дискретизации
    void init(float threshold, float attackTime, float releaseTime, float sampleRate) {
        threshold_ = threshold;
        attackCoeff_ = std::exp(-1.0 / (attackTime * sampleRate));
        releaseCoeff_ = std::exp(-1.0 / (releaseTime * sampleRate));
        currentGain_ = 0.0f; // начинаем с закрытого состояния
    }

    // Обрабатывает буфер float (значения в диапазоне -1..1)
    void processBlock(float* buffer, int numSamples) {
        // 1. Вычисляем пиковый уровень в кадре (максимальное абсолютное значение)
        float peak = 0.0f;
        for (int i = 0; i < numSamples; ++i) {
            float absVal = std::fabs(buffer[i]);
            if (absVal > peak) peak = absVal;
        }

        // 2. Определяем целевой коэффициент усиления (0 или 1)
        float targetGain = (peak > threshold_) ? 1.0f : 0.0f;

        // 3. Плавно изменяем текущий коэффициент усиления
        if (targetGain > currentGain_) {
            // Атака (открытие) – быстро
            currentGain_ = targetGain + (currentGain_ - targetGain) * attackCoeff_;
        } else {
            // Релиз (закрытие) – медленно
            currentGain_ = targetGain + (currentGain_ - targetGain) * releaseCoeff_;
        }

        // Небольшая страховка от выхода за пределы
        currentGain_ = std::clamp(currentGain_, 0.0f, 1.0f);

        // 4. Применяем усиление к буферу
        for (int i = 0; i < numSamples; ++i) {
            buffer[i] *= currentGain_;
        }
    }

private:
    std::atomic<float> threshold_ = 0.01f;
    std::atomic<float> attackCoeff_ = 0.0f;
    std::atomic<float> releaseCoeff_ = 0.0f;
    float currentGain_ = 0.0f;
};

/*
// Создаём объект и настраиваем
NoiseGate gate;
gate.init(
    0.005f,    // порог (очень тихий уровень, подберите под свой сигнал)
    0.002f,    // время атаки 2 мс
    0.1f,      // время релиза 100 мс
    44100.0f   // частота дискретизации
);

// В цикле обработки кадра (882 сэмпла)
float pcmBuffer[882];
// ... заполняем буфер аудиоданными ...

// Применяем шумовой затвор (все тихие участки обнулятся)
gate.processBlock(pcmBuffer, 882);
*/