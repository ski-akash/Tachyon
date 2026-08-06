#pragma once

#include <cstdint>

namespace quill {

// 32-byte aligned for perfect L1 cache line packing (2 ticks per 64-byte line)
struct alignas(32) Tick {
    uint64_t timestamp_ns;  // 8 bytes: Nanoseconds since epoch
    int64_t  price;         // 8 bytes: Fixed-point price (price * 10000)
    uint32_t size;          // 4 bytes: Volume
    uint32_t seq_num;       // 4 bytes: Exchange sequence number
    uint16_t symbol_id;     // 2 bytes: Interned dictionary ID
    uint8_t  side;          // 1 byte:  0 = Bid, 1 = Ask, 2 = Trade
    uint8_t  padding[5];    // 5 bytes: Explicit padding
};

} // namespace quill