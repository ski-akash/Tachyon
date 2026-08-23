#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "planner/Planner.h"
#include "optimizer/Optimizer.h"
#include "executor/TickScanExecutor.h"
#include <iostream>
#include <chrono>

int main() {
    std::cout << "=== Tachyon End-to-End Time-Series Query ===\n\n";

    // Query looking for a specific 10-millisecond window deep inside our 10M tick file
    std::string sql = "SELECT price FROM ticks WHERE time BETWEEN 1704067200500000000 AND 1704067200510000000;";
    std::cout << "Query: " << sql << "\n\n";

    // 1. Front-End
    tachyon::Lexer lexer(sql);
    tachyon::Parser parser(std::move(lexer));
    auto ast = parser.parse()[0];

    // 2. Middle-End
    tachyon::Planner planner;
    auto logicalPlan = planner.createPlan(ast);

    tachyon::Optimizer optimizer(nullptr); // No catalog needed for this pushdown rule
    auto optimizedPlan = optimizer.optimize(logicalPlan);

    std::cout << "Optimized Plan:\n" << optimizedPlan->toString() << "\n\n";

    // Extract bounds from the rewritten plan
    std::shared_ptr<tachyon::TickScanNode> tickNode = nullptr;
    
    // The root is the ProjectNode. The TickScanNode is its child!
    if (auto projectNode = std::dynamic_pointer_cast<tachyon::ProjectNode>(optimizedPlan)) {
        tickNode = std::dynamic_pointer_cast<tachyon::TickScanNode>(projectNode->child);
    }

    if (!tickNode) {
        std::cout << "Optimization Failed! The AST was not rewritten.\n";
        return 1;
    }

    // 3. Back-End Execution
    auto start_time = std::chrono::high_resolution_clock::now();

    tachyon::TickScanExecutor executor("ticks_data.bin", tickNode->start_time, tickNode->end_time);
    executor.init();

    tachyon::Chunk chunk;
    size_t rows_fetched = 0;
    while (executor.next(chunk)) {
        rows_fetched += chunk.size;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "Rows Fetched: " << rows_fetched << "\n";
    std::cout << "Query Latency: " << total_ms << " ms\n";
    std::cout << "============================================\n";

    return 0;
}