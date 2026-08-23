#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace tachyon {

class Index {
public:
    std::string column_name;
    
    // The Hash Map: Keys are column values (e.g., "42"), 
    // Values are lists of row indices (e.g., row 1, row 5)
    std::unordered_map<std::string, std::vector<size_t>> map_;

    explicit Index(std::string col) : column_name(std::move(col)) {}

    // Add a new row to the index
    void insert(const std::string& key, size_t row_id) {
        map_[key].push_back(row_id);
    }

    // Instantly find all row indices for a given value
    std::vector<size_t> lookup(const std::string& key) const {
        auto it = map_.find(key);
        if (it != map_.end()) {
            return it->second;
        }
        return {}; // Return empty if not found
    }
};

} // namespace tachyon