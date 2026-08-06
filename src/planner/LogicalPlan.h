#pragma once

#include "ast/AST.h"
#include <memory>
#include <string>
#include <vector>

namespace quill {

// Base class for all relational algebra operators
class PlanNode {
public:
    virtual ~PlanNode() = default;
    
    // For debugging and the EXPLAIN command later
    virtual std::string toString() const = 0;
};

// 1. SCAN: Reads raw data from a table
class SeqScanNode : public PlanNode {
public:
    std::string tableName;

    explicit SeqScanNode(std::string name) : tableName(std::move(name)) {}

    std::string toString() const override {
        return "SeqScan(table: " + tableName + ")";
    }
};

// 1b. INDEX SCAN: Reads specific rows using an O(1) Hash Index
class IndexScanNode : public PlanNode {
public:
    std::string tableName;
    std::string columnName;
    std::string lookupKey; // E.g., "42"

    IndexScanNode(std::string table, std::string col, std::string key)
        : tableName(std::move(table)), columnName(std::move(col)), lookupKey(std::move(key)) {}

    std::string toString() const override {
        return "IndexScan(table: " + tableName + ", index: " + columnName + " = " + lookupKey + ")";
    }
};

// 2. FILTER: Applies a WHERE clause
class FilterNode : public PlanNode {
public:
    std::shared_ptr<PlanNode> child;       // The data source (e.g., a SeqScan)
    std::shared_ptr<Expression> predicate; // The math equation (e.g., id = 42)

    FilterNode(std::shared_ptr<PlanNode> childNode, std::shared_ptr<Expression> pred)
        : child(std::move(childNode)), predicate(std::move(pred)) {}

    std::string toString() const override {
        return "Filter(predicate: " + predicate->toString() + ")\n  -> " + child->toString();
    }
};

// 5. AGGREGATE: Groups rows together and computes functions like SUM()
class AggregateNode : public PlanNode {
public:
    std::shared_ptr<PlanNode> child; // The data source (e.g., a Scan or Filter)
    std::vector<std::shared_ptr<Expression>> groupBys; // Columns to group by
    std::vector<std::shared_ptr<Expression>> aggregates; // Functions to compute

    AggregateNode(std::shared_ptr<PlanNode> childNode, 
                  std::vector<std::shared_ptr<Expression>> gb,
                  std::vector<std::shared_ptr<Expression>> agg)
        : child(std::move(childNode)), groupBys(std::move(gb)), aggregates(std::move(agg)) {}

    std::string toString() const override {
        std::string gbStr = "";
        for (size_t i = 0; i < groupBys.size(); ++i) {
            gbStr += groupBys[i]->toString();
            if (i < groupBys.size() - 1) gbStr += ", ";
        }
        
        std::string aggStr = "";
        for (size_t i = 0; i < aggregates.size(); ++i) {
            aggStr += aggregates[i]->toString();
            if (i < aggregates.size() - 1) aggStr += ", ";
        }
        
        return "Aggregate(Groups: [" + gbStr + "], Aggs: [" + aggStr + "])\n  -> " + child->toString();
    }
};

// 3. PROJECT: Selects specific columns
class ProjectNode : public PlanNode {
public:
    std::shared_ptr<PlanNode> child;                  // The data source (e.g., a Filter or Scan)
    std::vector<std::shared_ptr<Expression>> columns; // The columns to keep

    ProjectNode(std::shared_ptr<PlanNode> childNode, std::vector<std::shared_ptr<Expression>> cols)
        : child(std::move(childNode)), columns(std::move(cols)) {}

    std::string toString() const override {
        std::string cols = "";
        for (size_t i = 0; i < columns.size(); ++i) {
            cols += columns[i]->toString();
            if (i < columns.size() - 1) cols += ", ";
        }
        return "Project(columns: [" + cols + "])\n  -> " + child->toString();
    }
};

// 4. NESTED LOOP JOIN: Combines two data sources based on a condition
class NestedLoopJoinNode : public PlanNode {
public:
    std::shared_ptr<PlanNode> left_child;  // The main table (e.g., users)
    std::shared_ptr<PlanNode> right_child; // The joined table (e.g., orders)
    std::shared_ptr<Expression> predicate; // The ON condition (e.g., users.id = orders.user_id)

    NestedLoopJoinNode(std::shared_ptr<PlanNode> left, 
                       std::shared_ptr<PlanNode> right, 
                       std::shared_ptr<Expression> pred)
        : left_child(std::move(left)), right_child(std::move(right)), predicate(std::move(pred)) {}

    std::string toString() const override {
        return "NestedLoopJoin(ON " + predicate->toString() + ")\n  -> Left: " + left_child->toString() + "\n  -> Right: " + right_child->toString();
    }
};

// 4b. HASH JOIN: Optimized join for large datasets
class HashJoinNode : public PlanNode {
public:
    std::shared_ptr<PlanNode> left_child;
    std::shared_ptr<PlanNode> right_child;
    std::shared_ptr<Expression> predicate;
    size_t estimated_cost; // NEW: The optimizer will stamp the cost here!

    HashJoinNode(std::shared_ptr<PlanNode> left, 
                 std::shared_ptr<PlanNode> right, 
                 std::shared_ptr<Expression> pred,
                 size_t cost)
        : left_child(std::move(left)), right_child(std::move(right)), 
          predicate(std::move(pred)), estimated_cost(cost) {}

    std::string toString() const override {
        return "HashJoin(ON " + predicate->toString() + ") [Cost: " + std::to_string(estimated_cost) + "]\n  -> Left: " + left_child->toString() + "\n  -> Right: " + right_child->toString();
    }
};

// NEW: Specialized Time-Series Scan Node
class TickScanNode : public PlanNode {
public:
    std::string tableName;
    uint64_t start_time;
    uint64_t end_time;

    TickScanNode(std::string table, uint64_t start, uint64_t end)
        : tableName(std::move(table)), start_time(start), end_time(end) {}

    std::string toString() const override {
        return "TickScan(table: " + tableName + 
               ", time_range: [" + std::to_string(start_time) + ", " + std::to_string(end_time) + "])";
    }
};

// NEW: Specialized Logical Node for B+Tree Range Scans
class IndexRangeScanNode : public PlanNode {
public:
    std::string tableName;
    std::string columnName;
    int64_t start_val;
    int64_t end_val;

    IndexRangeScanNode(std::string table, std::string column, int64_t start, int64_t end)
        : tableName(std::move(table)), columnName(std::move(column)), start_val(start), end_val(end) {}

    std::string toString() const override {
        return "IndexRangeScan(table: " + tableName + 
               ", column: " + columnName + 
               ", range: [" + std::to_string(start_val) + ", " + std::to_string(end_val) + "])";
    }
};

} // namespace quill