#pragma once

#include <vector>
#include <memory>
#include <cstdint>
#include <iostream>

namespace tachyon {

// The maximum number of keys a node can hold before it must split.
constexpr size_t BTREE_ORDER = 128; 

// --- 1. Base Node ---
struct BTreeNode {
    bool is_leaf;
    
    // Virtual destructor ensures proper cleanup of derived classes
    virtual ~BTreeNode() = default;
    
    explicit BTreeNode(bool leaf) : is_leaf(leaf) {}
};

// --- 2. Internal Node (Routing) ---
// Contains keys for routing, and pointers to child nodes.
// It does NOT contain actual row data.
struct InternalNode : public BTreeNode {
    std::vector<int64_t> keys; 
    std::vector<std::shared_ptr<BTreeNode>> children; 

    InternalNode() : BTreeNode(false) {
        keys.reserve(BTREE_ORDER);
        children.reserve(BTREE_ORDER + 1); // Internal nodes always have N+1 children
    }
};

// --- 3. Leaf Node (Data Storage) ---
// Contains keys and the actual physical Row IDs (or byte offsets).
// Linked horizontally for lightning-fast range scans.
struct LeafNode : public BTreeNode {
    std::vector<int64_t> keys;
    std::vector<uint64_t> values; // Represents Row ID or File Offset
    
    std::shared_ptr<LeafNode> next_leaf; // The magic Range Scan link

    LeafNode() : BTreeNode(true), next_leaf(nullptr) {
        keys.reserve(BTREE_ORDER);
        values.reserve(BTREE_ORDER);
    }
};

// --- 4. The B+Tree Manager ---
class BTree {
private:
    std::shared_ptr<BTreeNode> root_;

public:
    BTree() {
        // Every B+Tree starts its life as a single, empty Leaf Node
        root_ = std::make_shared<LeafNode>(); 
    }

    void insert(int64_t key, uint64_t value);
    
    // NEW: Declare the range scan function
    std::vector<uint64_t> searchRange(int64_t start_key, int64_t end_key);
};

} // namespace tachyon