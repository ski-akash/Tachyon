#pragma once

#include "executor/Executor.h"
#include "storage/MMapReader.h"
#include "storage/Storage.h" // Where your Chunk struct lives
#include <memory>
#include <vector>
#include <cstring>

namespace quill {

class TickScanExecutor : public Executor {
private:
    std::unique_ptr<MMapReader> mmap_;
    size_t total_rows_ = 0;
    size_t current_row_ = 0;
    
    const uint32_t* ts_deltas_ = nullptr;
    const int64_t* prices_ = nullptr;
    const uint32_t* sizes_ = nullptr;
    const uint16_t* symbol_ids_ = nullptr;
    const uint8_t* sides_ = nullptr;
    
    uint64_t current_ts_ = 0; 
    const size_t CHUNK_SIZE = 1024; 

    // NEW: Time bounds
    uint64_t start_time_;
    uint64_t end_time_;

public:
    TickScanExecutor(const std::string& filepath, uint64_t start = 0, uint64_t end = UINT64_MAX)
        : start_time_(start), end_time_(end) {
        mmap_ = std::make_unique<MMapReader>(filepath);
    }

    void init() override {
        const char* ptr = mmap_->data;
        if (std::strncmp(ptr, "QDBT", 4) != 0) throw std::runtime_error("Invalid Tick File Format");
        ptr += 4;
        
        total_rows_ = *reinterpret_cast<const size_t*>(ptr);
        ptr += sizeof(size_t);
        if (total_rows_ == 0) return;

        current_ts_ = *reinterpret_cast<const uint64_t*>(ptr);
        ptr += sizeof(uint64_t);
        
        ts_deltas_ = reinterpret_cast<const uint32_t*>(ptr);
        ptr += (total_rows_ - 1) * sizeof(uint32_t);
        
        prices_ = reinterpret_cast<const int64_t*>(ptr);
        ptr += total_rows_ * sizeof(int64_t);
        
        sizes_ = reinterpret_cast<const uint32_t*>(ptr);
        ptr += total_rows_ * sizeof(uint32_t);
        
        symbol_ids_ = reinterpret_cast<const uint16_t*>(ptr);
        ptr += total_rows_ * sizeof(uint16_t);
        sides_ = reinterpret_cast<const uint8_t*>(ptr);
        
        current_row_ = 0;

        // NEW: Fast-Forward to the Start Time (Simulated Index Seek)
        while (current_row_ < total_rows_ && current_ts_ < start_time_) {
            current_row_++;
            if (current_row_ < total_rows_) {
                current_ts_ += ts_deltas_[current_row_ - 1];
            }
        }
    }

    bool next(Chunk& chunk) override {
        // EOF or Passed the End Time boundary
        if (current_row_ >= total_rows_ || current_ts_ > end_time_) return false;

        size_t rows_to_read = std::min(CHUNK_SIZE, total_rows_ - current_row_);
        chunk.is_numeric = true;
        chunk.numeric_columns.assign(5, std::vector<int64_t>(rows_to_read));

        size_t actual_reads = 0;
        for (size_t i = 0; i < rows_to_read; ++i) {
            // Halt immediately if we cross the upper time boundary
            if (current_ts_ > end_time_) break;

            chunk.numeric_columns[0][i] = current_ts_;
            chunk.numeric_columns[1][i] = prices_[current_row_];
            chunk.numeric_columns[2][i] = sizes_[current_row_];
            chunk.numeric_columns[3][i] = symbol_ids_[current_row_];
            chunk.numeric_columns[4][i] = sides_[current_row_];

            actual_reads++;
            current_row_++;
            if (current_row_ < total_rows_) {
                current_ts_ += ts_deltas_[current_row_ - 1];
            }
        }

        chunk.size = actual_reads;
        return actual_reads > 0;
    }
};

} // namespace quill