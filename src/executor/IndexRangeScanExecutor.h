#pragma once

#include "executor/Executor.h"
#include "storage/Storage.h"
#include "index/BTree.h"
#include <memory>
#include <string>
#include <vector>

namespace quill {

// Physical executor for IndexRangeScanNode: an O(log N) B+Tree descent to
// the start of the range, followed by an O(k) horizontal leaf traversal to
// collect every matching row id, then a normal columnar row fetch - the
// same fetch pattern IndexScanExecutor uses for point lookups.
class IndexRangeScanExecutor : public Executor {
public:
    IndexRangeScanExecutor(std::shared_ptr<Table> table, std::string column_name,
                            int64_t start_key, int64_t end_key)
        : table_(std::move(table)), column_name_(std::move(column_name)),
          start_key_(start_key), end_key_(end_key) {}

    void init() override {
        current_idx_ = 0;
        matched_row_ids_.clear();

        auto it = table_->range_indexes_.find(column_name_);
        if (it != table_->range_indexes_.end()) {
            matched_row_ids_ = it->second->searchRange(start_key_, end_key_);
        }
    }

    bool next(Chunk& out_chunk) override {
        if (current_idx_ >= matched_row_ids_.size()) return false;

        size_t rows_to_fetch = std::min(BATCH_SIZE, matched_row_ids_.size() - current_idx_);

        out_chunk.columns.resize(table_->column_names.size());
        for (auto& col : out_chunk.columns) col.clear();
        out_chunk.size = rows_to_fetch;

        for (size_t col_idx = 0; col_idx < table_->column_names.size(); ++col_idx) {
            for (size_t i = 0; i < rows_to_fetch; ++i) {
                uint64_t row_id = matched_row_ids_[current_idx_ + i];
                out_chunk.columns[col_idx].push_back(table_->column_data_[col_idx][row_id]);
            }
        }

        current_idx_ += rows_to_fetch;
        return true;
    }

private:
    std::shared_ptr<Table> table_;
    std::string column_name_;
    int64_t start_key_;
    int64_t end_key_;

    std::vector<uint64_t> matched_row_ids_;
    size_t current_idx_ = 0;
};

} // namespace quill
