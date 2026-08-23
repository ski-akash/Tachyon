#pragma once

#include "executor/Executor.h"
#include <unordered_map>
#include <vector>
#include <memory>
#include <iostream>

namespace tachyon {

// The state we maintain for every single time bucket
struct VWAPState {
    uint64_t bucket_time = 0;
    int64_t sum_price_vol = 0;
    uint64_t sum_vol = 0;
    uint32_t tick_count = 0;
};

class TimeSeriesAggExecutor : public Executor {
private:
    std::shared_ptr<Executor> child_;
    
    // 1 Minute in nanoseconds
    const uint64_t BUCKET_INTERVAL_NS = 60000000000ULL; 
    
    // Hash table mapping a Time Bucket to its VWAP state
    std::unordered_map<uint64_t, VWAPState> hash_table_;
    
    // Iterators for yielding results
    std::unordered_map<uint64_t, VWAPState>::iterator current_it_;
    bool is_computed_ = false;

public:
    TimeSeriesAggExecutor(std::shared_ptr<Executor> child) : child_(std::move(child)) {}

    void init() override {
        child_->init();
        hash_table_.clear();
        is_computed_ = false;
    }

    bool next(Chunk& chunk) override {
        // Step 1: Consume the entire child pipeline (TickScan) and build the Hash Table
        if (!is_computed_) {
            Chunk child_chunk;
            
            while (child_->next(child_chunk)) {
                // Vectorized tight-loop processing
                for (size_t i = 0; i < child_chunk.size; ++i) {
                    uint64_t ts = child_chunk.numeric_columns[0][i];
                    int64_t price = child_chunk.numeric_columns[1][i];
                    uint32_t size = child_chunk.numeric_columns[2][i];
                    
                    // 1. Calculate the Time Bucket (Floor to nearest minute)
                    uint64_t bucket = (ts / BUCKET_INTERVAL_NS) * BUCKET_INTERVAL_NS;
                    
                    // 2. Update the running VWAP math
                    auto& state = hash_table_[bucket];
                    state.bucket_time = bucket;
                    state.sum_price_vol += static_cast<int64_t>(price) * size;
                    state.sum_vol += size;
                    state.tick_count += 1;
                }
            }
            is_computed_ = true;
            current_it_ = hash_table_.begin();
        }

        // Step 2: Yield the aggregated results back up the pipeline in chunks
        if (current_it_ == hash_table_.end()) {
            return false;
        }

        size_t output_size = 0;
        chunk.is_numeric = true;
        // Output format: [Bucket_Time, Final_VWAP, Total_Volume, Tick_Count]
        chunk.numeric_columns.assign(4, std::vector<int64_t>(1024)); 

        while (current_it_ != hash_table_.end() && output_size < 1024) {
            const auto& state = current_it_->second;
            
            // Calculate final VWAP for this bucket
            int64_t final_vwap = 0;
            if (state.sum_vol > 0) {
                final_vwap = static_cast<int64_t>(state.sum_price_vol / state.sum_vol);
            }

            chunk.numeric_columns[0][output_size] = state.bucket_time;
            chunk.numeric_columns[1][output_size] = final_vwap;
            chunk.numeric_columns[2][output_size] = state.sum_vol;
            chunk.numeric_columns[3][output_size] = state.tick_count;

            output_size++;
            current_it_++;
        }

        chunk.size = output_size;
        return output_size > 0;
    }
};

} // namespace tachyon