#include "storage/Tick.h"
#include "storage/TickStore.h"
#include "storage/RingBuffer.h"
#include <iostream>
#include <chrono>
#include <vector>
#include <algorithm>

using namespace tachyon;

constexpr size_t BUFFER_SIZE = 1048576; // 1M capacity
constexpr size_t BATCHES = 10;          // 10 batches of 1M = 10M total ticks
constexpr size_t TOTAL_TICKS = BUFFER_SIZE * BATCHES;

SPSCRingBuffer<BUFFER_SIZE> ring_buffer;
TickStore tick_store;

int main() {
    std::cout << "==========================================\n";
    std::cout << "   Tachyon Time-Series Ingestion Benchmark\n";
    std::cout << "==========================================\n\n";

    tick_store.getOrInternSymbol("AAPL");
    std::vector<uint64_t> latencies;
    latencies.reserve(TOTAL_TICKS);

    auto start_time = std::chrono::high_resolution_clock::now();

    Tick t;
    t.price = 1502500;
    t.size = 100;
    t.symbol_id = 1;
    t.side = 1;

    for (size_t batch = 0; batch < BATCHES; ++batch) {
        // 1. PRODUCER PHASE: Fill to maximum safe capacity (N - 1)
        for (size_t i = 0; i < BUFFER_SIZE - 1; ++i) {
            t.timestamp_ns = std::chrono::system_clock::now().time_since_epoch().count();
            t.seq_num = (batch * BUFFER_SIZE) + i;
            ring_buffer.push(t);
        }

        // 2. CONSUMER PHASE: Drain exactly what we pushed
        size_t ticks_processed = 0;
        while (ticks_processed < BUFFER_SIZE - 1) {
            Tick out_tick;
            if (ring_buffer.pop(out_tick)) {
                uint64_t now = std::chrono::system_clock::now().time_since_epoch().count();
                latencies.push_back(now - out_tick.timestamp_ns);
                tick_store.append(out_tick);
                ticks_processed++;
            }
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::nth_element(latencies.begin(), latencies.begin() + latencies.size() / 2, latencies.end());
    uint64_t p50 = latencies[latencies.size() / 2];

    size_t p99_idx = latencies.size() * 0.99;
    std::nth_element(latencies.begin(), latencies.begin() + p99_idx, latencies.end());
    uint64_t p99 = latencies[p99_idx];

    std::cout << "Total Ticks Ingested : " << TOTAL_TICKS << "\n";
    std::cout << "Total Time           : " << total_ms << " ms\n";
    std::cout << "Throughput           : " << (TOTAL_TICKS / (total_ms / 1000.0)) / 1000000.0 << " Million Ticks / sec\n\n";
    
    std::cout << "--- Latency (Memory Pipeline) ---\n";
    std::cout << "p50 Latency          : " << p50 << " nanoseconds\n";
    std::cout << "p99 Latency          : " << p99 << " nanoseconds\n";
    std::cout << "==========================================\n";

    return 0;
}