#pragma once

#include "storage/Tick.h"
#include <atomic>
#include <vector>
#include <cstddef>
#include <stdexcept>

namespace tachyon {

template <size_t Capacity>
class SPSCRingBuffer {
public:
    SPSCRingBuffer() : buffer_(Capacity) {
        // Capacity must be a power of 2 for fast modulo arithmetic (bitwise AND)
        if ((Capacity & (Capacity - 1)) != 0) {
            throw std::invalid_argument("Capacity must be a power of 2");
        }
    }

    // Called by the Ingestion/Network Thread
    bool push(const Tick& tick) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & (Capacity - 1);

        // If the buffer is full, we must drop or spin (returning false to let caller spin)
        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false;
        }

        buffer_[current_tail] = tick;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Called by the Storage Thread
    bool pop(Tick& out_tick) {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        // If the buffer is empty
        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false; 
        }

        out_tick = buffer_[current_head];
        head_.store((current_head + 1) & (Capacity - 1), std::memory_order_release);
        return true;
    }

private:
    std::vector<Tick> buffer_;

    // Align to 64 bytes to prevent false sharing between producer and consumer threads
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

} // namespace tachyon