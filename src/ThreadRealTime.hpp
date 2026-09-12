#pragma once

#include <thread>
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <system_error>
#include <functional>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <pthread.h>
    #include <sched.h>
#endif

// Поток с real-time приоритетом.
// Наследует std::thread, поэтому joinable(), detach() и т.д. работают как обычно.
class ThreadRealTime : public std::thread {
public:
    ThreadRealTime() noexcept = default;

    // Конструктор как у std::thread, но с дополнительным параметром priority.
    // priority: 1..99 (SCHED_FIFO). 0 = не менять приоритет.
    template <class F, class... Args>
    explicit ThreadRealTime(int priority, F&& f, Args&&... args)
        : std::thread(&ThreadRealTime::trampoline<
                          std::decay_t<F>, std::decay_t<Args>...>,
                      priority,
                      std::forward<F>(f),
                      std::forward<Args>(args)...)
    {}

    // Удобный «фабричный» метод, если не хочется возиться с шаблоном конструктора.
    template <class F, class... Args>
    static ThreadRealTime spawn(int priority, F&& f, Args&&... args) {
        return ThreadRealTime(priority,
                              std::forward<F>(f),
                              std::forward<Args>(args)...);
    }

    // Установить приоритет уже запущенному потоку (снаружи).
    // Возвращает true при успехе.
    bool setPriority(int priority) noexcept {
        if (!joinable()) return false;
        return applyPriority(native_handle(), priority);
    }

    // Текущий приоритет потока (или -1 при ошибке).
    int getPriority() noexcept {
        if (!joinable()) return -1;
        #ifdef _WIN32
            return GetThreadPriority(native_handle());
        #else
            int policy = 0;
            sched_param sp{};
            if (pthread_getschedparam(native_handle(), &policy, &sp) != 0)
                return -1;
            return sp.sched_priority;
        #endif
    }

private:
    template <class F, class... Args>
    static void trampoline(int priority, F&& f, Args&&... args) {
        if (priority > 0) {
            if (!applyPriority(self_id(), priority)) {
                std::fprintf(stderr,
                    "[ThreadRealTime] failed to set priority %d: %s\n",
                    priority, std::strerror(errno));
            }
        }
        std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
    }

    static std::thread::native_handle_type self_id() noexcept {
        #ifdef _WIN32
            return GetCurrentThread();
        #else
            return pthread_self();
        #endif
    }

    static bool applyPriority(std::thread::native_handle_type handle, int priority) noexcept {
        #ifdef _WIN32
            HANDLE h = handle;
            if (!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS))
                return false;
            return SetThreadPriority(h, THREAD_PRIORITY_TIME_CRITICAL) != 0;
        #else
            sched_param sp{};
            sp.sched_priority = priority;   // 1..99 для SCHED_FIFO
            pthread_t t = handle;
            return pthread_setschedparam(t, SCHED_FIFO, &sp) == 0;
        #endif
    }
};