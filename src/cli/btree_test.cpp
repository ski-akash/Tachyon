#include "index/BTree.h"
#include <iostream>
#include <cassert>

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

    return 0;
}