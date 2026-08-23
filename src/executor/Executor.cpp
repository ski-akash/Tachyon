#include "executor/Executor.h"
#include <algorithm>
#include <charconv>

namespace quill {

namespace {

// Evaluates "lhs <op> rhs" for a column value. Numeric comparison is used
// whenever both sides parse as numbers (covers <, >, <=, >=, =, !=); string
// comparison is the fallback for = and != on non-numeric columns.
bool evaluateComparison(const std::string& lhs, const std::string& op, const std::string& rhs) {
    double lhs_num, rhs_num;
    auto lhs_result = std::from_chars(lhs.data(), lhs.data() + lhs.size(), lhs_num);
    auto rhs_result = std::from_chars(rhs.data(), rhs.data() + rhs.size(), rhs_num);
    bool both_numeric = lhs_result.ec == std::errc() && rhs_result.ec == std::errc();

    if (both_numeric) {
        if (op == "=") return lhs_num == rhs_num;
        if (op == "!=") return lhs_num != rhs_num;
        if (op == "<") return lhs_num < rhs_num;
        if (op == ">") return lhs_num > rhs_num;
        if (op == "<=") return lhs_num <= rhs_num;
        if (op == ">=") return lhs_num >= rhs_num;
        return false;
    }

    if (op == "=") return lhs == rhs;
    if (op == "!=") return lhs != rhs;
    return false; // <, >, <=, >= are meaningless on non-numeric values
}

} // namespace

// ---------------------------------------------------------
// 1. SEQUENCE SCAN (Vectorized)
// ---------------------------------------------------------
SeqScanExecutor::SeqScanExecutor(std::shared_ptr<Table> table)
    : table_(std::move(table)) {}

void SeqScanExecutor::init() {
    current_row_ = 0; 
}

bool SeqScanExecutor::next(Chunk& out_chunk) {
    if (current_row_ >= table_->num_rows_) return false;

    // Determine how many rows to fetch (up to BATCH_SIZE)
    size_t rows_to_fetch = std::min(BATCH_SIZE, table_->num_rows_ - current_row_);
    
    out_chunk.columns.resize(table_->column_names.size());
    for (auto& col : out_chunk.columns) col.clear();
    out_chunk.size = rows_to_fetch;

    // Bulk copy data from the table into our chunk
    for (size_t col_idx = 0; col_idx < table_->column_names.size(); ++col_idx) {
        for (size_t i = 0; i < rows_to_fetch; ++i) {
            out_chunk.columns[col_idx].push_back(table_->column_data_[col_idx][current_row_ + i]);
        }
    }
    
    current_row_ += rows_to_fetch;
    return true;
}

// ---------------------------------------------------------
// 2. FILTER (Vectorized)
// ---------------------------------------------------------
FilterExecutor::FilterExecutor(std::unique_ptr<Executor> child, 
                               std::shared_ptr<Expression> predicate, 
                               std::shared_ptr<Table> table_schema)
    : child_(std::move(child)), predicate_(std::move(predicate)), table_schema_(std::move(table_schema)) {}

void FilterExecutor::init() {
    child_->init();
}

bool FilterExecutor::next(Chunk& out_chunk) {
    Chunk in_chunk;
    
    while (child_->next(in_chunk)) {
        if (!predicate_) {
            out_chunk = in_chunk; // No WHERE clause, pass the whole chunk
            return true; 
        }

        auto binary_expr = std::dynamic_pointer_cast<BinaryExpression>(predicate_);
        if (binary_expr) {
            auto left_ident = std::dynamic_pointer_cast<Identifier>(binary_expr->left);
            auto right_literal = std::dynamic_pointer_cast<NumberLiteral>(binary_expr->right);

            if (left_ident && right_literal) {
                int col_idx = table_schema_->getColumnIndex(left_ident->value);
                if (col_idx != -1) {
                    out_chunk.columns.resize(in_chunk.columns.size());
                    for (auto& col : out_chunk.columns) col.clear();
                    out_chunk.size = 0;

                    // Tight CPU loop to evaluate the filter over the whole batch
                    for (size_t row_idx = 0; row_idx < in_chunk.size; ++row_idx) {
                        if (evaluateComparison(in_chunk.columns[col_idx][row_idx], binary_expr->op, right_literal->value)) {
                            // Row passes, copy it to the output chunk
                            for (size_t c = 0; c < in_chunk.columns.size(); ++c) {
                                out_chunk.columns[c].push_back(in_chunk.columns[c][row_idx]);
                            }
                            out_chunk.size++;
                        }
                    }
                    if (out_chunk.size > 0) return true; // Yield this filtered batch
                }
            }
        }
    }
    return false; 
}

// ---------------------------------------------------------
// 3. PROJECT (Vectorized)
// ---------------------------------------------------------
ProjectExecutor::ProjectExecutor(std::unique_ptr<Executor> child, 
                                 std::vector<std::shared_ptr<Expression>> columns,
                                 std::shared_ptr<Table> table_schema)
    : child_(std::move(child)), columns_(std::move(columns)), table_schema_(std::move(table_schema)) {}

void ProjectExecutor::init() {
    child_->init();
}

bool ProjectExecutor::next(Chunk& out_chunk) {
    Chunk in_chunk;
    
    if (child_->next(in_chunk)) {
        out_chunk.columns.resize(columns_.size());
        for (auto& col : out_chunk.columns) col.clear();
        out_chunk.size = in_chunk.size;
        
        // Build a new chunk containing ONLY the requested columns
        for (size_t out_c = 0; out_c < columns_.size(); ++out_c) {
            auto ident = std::dynamic_pointer_cast<Identifier>(columns_[out_c]);
            if (ident) {
                int in_c = table_schema_->getColumnIndex(ident->value);
                if (in_c != -1) {
                    // FAST PATH: Bulk copy the entire column array at once!
                    out_chunk.columns[out_c] = in_chunk.columns[in_c]; 
                }
            }
        }
        return true;
    }
    return false;
}

// ---------------------------------------------------------
// 4. NESTED LOOP JOIN (Vectorized)
// ---------------------------------------------------------
NestedLoopJoinExecutor::NestedLoopJoinExecutor(std::unique_ptr<Executor> left, 
                                               std::unique_ptr<Executor> right, 
                                               std::shared_ptr<Expression> predicate,
                                               std::shared_ptr<Table> left_schema,
                                               std::shared_ptr<Table> right_schema)
    : left_child_(std::move(left)), right_child_(std::move(right)), 
      predicate_(std::move(predicate)), 
      left_schema_(std::move(left_schema)), right_schema_(std::move(right_schema)) {}

void NestedLoopJoinExecutor::init() {
    left_child_->init();
    right_child_->init();
    
    // Materialize the entire right child into memory once
    right_materialized_.clear();
    Chunk right_chunk;
    while (right_child_->next(right_chunk)) {
        right_materialized_.push_back(right_chunk);
    }
}

bool NestedLoopJoinExecutor::next(Chunk& out_chunk) {
    Chunk left_chunk;
    
    // Pull a batch of rows from the left table
    if (left_child_->next(left_chunk)) {
        
        // Prepare the output chunk size (Left columns + Right columns)
        size_t total_cols = left_schema_->column_names.size() + right_schema_->column_names.size();
        out_chunk.columns.resize(total_cols);
        for (auto& col : out_chunk.columns) col.clear();
        out_chunk.size = 0;

        // Parse the ON condition (e.g., users.id = orders.user_id)
        auto binary_expr = std::dynamic_pointer_cast<BinaryExpression>(predicate_);
        if (!binary_expr || binary_expr->op != "=") return false;

        auto left_ident = std::dynamic_pointer_cast<Identifier>(binary_expr->left);
        auto right_ident = std::dynamic_pointer_cast<Identifier>(binary_expr->right);
        if (!left_ident || !right_ident) return false;

        // Strip the table names (e.g., "users.id" -> "id") for lookup
        std::string left_col_name = left_ident->value.substr(left_ident->value.find('.') + 1);
        std::string right_col_name = right_ident->value.substr(right_ident->value.find('.') + 1);

        int l_col_idx = left_schema_->getColumnIndex(left_col_name);
        int r_col_idx = right_schema_->getColumnIndex(right_col_name);
        
        if (l_col_idx == -1 || r_col_idx == -1) return false;

        // THE VECTORIZED NESTED LOOP
        // 1. Loop through every row in the current Left chunk
        for (size_t l_row = 0; l_row < left_chunk.size; ++l_row) {
            
            // 2. Loop through every materialized Right chunk
            for (const auto& right_chunk : right_materialized_) {
                
                // 3. Loop through every row in the current Right chunk
                for (size_t r_row = 0; r_row < right_chunk.size; ++r_row) {
                    
                    // Do they match?
                    if (left_chunk.columns[l_col_idx][l_row] == right_chunk.columns[r_col_idx][r_row]) {
                        
                        // YES! Copy the left columns into the output
                        for (size_t c = 0; c < left_chunk.columns.size(); ++c) {
                            out_chunk.columns[c].push_back(left_chunk.columns[c][l_row]);
                        }
                        
                        // And copy the right columns into the output
                        for (size_t c = 0; c < right_chunk.columns.size(); ++c) {
                            out_chunk.columns[left_chunk.columns.size() + c].push_back(right_chunk.columns[c][r_row]);
                        }
                        out_chunk.size++;
                    }
                }
            }
        }
        
        // If we found matches, return true to pass the chunk up the pipeline!
        if (out_chunk.size > 0) return true;
    }
    
    return false; // No more data
}

// ---------------------------------------------------------
// 5. HASH JOIN (Vectorized)
// ---------------------------------------------------------
HashJoinExecutor::HashJoinExecutor(std::unique_ptr<Executor> left, 
                                   std::unique_ptr<Executor> right, 
                                   std::shared_ptr<Expression> predicate,
                                   std::shared_ptr<Table> left_schema,
                                   std::shared_ptr<Table> right_schema)
    : left_child_(std::move(left)), right_child_(std::move(right)), 
      predicate_(std::move(predicate)), 
      left_schema_(std::move(left_schema)), right_schema_(std::move(right_schema)) {}

void HashJoinExecutor::init() {
    left_child_->init();
    right_child_->init();
    
    hash_map_.clear();

    // Parse the ON condition to figure out which column from the right table we are hashing
    auto binary_expr = std::dynamic_pointer_cast<BinaryExpression>(predicate_);
    auto right_ident = std::dynamic_pointer_cast<Identifier>(binary_expr->right);
    std::string right_col_name = right_ident->value.substr(right_ident->value.find('.') + 1);
    int r_col_idx = right_schema_->getColumnIndex(right_col_name);

    // === BUILD PHASE ===
    // Read the entire right table and build the hash map
    Chunk right_chunk;
    while (right_child_->next(right_chunk)) {
        for (size_t r = 0; r < right_chunk.size; ++r) {
            std::string key = right_chunk.columns[r_col_idx][r];
            
            // Extract the row into a simple vector to store in our map
            std::vector<std::string> row;
            for (size_t c = 0; c < right_chunk.columns.size(); ++c) {
                row.push_back(right_chunk.columns[c][r]);
            }
            
            hash_map_[key].push_back(std::move(row));
        }
    }
}

bool HashJoinExecutor::next(Chunk& out_chunk) {
    Chunk left_chunk;
    
    // === PROBE PHASE ===
    // Pull a batch of rows from the left table
    if (left_child_->next(left_chunk)) {
        
        size_t total_cols = left_schema_->column_names.size() + right_schema_->column_names.size();
        out_chunk.columns.resize(total_cols);
        for (auto& col : out_chunk.columns) col.clear();
        out_chunk.size = 0;

        // Parse the ON condition for the left table's column
        auto binary_expr = std::dynamic_pointer_cast<BinaryExpression>(predicate_);
        auto left_ident = std::dynamic_pointer_cast<Identifier>(binary_expr->left);
        std::string left_col_name = left_ident->value.substr(left_ident->value.find('.') + 1);
        int l_col_idx = left_schema_->getColumnIndex(left_col_name);
        
        // Scan the left chunk and do an O(1) hash map lookup for every row
        for (size_t l_row = 0; l_row < left_chunk.size; ++l_row) {
            std::string key = left_chunk.columns[l_col_idx][l_row];
            
            // Did we find a match in the hash map?
            if (hash_map_.find(key) != hash_map_.end()) {
                
                // Yes! There might be multiple matching right rows for this one left row
                for (const auto& right_row : hash_map_[key]) {
                    
                    // Copy left columns
                    for (size_t c = 0; c < left_chunk.columns.size(); ++c) {
                        out_chunk.columns[c].push_back(left_chunk.columns[c][l_row]);
                    }
                    
                    // Copy right columns
                    for (size_t c = 0; c < right_row.size(); ++c) {
                        out_chunk.columns[left_chunk.columns.size() + c].push_back(right_row[c]);
                    }
                    out_chunk.size++;
                }
            }
        }
        
        if (out_chunk.size > 0) return true;
    }
    
    return false; // No more data
}

// ---------------------------------------------------------
// 6. AGGREGATE (Vectorized)
// ---------------------------------------------------------
AggregateExecutor::AggregateExecutor(std::unique_ptr<Executor> child, 
                                     std::vector<std::shared_ptr<Expression>> groupBys,
                                     std::vector<std::shared_ptr<Expression>> aggregates,
                                     std::shared_ptr<Table> table_schema)
    : child_(std::move(child)), groupBys_(std::move(groupBys)), 
      aggregates_(std::move(aggregates)), table_schema_(std::move(table_schema)) {}

void AggregateExecutor::init() {
    child_->init();
    final_results_.clear();
    current_row_ = 0;

    // The Hash Map: Key is the GROUP BY value, Value is the running SUM
    std::unordered_map<std::string, int> hash_map;

    // Figure out which column we are grouping by (e.g., user_id)
    auto gb_ident = std::dynamic_pointer_cast<Identifier>(groupBys_[0]);
    int gb_idx = table_schema_->getColumnIndex(gb_ident->value);

    // Figure out which column we are summing (e.g., amount)
    auto agg_func = std::dynamic_pointer_cast<FunctionCall>(aggregates_[0]);
    auto agg_ident = std::dynamic_pointer_cast<Identifier>(agg_func->arguments[0]);
    int agg_idx = table_schema_->getColumnIndex(agg_ident->value);

    Chunk in_chunk;
    
    // === BUILD PHASE ===
    // Pull chunks from the child and compute the running totals!
    while (child_->next(in_chunk)) {
        for (size_t r = 0; r < in_chunk.size; ++r) {
            std::string group_key = in_chunk.columns[gb_idx][r];
            std::string amount_str = in_chunk.columns[agg_idx][r];
            
            // Strip the '$' and convert to integer
            if (!amount_str.empty() && amount_str[0] == '$') amount_str = amount_str.substr(1);
            int amount = std::stoi(amount_str);

            // Add it to our hash map accumulator
            hash_map[group_key] += amount;
        }
    }

    // Convert the finished hash map into a list of rows so we can output it
    for (const auto& pair : hash_map) {
        final_results_.push_back({pair.first, "$" + std::to_string(pair.second)});
    }
}

bool AggregateExecutor::next(Chunk& out_chunk) {
    // === PROBE PHASE ===
    if (current_row_ >= final_results_.size()) return false;

    size_t rows_to_fetch = std::min(BATCH_SIZE, final_results_.size() - current_row_);
    
    out_chunk.columns.resize(2); // One for the Group Key, One for the Sum
    for (auto& col : out_chunk.columns) col.clear();
    out_chunk.size = rows_to_fetch;

    for (size_t i = 0; i < rows_to_fetch; ++i) {
        out_chunk.columns[0].push_back(final_results_[current_row_ + i][0]);
        out_chunk.columns[1].push_back(final_results_[current_row_ + i][1]);
    }
    
    current_row_ += rows_to_fetch;
    return true;
}

// ---------------------------------------------------------
// 7. INDEX SCAN (Vectorized)
// ---------------------------------------------------------
IndexScanExecutor::IndexScanExecutor(std::shared_ptr<Table> table, std::string col, std::string key)
    : table_(std::move(table)), column_name_(std::move(col)), lookup_key_(std::move(key)) {}

void IndexScanExecutor::init() {
    current_idx_ = 0;
    matched_row_ids_.clear();

    // Look up the exact row numbers from the Hash Index!
    auto it = table_->indexes_.find(column_name_);
    if (it != table_->indexes_.end()) {
        matched_row_ids_ = it->second->lookup(lookup_key_);
    }
}

bool IndexScanExecutor::next(Chunk& out_chunk) {
    // If we fetched all matching rows, we are done
    if (current_idx_ >= matched_row_ids_.size()) return false;

    size_t rows_to_fetch = std::min(BATCH_SIZE, matched_row_ids_.size() - current_idx_);

    out_chunk.columns.resize(table_->column_names.size());
    for (auto& col : out_chunk.columns) col.clear();
    out_chunk.size = rows_to_fetch;

    // Bulk copy ONLY the specific rows that the index told us about
    for (size_t col_idx = 0; col_idx < table_->column_names.size(); ++col_idx) {
        for (size_t i = 0; i < rows_to_fetch; ++i) {
            size_t actual_row_id = matched_row_ids_[current_idx_ + i];
            out_chunk.columns[col_idx].push_back(table_->column_data_[col_idx][actual_row_id]);
        }
    }

    current_idx_ += rows_to_fetch;
    return true;
}

} // namespace quill