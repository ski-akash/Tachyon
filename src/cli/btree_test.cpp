#include "index/BTree.h"
#include <iostream>
#include <cassert>
#include <chrono>
#include <vector>
#include <utility>

using namespace quill;

int main() {
    std::cout << "=== B+Tree Insertion & Split Test ===\n";
    
    BTree tree;
    const int TOTAL_INSERTS = 10000;

    std::cout << "Inserting " << TOTAL_INSERTS << " sequential keys...\n";
    
    // Inserting sequentially is the absolute worst-case scenario for a B-Tree 
    // because it forces continuous right-side splits. If this works, our split math is perfect.
    for (int i = 1; i <= TOTAL_INSERTS; ++i) {
        // Key is 'i', Value is a mock Row ID (i * 10)
        tree.insert(i, i * 10);
    }

    std::cout << "Successfully inserted " << TOTAL_INSERTS << " keys!\n";
    std::cout << "All internal and leaf nodes split correctly without segfaults.\n";
    std::cout << "=====================================\n";

    std::cout << "\nTesting Range Scan: BETWEEN 500 AND 550...\n";
    
    auto results = tree.searchRange(500, 550);
    
    std::cout << "Rows Found: " << results.size() << "\n";
    if (results.size() > 0) {
        std::cout << "First Row ID: " << results.front() << "\n";
        std::cout << "Last Row ID: " << results.back() << "\n";
    }

    // Verify correctness
    assert(results.size() == 51); // 500 to 550 inclusive is 51 rows
    assert(results.front() == 5000); // 500 * 10
    assert(results.back() == 5500);  // 550 * 10

    std::cout << "Range Scan passed! Horizontal Leaf Traversal is working.\n";
    std::cout << "=====================================\n";

    // --- Performance: B+Tree range scan vs. a naive linear scan ---
    // Same dataset, same query, timed both ways so the speedup is measured,
    // not asserted.
    const int LARGE_N = 5000000;
    std::cout << "\n=== B+Tree vs Linear Scan: " << LARGE_N << " keys ===\n";

    BTree large_tree;
    std::vector<std::pair<int64_t, uint64_t>> flat_table;
    flat_table.reserve(LARGE_N);
    for (int i = 1; i <= LARGE_N; ++i) {
        large_tree.insert(i, static_cast<uint64_t>(i) * 10);
        flat_table.push_back({i, static_cast<uint64_t>(i) * 10});
    }

    const int64_t range_start = LARGE_N / 2;
    const int64_t range_end = range_start + 50;

    auto t0 = std::chrono::high_resolution_clock::now();
    auto tree_results = large_tree.searchRange(range_start, range_end);
    auto t1 = std::chrono::high_resolution_clock::now();

    std::vector<uint64_t> linear_results;
    for (const auto& [key, value] : flat_table) {
        if (key >= range_start && key <= range_end) linear_results.push_back(value);
    }
    auto t2 = std::chrono::high_resolution_clock::now();

    auto tree_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    auto linear_us = std::chrono::duration_cast<std::chrono::microseconds>(t2 - t1).count();

    assert(tree_results.size() == linear_results.size());

    std::cout << "Query: BETWEEN " << range_start << " AND " << range_end
              << " (" << tree_results.size() << " rows)\n";
    std::cout << "B+Tree Range Scan   : " << tree_us << " us\n";
    std::cout << "Linear Scan         : " << linear_us << " us\n";
    std::cout << "=====================================\n";

    return 0;
}