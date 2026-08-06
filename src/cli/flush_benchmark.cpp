#include "storage/TickStore.h"
#include "storage/TickFlusher.h"
#include <iostream>
#include <chrono>
#include <fstream>

using namespace quill;

constexpr size_t TOTAL_TICKS = 10000000; // 10 Million ticks

int main() {
    std::cout << "==========================================\n";
    std::cout << " QuillDB Persistence & Compression Test   \n";
    std::cout << "==========================================\n\n";

    TickStore store;
    store.getOrInternSymbol("AAPL");
    store.getOrInternSymbol("MSFT");

    std::cout << "Generating " << TOTAL_TICKS << " synthetic ticks...\n";

    Tick t;
    t.price = 1502500;
    t.size = 100;
    t.side = 1;
    
    // Start at a fixed timestamp
    uint64_t current_time_ns = 1704067200000000000ULL; // 2024-01-01 00:00:00

    for (size_t i = 0; i < TOTAL_TICKS; ++i) {
        // Advance time by a small random delta (e.g., 5000 to 25000 nanoseconds)
        // This ensures the delta easily fits inside a 32-bit unsigned integer
        current_time_ns += 5000 + (i % 20000); 
        
        t.timestamp_ns = current_time_ns;
        t.seq_num = i;
        t.symbol_id = (i % 2 == 0) ? 1 : 2; // Alternate between AAPL and MSFT
        
        store.append(t);
    }
    
    std::cout << "Data generated. Flushing to disk...\n\n";

    // Measure write time
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::string filepath = "ticks_data.bin";
    TickFlusher::flushToDisk(store, filepath);
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    // Calculate compression metrics
    // Use standard ifstream to jump to the end of the file and read the byte offset
    std::ifstream in(filepath, std::ifstream::ate | std::ifstream::binary);
    uintmax_t file_size_bytes = in.tellg();
    in.close();
    
    // A raw dump of our 32-byte structs
    uintmax_t raw_size_bytes = TOTAL_TICKS * sizeof(Tick); 
    
    double file_size_mb = file_size_bytes / (1024.0 * 1024.0);
    double raw_size_mb = raw_size_bytes / (1024.0 * 1024.0);
    double compression_ratio = (1.0 - (static_cast<double>(file_size_bytes) / raw_size_bytes)) * 100.0;

    std::cout << "\n--- Persistence Metrics ---\n";
    std::cout << "Flush Time           : " << total_ms << " ms\n";
    std::cout << "Write Bandwidth      : " << (file_size_mb / (total_ms / 1000.0)) << " MB/sec\n\n";

    std::cout << "--- Storage Metrics ---\n";
    std::cout << "Naive Raw Size       : " << raw_size_mb << " MB (32 bytes/tick)\n";
    std::cout << "Delta-Encoded Size   : " << file_size_mb << " MB\n";
    std::cout << "Space Saved          : " << compression_ratio << "%\n";
    std::cout << "==========================================\n";

    return 0;
}