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


#include <algorithm>
#include <cmath>

class GainControl {
public:
    // Установить целевой коэффициент усиления (например, 1.0 = без изменений, 0.5 = тише, 2.0 = громче)
    void setGain(double targetGain, int rampSamples = 0) {
        targetGain_ = targetGain;
        rampSamples_ = rampSamples;
        if (rampSamples <= 0) {
            currentGain_ = targetGain;
            step_ = 0.0;
        } else {
            step_ = (targetGain - currentGain_) / rampSamples;
        }
    }

    // Обработка буфера (изменяет сэмплы на месте)
    void processBlock(float* buffer, int numSamples) {
        for (int i = 0; i < numSamples; ++i) {
            // Если есть шаг плавного изменения
            if (step_ != 0.0) {
                currentGain_ += step_;
                // Корректировка, чтобы не перескочить
                if (std::abs(step_) > 0 && 
                    ((step_ > 0 && currentGain_ >= targetGain_) || 
                     (step_ < 0 && currentGain_ <= targetGain_))) {
                    currentGain_ = targetGain_;
                    step_ = 0.0;
                }
            }
            // Применяем усиление и обрезаем пики (предотвращаем клиппинг)
            float val = buffer[i] * currentGain_;
            buffer[i] = std::clamp(val, -1.0f, 1.0f); // если ваш диапазон -1..1
        }
    }

private:
    double currentGain_ = 1.0;
    double targetGain_ = 1.0;
    double step_ = 0.0;
    int rampSamples_ = 0;
};


#include <cmath>
#include <vector>

class BiquadFilter {
public:
    void setBandPass(double freqLow, double freqHigh, double sampleRate) {
        double wLow  = 2.0 * M_PI * freqLow  / sampleRate;
        double wHigh = 2.0 * M_PI * freqHigh / sampleRate;
        double cosLow  = std::cos(wLow);
        double cosHigh = std::cos(wHigh);
        double alphaLow  = std::sin(wLow)  / (2.0 * 0.707); // Q=0.707 (Баттерворт)
        double alphaHigh = std::sin(wHigh) / (2.0 * 0.707);

        // Коэффициенты для ФНЧ (нижняя граница)
        double b0_lp = (1.0 - cosLow) / 2.0;
        double b1_lp = 1.0 - cosLow;
        double b2_lp = (1.0 - cosLow) / 2.0;
        double a0_lp = 1.0 + alphaLow;
        double a1_lp = -2.0 * cosLow;
        double a2_lp = 1.0 - alphaLow;

        // Коэффициенты для ФВЧ (верхняя граница)
        double b0_hp = (1.0 + cosHigh) / 2.0;
        double b1_hp = -(1.0 + cosHigh);
        double b2_hp = (1.0 + cosHigh) / 2.0;
        double a0_hp = 1.0 + alphaHigh;
        double a1_hp = -2.0 * cosHigh;
        double a2_hp = 1.0 - alphaHigh;

        // Объединяем в один полосовой фильтр (каскад ФНЧ+ФВЧ)
        // Умножаем полиномы, получаем коэффициенты для одного биквадрата
        // (или проще использовать два последовательных фильтра, что тоже быстро)
        // Для простоты оставим каскад – два биквадрата подряд.
        // Ниже инициализируем два фильтра.
        lp_.setCoefficients(b0_lp/a0_lp, b1_lp/a0_lp, b2_lp/a0_lp, a1_lp/a0_lp, a2_lp/a0_lp);
        hp_.setCoefficients(b0_hp/a0_hp, b1_hp/a0_hp, b2_hp/a0_hp, a1_hp/a0_hp, a2_hp/a0_hp);
    }

    double process(double input) {
        // Сначала ФНЧ, затем ФВЧ (или наоборот – порядок не важен)
        return hp_.process(lp_.process(input));
    }

    void processBlock(float* buffer, int numSamples) {
        for (int i = 0; i < numSamples; ++i) {
            buffer[i] = static_cast<float>(process(static_cast<double>(buffer[i])));
        }
    }

private:
    struct Biquad {
        double b0=1, b1=0, b2=0, a1=0, a2=0;
        double x1=0, x2=0, y1=0, y2=0;

        void setCoefficients(double b0_, double b1_, double b2_, double a1_, double a2_) {
            b0=b0_; b1=b1_; b2=b2_; a1=a1_; a2=a2_;
        }

        double process(double x) {
            double y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
            x2 = x1; x1 = x;
            y2 = y1; y1 = y;
            return y;
        }
    };

    Biquad lp_, hp_;
};