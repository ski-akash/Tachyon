#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "planner/Planner.h"
#include "optimizer/Optimizer.h"
#include "catalog/Catalog.h"
#include "executor/Executor.h"
#include <iostream>
#include <chrono>
#include <vector>

const int64_t dim = 1000000;

// Helper function to execute a physical plan
void executePlan(std::unique_ptr<tachyon::Executor> executor) {
    executor->init();
    tachyon::Chunk chunk;
    while (executor->next(chunk)) {
        // We just fetch the data to measure speed, we don't need to print 1 million rows
    }
}

int main() {
    std::cout << "==========================================\n";
    std::cout << "       Tachyon Performance Benchmark      \n";
    std::cout << "==========================================\n\n";

    auto catalog = std::make_shared<tachyon::Catalog>();
    auto users_table = std::make_shared<tachyon::Table>("users", std::vector<std::string>{"id", "name"});

    std::cout << "Inserting 1,000,000 rows into in-memory columnar store...\n";
    for (int i = 0; i < dim; ++i) {
        users_table->insert({std::to_string(i), "User_" + std::to_string(i)});
    }
    catalog->setTableRowCount("users", dim);
    std::cout << "Data loaded.\n\n";

    std::string sql = "SELECT name FROM users WHERE id = 999999;"; 
    tachyon::Lexer lexer(sql);
    tachyon::Parser parser(std::move(lexer));
    auto targetStmt = parser.parse()[0];

    tachyon::Planner planner;
    auto logicalPlan = planner.createPlan(targetStmt);

    // ==========================================
    // TEST 1: SEQUENTIAL SCAN (No Index)
    // ==========================================
    std::cout << "--- TEST 1: Sequential Scan (O(N)) ---\n";
    tachyon::Optimizer naiveOptimizer(catalog); // Catalog has NO index registered
    auto naivePlan = naiveOptimizer.optimize(logicalPlan);
    
    // Wire up the physical executor manually for the benchmark
    auto scan = std::make_unique<tachyon::SeqScanExecutor>(users_table);
    auto filter = std::make_unique<tachyon::FilterExecutor>(
        std::move(scan), 
        std::dynamic_pointer_cast<tachyon::SelectStatement>(targetStmt)->whereClause, 
        users_table
    );
    
    auto start_time = std::chrono::high_resolution_clock::now();
    executePlan(std::move(filter));
    auto end_time = std::chrono::high_resolution_clock::now();
    auto seq_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    std::cout << "Sequential Scan Time: " << seq_duration << " ms\n\n";

    // ==========================================
    // TEST 2: INDEX SCAN (O(1))
    // ==========================================
    std::cout << "--- TEST 2: Hash Index Scan (O(1)) ---\n";
    
    // Create the index and register it with the Catalog
    users_table->createIndex("id");
    catalog->createIndex("users", "id", tachyon::IndexType::HASH); // or BTREE

    tachyon::Optimizer smartOptimizer(catalog);
    auto smartPlan = smartOptimizer.optimize(logicalPlan); // Optimizer automatically chooses IndexScan
    
    auto index_scan = std::make_unique<tachyon::IndexScanExecutor>(users_table, "id", "999999");

    start_time = std::chrono::high_resolution_clock::now();
    executePlan(std::move(index_scan));
    end_time = std::chrono::high_resolution_clock::now();
    auto idx_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "Index Scan Time: " << idx_duration << " ms\n\n";

    std::cout << "Performance Increase: " << (seq_duration / (idx_duration == 0 ? 1 : idx_duration)) << "x faster\n";
    std::cout << "==========================================\n";

    return 0;
}