#pragma once

#include "storage/Tick.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace quill {

class TickStore {
public:
    // Structure of Arrays (SoA) for blistering fast columnar scans
    std::vector<uint64_t> timestamps;
    std::vector<int64_t>  prices;
    std::vector<uint32_t> sizes;
    std::vector<uint32_t> seq_nums;
    std::vector<uint16_t> symbol_ids;
    std::vector<uint8_t>  sides;

    // Symbol interning: Map string tickers ("AAPL") to fast integers (1)
    std::unordered_map<std::string, uint16_t> symbol_to_id;
    std::unordered_map<uint16_t, std::string> id_to_symbol;
    uint16_t next_symbol_id = 1;

    // Temporarily removed std::mutex for Windows compiler compatibility
    uint16_t getOrInternSymbol(const std::string& symbol) {
        if (symbol_to_id.find(symbol) == symbol_to_id.end()) {
            symbol_to_id[symbol] = next_symbol_id;
            id_to_symbol[next_symbol_id] = symbol;
            next_symbol_id++;
        }
        return symbol_to_id[symbol];
    }

    // Append a tick into the columnar arrays
    // (We will handle concurrency for this in the ring buffer step)
    void append(const Tick& t) {
        timestamps.push_back(t.timestamp_ns);
        prices.push_back(t.price);
        sizes.push_back(t.size);
        seq_nums.push_back(t.seq_num);
        symbol_ids.push_back(t.symbol_id);
        sides.push_back(t.side);
    }
    
    size_t size() const { return timestamps.size(); }
};

} // namespace quill