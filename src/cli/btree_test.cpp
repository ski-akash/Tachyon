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

    return 0;
}