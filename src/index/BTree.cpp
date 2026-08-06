#include "index/BTree.h"
#include <algorithm>

namespace quill {

// A helper struct to pass split data back up the recursive call stack
struct SplitResult {
    bool was_split = false;
    int64_t median_key = 0;
    std::shared_ptr<BTreeNode> right_node = nullptr;
};

class BTreeImpl {
public:
    static SplitResult insertRecursive(std::shared_ptr<BTreeNode> node, int64_t key, uint64_t value) {
        if (node->is_leaf) {
            auto leaf = std::static_pointer_cast<LeafNode>(node);
            
            // 1. Binary search to find the insertion point
            auto it = std::lower_bound(leaf->keys.begin(), leaf->keys.end(), key);
            size_t index = std::distance(leaf->keys.begin(), it);

            // 2. Insert into the sorted arrays
            leaf->keys.insert(leaf->keys.begin() + index, key);
            leaf->values.insert(leaf->values.begin() + index, value);

            // 3. Check for Overflow
            if (leaf->keys.size() <= BTREE_ORDER) {
                return {false, 0, nullptr}; // No split needed
            }

            // 4. Split the Leaf
            auto new_leaf = std::make_shared<LeafNode>();
            size_t mid = leaf->keys.size() / 2;

            // Move the upper half to the new leaf
            new_leaf->keys.assign(leaf->keys.begin() + mid, leaf->keys.end());
            new_leaf->values.assign(leaf->values.begin() + mid, leaf->values.end());
            
            leaf->keys.resize(mid);
            leaf->values.resize(mid);

            // Maintain the Linked List for Range Scans!
            new_leaf->next_leaf = leaf->next_leaf;
            leaf->next_leaf = new_leaf;

            // In a B+Tree, the median key is COPIED up to the parent from a leaf
            return {true, new_leaf->keys[0], new_leaf};
        } 
        else {
            auto internal = std::static_pointer_cast<InternalNode>(node);
            
            // 1. Binary search to find which child to descend into
            auto it = std::upper_bound(internal->keys.begin(), internal->keys.end(), key);
            size_t child_index = std::distance(internal->keys.begin(), it);

            // 2. Recursively insert into the child
            SplitResult result = insertRecursive(internal->children[child_index], key, value);

            if (!result.was_split) {
                return {false, 0, nullptr};
            }

            // 3. Child split! We must insert the new median key and right child pointer here
            internal->keys.insert(internal->keys.begin() + child_index, result.median_key);
            internal->children.insert(internal->children.begin() + child_index + 1, result.right_node);

            // 4. Check for Internal Node Overflow
            if (internal->keys.size() <= BTREE_ORDER) {
                return {false, 0, nullptr};
            }

            // 5. Split the Internal Node
            auto new_internal = std::make_shared<InternalNode>();
            size_t mid = internal->keys.size() / 2;

            // In an Internal Node, the median key is PUSHED up (removed from this level)
            int64_t mid_key = internal->keys[mid];

            new_internal->keys.assign(internal->keys.begin() + mid + 1, internal->keys.end());
            new_internal->children.assign(internal->children.begin() + mid + 1, internal->children.end());

            internal->keys.resize(mid);
            internal->children.resize(mid + 1);

            return {true, mid_key, new_internal};
        }
    }

    // NEW: Helper to drop straight down to the correct leaf
    static std::shared_ptr<LeafNode> findLeaf(std::shared_ptr<BTreeNode> node, int64_t key) {
        while (!node->is_leaf) {
            auto internal = std::static_pointer_cast<InternalNode>(node);
            // Find which child pointer to follow
            auto it = std::upper_bound(internal->keys.begin(), internal->keys.end(), key);
            size_t child_index = std::distance(internal->keys.begin(), it);
            node = internal->children[child_index];
        }
        return std::static_pointer_cast<LeafNode>(node);
    }

};

// The Public API
void BTree::insert(int64_t key, uint64_t value) {
    SplitResult result = BTreeImpl::insertRecursive(root_, key, value);

    // If the Root itself split, we must grow the tree upwards!
    if (result.was_split) {
        auto new_root = std::make_shared<InternalNode>();
        new_root->keys.push_back(result.median_key);
        new_root->children.push_back(root_);
        new_root->children.push_back(result.right_node);
        root_ = new_root;
    }
}

// NEW: The Horizontal Range Scan
std::vector<uint64_t> BTree::searchRange(int64_t start_key, int64_t end_key) {
    std::vector<uint64_t> results;
    if (!root_) return results;

    // 1. Drop down the tree $O(\log N)$ to find the starting leaf
    auto leaf = BTreeImpl::findLeaf(root_, start_key);

    // 2. Slide horizontally across the linked leaves
    while (leaf != nullptr) {
        for (size_t i = 0; i < leaf->keys.size(); ++i) {
            // If we've passed the upper bound, we are completely done!
            if (leaf->keys[i] > end_key) {
                return results; 
            }
            
            // If it's within our range, collect the value (Row ID)
            if (leaf->keys[i] >= start_key) {
                results.push_back(leaf->values[i]);
            }
        }
        // Move to the next leaf node in the chain
        leaf = leaf->next_leaf; 
    }

    return results;
}

} // namespace quill