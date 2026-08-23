#pragma once

#include "storage/Index.h" // NEW
#include "index/BTree.h"
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace quill {

using Tuple = std::vector<std::string>;

struct Chunk {
    // Legacy support for your existing standard SQL engine
    std::vector<std::vector<std::string>> columns;
    
    // NEW: Fast-path numeric columns for the Time-Series engine
    std::vector<std::vector<int64_t>> numeric_columns;
    
    size_t size = 0;
    
    // Flag to tell executors which vectors to read
    bool is_numeric = false; 
};

class Table {
public:
    std::string name;
    std::vector<std::string> column_names;
    std::vector<std::vector<std::string>> column_data_;
    size_t num_rows_ = 0;

    // NEW: A map of column names to their Indexes
    std::unordered_map<std::string, std::shared_ptr<Index>> indexes_;

    // A map of column names to their B+Tree range indexes (numeric columns only)
    std::unordered_map<std::string, std::shared_ptr<BTree>> range_indexes_;

    Table(std::string n, std::vector<std::string> cols)
        : name(std::move(n)), column_names(std::move(cols)) {
        column_data_.resize(column_names.size());
    }

    // NEW: Allow users to create an index on a specific column
    void createIndex(const std::string& colName) {
        if (indexes_.find(colName) == indexes_.end()) {
            indexes_[colName] = std::make_shared<Index>(colName);
            
            // Backfill the index with existing data
            int colIdx = getColumnIndex(colName);
            if (colIdx != -1) {
                for (size_t r = 0; r < num_rows_; ++r) {
                    indexes_[colName]->insert(column_data_[colIdx][r], r);
                }
            }
        }
    }

    // NEW: Allow users to create a B+Tree range index on a numeric column
    void createRangeIndex(const std::string& colName) {
        if (range_indexes_.find(colName) == range_indexes_.end()) {
            auto tree = std::make_shared<BTree>();
            range_indexes_[colName] = tree;

            int colIdx = getColumnIndex(colName);
            if (colIdx != -1) {
                for (size_t r = 0; r < num_rows_; ++r) {
                    tree->insert(std::stoll(column_data_[colIdx][r]), r);
                }
            }
        }
    }

    void insert(const Tuple& row) {
        if (row.size() != column_names.size()) {
            throw std::runtime_error("Insert failed: Row size does not match column count.");
        }
        
        size_t new_row_id = num_rows_; // The index where this row will live

        for (size_t i = 0; i < row.size(); ++i) {
            column_data_[i].push_back(row[i]);
            
            // NEW: If this column has an index, update it!
            auto it = indexes_.find(column_names[i]);
            if (it != indexes_.end()) {
                it->second->insert(row[i], new_row_id);
            }

            auto rit = range_indexes_.find(column_names[i]);
            if (rit != range_indexes_.end()) {
                rit->second->insert(std::stoll(row[i]), new_row_id);
            }
        }
        num_rows_++;
    }

    int getColumnIndex(const std::string& colName) const {
        for (size_t i = 0; i < column_names.size(); ++i) {
            if (column_names[i] == colName) return i;
        }
        return -1;
    }
};

} // namespace quill