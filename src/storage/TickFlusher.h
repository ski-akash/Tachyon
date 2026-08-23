#pragma once

#include "storage/TickStore.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace tachyon {

class TickFlusher {
public:
    // Flushes the columnar data to a binary file using Delta Encoding for timestamps
    static void flushToDisk(const TickStore& store, const std::string& filepath) {
        std::ofstream out(filepath, std::ios::binary);
        if (!out) {
            throw std::runtime_error("Failed to open file for writing: " + filepath);
        }

        size_t row_count = store.size();
        
        // 1. Write File Header (Magic Bytes + Row Count)
        const char magic[] = "QDBT"; // Tachyon TimeSeries
        out.write(magic, 4);
        out.write(reinterpret_cast<const char*>(&row_count), sizeof(size_t));

        if (row_count == 0) return;

        // 2. Write Timestamps (Delta Encoded)
        // Store the very first timestamp as a full 64-bit reference point
        uint64_t base_timestamp = store.timestamps[0];
        out.write(reinterpret_cast<const char*>(&base_timestamp), sizeof(uint64_t));

        // Store the rest as 32-bit deltas (assuming ticks are less than ~4 seconds apart)
        std::vector<uint32_t> ts_deltas;
        ts_deltas.reserve(row_count - 1);
        for (size_t i = 1; i < row_count; ++i) {
            uint64_t diff = store.timestamps[i] - store.timestamps[i - 1];
            ts_deltas.push_back(static_cast<uint32_t>(diff)); 
        }
        out.write(reinterpret_cast<const char*>(ts_deltas.data()), ts_deltas.size() * sizeof(uint32_t));

        // 3. Write Prices & Sizes (Raw block writes for maximum speed)
        out.write(reinterpret_cast<const char*>(store.prices.data()), row_count * sizeof(int64_t));
        out.write(reinterpret_cast<const char*>(store.sizes.data()), row_count * sizeof(uint32_t));
        
        // 4. Write Symbol IDs and Sides
        out.write(reinterpret_cast<const char*>(store.symbol_ids.data()), row_count * sizeof(uint16_t));
        out.write(reinterpret_cast<const char*>(store.sides.data()), row_count * sizeof(uint8_t));

        out.close();
        std::cout << "Successfully flushed " << row_count << " ticks to " << filepath << "\n";
    }
};

} // namespace tachyon