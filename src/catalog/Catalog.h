#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set> // NEW
#include <vector>

namespace quill {

enum class IndexType {
    HASH,
    BTREE
};

struct IndexInfo {
    std::string tableName;
    std::string columnName;
    IndexType type;
};

class Catalog {
public:
    void setTableRowCount(const std::string& tableName, size_t rowCount);
    size_t getTableRowCount(const std::string& tableName) const;

    // NEW: Track which columns have an index
    void addIndex(const std::string& tableName, const std::string& columnName);

    // NEW API: Register an index
    void createIndex(const std::string& tableName, const std::string& columnName, IndexType type) {
        indexes_.push_back({tableName, columnName, type});
    }

    // NEW API: Check if a column has a specific type of index
    bool hasIndex(const std::string& tableName, const std::string& columnName, IndexType type) const {
        for (const auto& idx : indexes_) {
            if (idx.tableName == tableName && idx.columnName == columnName && idx.type == type) {
                return true;
            }
        }
        return false;
    }

private:
    std::unordered_map<std::string, size_t> table_row_counts_;
    
    // Map of Table Name -> Set of Indexed Column Names
    std::unordered_map<std::string, std::unordered_set<std::string>> table_indexes_;

    std::vector<IndexInfo> indexes_;
};

} // namespace quill