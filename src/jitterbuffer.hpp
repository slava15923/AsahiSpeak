#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>
#include "readerwriterqueue.h"   // moodycamel

template <typename T, size_t FRAME, size_t QUEUE_CAP = 32>
class ClientJitterBuffer {
public:
    using Frame = std::array<T, FRAME>;

    ClientJitterBuffer(size_t targetDepth, size_t maxDepth)
        : queue_(QUEUE_CAP),
          targetDepth_(targetDepth), maxDepth_(maxDepth),
          pending_(maxDepth), pendingSet_(maxDepth, false) {}

    // ============ ПРОИЗВОДИТЕЛЬ: сетевой поток ============
    // Здесь можно всё: сортировка, отсев, никаких ограничений RT.
    bool Push(uint32_t seq, const Frame& frame) {
        if (!primed_) {
            nextExpected_ = seq;
            primed_ = true;
        }

        const int32_t diff = static_cast<int32_t>(seq - nextExpected_);
        if (diff < 0)                        { ++lateDrops_; return false; }  // опоздал
        if (static_cast<size_t>(diff) >= maxDepth_) { ++lateDrops_; return false; }

        // Кладём в reorder-буфер по слоту seq % maxDepth_
        const size_t slot = seq % maxDepth_;
        pending_[slot]    = frame;
        pendingSet_[slot] = true;

        // Всё, что уже стоит «по порядку», отправляем в SPSC-очередь к RT
        while (true) {
            const size_t s = nextExpected_ % maxDepth_;
            if (!pendingSet_[s]) break;
            if (!queue_.try_enqueue(pending_[s])) break;  // очередь полна — RT не успевает
            pendingSet_[s] = false;
            ++nextExpected_;
            ++delivered_;
        }

        // Как только накопили targetDepth кадров — «открываем шлюз» для RT
        if (!ready_.load(std::memory_order_relaxed) && delivered_ >= targetDepth_) {
            ready_.store(true, std::memory_order_release);
        }
        return true;
    }

    // ============ ПОТРЕБИТЕЛЬ: RT-микшер ============
    // Никаких аллокаций, никаких мьютексов, никаких ветвлений по размеру.
    bool Pop(Frame& out) noexcept {
        if (!ready_.load(std::memory_order_acquire)) return false;
        if (queue_.try_dequeue(out)) {
            lastFrame_ = out;
            plcFrames_ = 0;
            plcGain_ = 1.0f;
            return true;
        }
        if (plcFrames_ >= MAX_PLC_FRAMES) return false;
        // fade-out предыдущего кадра
        for (size_t i = 0; i < FRAME; ++i)
            out[i] = lastFrame_[i] * plcGain_;
        plcGain_ *= PLC_DECAY;      // 0.6f
        ++plcFrames_;
        return true;                 // ← ВАЖНО
    }

    // Вызывается ТОЛЬКО из сетевого потока, когда RT гарантированно не читает
    // (см. RemoveClient в менеджере).
    void Reset() {
        Frame tmp;
        while (queue_.try_dequeue(tmp)) {}
        std::fill(pendingSet_.begin(), pendingSet_.end(), false);
        primed_ = false;
        ready_.store(false, std::memory_order_release);
        delivered_    = 0;
        nextExpected_ = 0;
    }

    size_t LateDrops() const noexcept { return lateDrops_; }

private:
    static bool seqLess(uint32_t a, uint32_t b) noexcept {
        return static_cast<int32_t>(a - b) < 0;
    }

    // ===== PLC-параметры =====
    static constexpr int   MAX_PLC_FRAMES = 10;      // до 200 мс concealment
    static constexpr float PLC_DECAY      = 0.6f;    // гасим до 60% за кадр

    // ===== PLC-состояние (RT-only, кроме Reset) =====
    Frame lastFrame_{};
    int   plcFrames_ = 0;
    float plcGain_   = 1.0f;

    moodycamel::ReaderWriterQueue<Frame> queue_;
    std::atomic<bool>                    ready_{false};

    // Всё ниже — только производитель, из RT не читается.
    std::vector<Frame> pending_;
    std::vector<bool>  pendingSet_;
    uint32_t nextExpected_ = 0;
    bool     primed_       = false;
    size_t   delivered_    = 0;
    size_t   lateDrops_    = 0;
    size_t   targetDepth_;
    size_t   maxDepth_;
};



template <typename T, size_t FRAME, size_t MAX_CLIENTS = 64, size_t QUEUE_CAP = 32>
class JitterBufferManager {
public:
    using Frame = std::array<T, FRAME>;

    JitterBufferManager(size_t targetDepth, size_t maxDepth)
        : targetDepth_(targetDepth), maxDepth_(maxDepth) {
        for (auto& p : slots_) p.store(nullptr, std::memory_order_relaxed);
    }

    ~JitterBufferManager() {
        for (auto& p : slots_) delete p.load(std::memory_order_relaxed);
    }

    // ============ ПРОИЗВОДИТЕЛЬ (сетевой поток) ============
    void PushPacket(uint32_t clientId, uint32_t seq, const Frame& frame) {
        if (clientId >= MAX_CLIENTS) return;  // клиент за пределами пула
        Slot* c = getOrCreate(clientId);
        c->jb.Push(seq, frame);
    }

    // Только сетевой/control-поток. RT в этот момент не читает (active = false).
    void RemoveClient(uint32_t clientId) {
        if (clientId >= MAX_CLIENTS) return;
        Slot* c = slots_[clientId].load(std::memory_order_acquire);
        if (!c) return;

        c->active.store(false, std::memory_order_release);
        // RT теперь не зайдёт в Pop(). Очищаем очередь на стороне producer-а.
        // Небольшой sleep, чтобы RT успел завершить текущий MixInto.
        // (если RemoveClient вызывается редко — это ок)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        c->jb.Reset();
    }

    // ============ ПОТРЕБИТЕЛЬ (RT-микшер) ============
    // Полностью lock-free: атомарные load + try_dequeue. Без аллокаций.
    size_t MixInto(T* dst, size_t count) noexcept {
        size_t mixed = 0;
        Frame tmp;

        // Коэффициент сглаживания gain за один кадр (~20 мс):
        // 1 - exp(-1/(tau * Fs_frame)), tau ~50 мс → ~0.33
        // Проще: фиксированный 0.3f даёт переключение gain за 2-3 кадра,
        // что не слышно как щелчок, но и не «затянуто».
        constexpr float kGainSmooth = 0.3f;

        for (size_t i = 0; i < MAX_CLIENTS; ++i) {
            Slot* c = slots_[i].load(std::memory_order_acquire);
            if (!c) continue;
            if (!c->active.load(std::memory_order_acquire)) continue;

            if (!c->jb.Pop(tmp)) continue;

            // --- per-client gain со сглаживанием ---
            const float target = c->targetGain.load(std::memory_order_relaxed);
            const float g0 = c->smoothedGain;
            const float g1 = g0 + (target - g0) * kGainSmooth;
            c->smoothedGain = g1;

            // линейная интерполяция gain по кадру — иначе на границе
            // между кадрами будет микро-ступенька
            const float dg = (g1 - g0) / static_cast<float>(count);
            float g = g0;
            for (size_t k = 0; k < count; ++k) {
                dst[k] += tmp[k] * g;
                g += dg;
            }
            ++mixed;
        }
        return mixed;
    }

    size_t ClientCount() const noexcept {
        size_t n = 0;
        for (auto& p : slots_) if (p.load(std::memory_order_relaxed)) ++n;
        return n;
    }

    void SetClientGain(uint32_t clientId, float gain) {
        if (clientId >= MAX_CLIENTS) return;
        Slot* c = slots_[clientId].load(std::memory_order_acquire);
        if (!c) return;
        if (gain < 0.0f) gain = 0.0f;
        if (gain > 4.0f) gain = 4.0f;              // защита от +∞; 4× хватит с запасом
        c->targetGain.store(gain, std::memory_order_relaxed);
    }

    float GetClientGain(uint32_t clientId) const noexcept {
        if (clientId >= MAX_CLIENTS) return 0.0f;
        Slot* c = slots_[clientId].load(std::memory_order_acquire);
        if (!c) return 0.0f;
        return c->targetGain.load(std::memory_order_relaxed);
    }

private:
    struct Slot {
        ClientJitterBuffer<T, FRAME, QUEUE_CAP> jb;

        std::atomic<bool>  active{true};
        std::atomic<float> targetGain{1.0f};   // пишет control-поток, читает RT

        // RT-only состояние: трогает ТОЛЬКО MixInto. Никаких атомиков не нужно.
        float smoothedGain = 1.0f;

        Slot(size_t td, size_t md) : jb(td, md) {}
    };

    Slot* getOrCreate(uint32_t id) {
        Slot* c = slots_[id].load(std::memory_order_acquire);
        if (c) {
            // Реактивация: сбрасываем состояние пока RT спит на active=false
            if (!c->active.load(std::memory_order_relaxed)) {
                c->jb.Reset();
                c->active.store(true, std::memory_order_release);
            }
            return c;
        }
        Slot* nc = new Slot(targetDepth_, maxDepth_);
        slots_[id].store(nc, std::memory_order_release);
        return nc;
    }

    std::array<std::atomic<Slot*>, MAX_CLIENTS> slots_;
    size_t targetDepth_;
    size_t maxDepth_;
};