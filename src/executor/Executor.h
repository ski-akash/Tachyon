#pragma once

#include "planner/LogicalPlan.h"
#include "storage/Storage.h"
#include <memory>
#include <vector>
#include <unordered_map>

namespace tachyon {

// How many rows we process in a single chunk
constexpr size_t BATCH_SIZE = 100; 

class Executor {
public:
    virtual ~Executor() = default;
    
    virtual void init() = 0;
    
    // NEW: Returns true if a CHUNK of rows was fetched, false if no more data.
    virtual bool next(Chunk& out_chunk) = 0;
};

class SeqScanExecutor : public Executor {
public:
    explicit SeqScanExecutor(std::shared_ptr<Table> table);
    void init() override;
    bool next(Chunk& out_chunk) override;

private:
    std::shared_ptr<Table> table_;
    size_t current_row_ = 0;
};

// 7. INDEX SCAN (Vectorized)
class IndexScanExecutor : public Executor {
public:
    IndexScanExecutor(std::shared_ptr<Table> table, std::string column_name, std::string lookup_key);
    
    void init() override;
    bool next(Chunk& out_chunk) override;

private:
    std::shared_ptr<Table> table_;
    std::string column_name_;
    std::string lookup_key_;
    
    // We will store the exact row numbers we need to fetch here
    std::vector<size_t> matched_row_ids_; 
    size_t current_idx_ = 0;
};

class FilterExecutor : public Executor {
public:
    FilterExecutor(std::unique_ptr<Executor> child, 
                   std::shared_ptr<Expression> predicate, 
                   std::shared_ptr<Table> table_schema);
    
    void init() override;
    bool next(Chunk& out_chunk) override;

private:
    std::unique_ptr<Executor> child_;
    std::shared_ptr<Expression> predicate_;
    std::shared_ptr<Table> table_schema_; 
};

class ProjectExecutor : public Executor {
public:
    ProjectExecutor(std::unique_ptr<Executor> child, 
                    std::vector<std::shared_ptr<Expression>> columns,
                    std::shared_ptr<Table> table_schema);
    
    void init() override;
    bool next(Chunk& out_chunk) override;

private:
    std::unique_ptr<Executor> child_;
    std::vector<std::shared_ptr<Expression>> columns_;
    std::shared_ptr<Table> table_schema_;
};

// 4. NESTED LOOP JOIN (Vectorized)
class NestedLoopJoinExecutor : public Executor {
public:
    NestedLoopJoinExecutor(std::unique_ptr<Executor> left, 
                           std::unique_ptr<Executor> right, 
                           std::shared_ptr<Expression> predicate,
                           std::shared_ptr<Table> left_schema,
                           std::shared_ptr<Table> right_schema);
    
    void init() override;
    bool next(Chunk& out_chunk) override;

private:
    std::unique_ptr<Executor> left_child_;
    std::unique_ptr<Executor> right_child_;
    std::shared_ptr<Expression> predicate_;
    
    std::shared_ptr<Table> left_schema_;
    std::shared_ptr<Table> right_schema_;
    
    // We "materialize" the right table's chunks into memory during init()
    std::vector<Chunk> right_materialized_; 
};

// 5. HASH JOIN (Vectorized)
class HashJoinExecutor : public Executor {
public:
    HashJoinExecutor(std::unique_ptr<Executor> left, 
                     std::unique_ptr<Executor> right, 
                     std::shared_ptr<Expression> predicate,
                     std::shared_ptr<Table> left_schema,
                     std::shared_ptr<Table> right_schema);
    
    void init() override;
    bool next(Chunk& out_chunk) override;

private:
    std::unique_ptr<Executor> left_child_;
    std::unique_ptr<Executor> right_child_;
    std::shared_ptr<Expression> predicate_;
    
    std::shared_ptr<Table> left_schema_;
    std::shared_ptr<Table> right_schema_;
    
    // THE HASH MAP: Key is the string value of the join column. 
    // Value is a list of rows (each row is a vector of strings).
    std::unordered_map<std::string, std::vector<std::vector<std::string>>> hash_map_; 
};

// 6. AGGREGATE (Vectorized)
class AggregateExecutor : public Executor {
public:
    AggregateExecutor(std::unique_ptr<Executor> child, 
                      std::vector<std::shared_ptr<Expression>> groupBys,
                      std::vector<std::shared_ptr<Expression>> aggregates,
                      std::shared_ptr<Table> table_schema);
    
    void init() override;
    bool next(Chunk& out_chunk) override;

private:
    std::unique_ptr<Executor> child_;
    std::vector<std::shared_ptr<Expression>> groupBys_;
    std::vector<std::shared_ptr<Expression>> aggregates_;
    std::shared_ptr<Table> table_schema_;
    
    // We compute the final grouped results during init() and store them here
    std::vector<std::vector<std::string>> final_results_;
    size_t current_row_ = 0;
};

} // namespace tachyon