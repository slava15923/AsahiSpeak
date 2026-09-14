#pragma once

#include <iostream>
#include <map>
#include <mutex>
#include <cstdint>

template <typename T>
class JitterBuffer {
private:
    std::mutex mutex;
    uint64_t sequence;
    uint64_t minimalSize = 3;
    std::map<uint64_t, T> buffer;
    unsigned int maxSize = 32;
    bool primed = false;
public:
    JitterBuffer(const uint64_t startSequence_) : sequence(startSequence_) {}

    void push(const uint64_t sequence_, T data) {
        std::lock_guard<std::mutex> lock(mutex);
        if (sequence > sequence_) return;
        if(buffer.size() >= maxSize){ 
            buffer.erase(buffer.begin()); 
            buffer.try_emplace(sequence_, std::move(data));
            return;
        }
        buffer.try_emplace(sequence_, std::move(data));
        if(buffer.size() >= minimalSize) primed = true;
    }

    bool pop(T& data) {
        std::lock_guard<std::mutex> lock(mutex);

        if (buffer.empty()) { primed = false; return false; }
        if (!primed) return false;

        auto it = buffer.begin();

        sequence = it->first;

        data = std::move(it->second);
        buffer.erase(it);
        ++sequence;
        return true;
    }
    
};