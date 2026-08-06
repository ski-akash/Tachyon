#include "executor/TickScanExecutor.h"
#include "executor/TimeSeries/AggExecutor.h"
#include <iostream>
#include <chrono>

using namespace quill;

int main() {
    std::cout << "=== QuillDB Quant: VWAP & OHLCV Aggregation ===\n\n";

    // Simulating: SELECT TIME_BUCKET(time, '1m'), VWAP(price, size), SUM(size), COUNT(*)
    //             FROM ticks 
    //             WHERE time BETWEEN <start> AND <end>
    //             GROUP BY TIME_BUCKET(time, '1m');

    // A 30-minute window in nanoseconds
    uint64_t start_time = 1704067200000000000ULL; 
    uint64_t end_time   = start_time + (30ULL * 60ULL * 1000000000ULL); 

    std::cout << "Executing query over 30-minute time window...\n";

    auto start_timer = std::chrono::high_resolution_clock::now();

    // 1. Create the base scan (Zero-Copy MMap)
    auto tick_scan = std::make_shared<TickScanExecutor>("ticks_data.bin", start_time, end_time);
    
    // 2. Wrap it in our new Financial Aggregator
    TimeSeriesAggExecutor agg_executor(tick_scan);
    agg_executor.init();

    // 3. Drain the pipeline
    Chunk result_chunk;
    size_t total_buckets = 0;
    
    std::cout << "---------------------------------------------------------\n";
    std::cout << "Bucket_Time \t\t VWAP \t\t Volume \t Ticks\n";
    std::cout << "---------------------------------------------------------\n";

    while (agg_executor.next(result_chunk)) {
        for (size_t i = 0; i < result_chunk.size; ++i) {
            uint64_t b_time = result_chunk.numeric_columns[0][i];
            int64_t  vwap   = result_chunk.numeric_columns[1][i];
            uint64_t vol    = result_chunk.numeric_columns[2][i];
            uint32_t ticks  = result_chunk.numeric_columns[3][i];

            std::cout << b_time << " \t " << (vwap / 10000.0) << " \t " << vol << " \t\t " << ticks << "\n";
            total_buckets++;
        }
    }

    auto end_timer = std::chrono::high_resolution_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_timer - start_timer).count();

    std::cout << "---------------------------------------------------------\n";
    std::cout << "Total 1-Minute Buckets Generated : " << total_buckets << "\n";
    std::cout << "Vectorized Aggregation Latency   : " << total_ms << " ms\n";
    std::cout << "=========================================================\n";

    return 0;
}