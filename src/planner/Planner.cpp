#include "planner/Planner.h"

namespace tachyon {

std::shared_ptr<PlanNode> Planner::createPlan(const std::shared_ptr<Statement>& stmt) {
    // We use dynamic_cast to figure out what type of Statement the parser gave us
    auto selectStmt = std::dynamic_pointer_cast<SelectStatement>(stmt);
    
    if (selectStmt) {
        return planSelect(selectStmt);
    }
    
    throw std::runtime_error("Unsupported statement type in planner.");
}

std::shared_ptr<PlanNode> Planner::planSelect(const std::shared_ptr<SelectStatement>& stmt) {
    // 1. SCAN
    std::shared_ptr<PlanNode> root = std::make_shared<SeqScanNode>(stmt->tableName);

    // 2. JOIN
    for (const auto& join : stmt->joins) {
        auto right_scan = std::make_shared<SeqScanNode>(join->tableName);
        root = std::make_shared<NestedLoopJoinNode>(root, right_scan, join->condition);
    }

    // 3. FILTER
    if (stmt->whereClause != nullptr) {
        root = std::make_shared<FilterNode>(root, stmt->whereClause);
    }

    // NEW: 4. AGGREGATE
    bool hasAggregates = !stmt->groupBy.empty();
    std::vector<std::shared_ptr<Expression>> aggregates;
    
    // Check if any of our requested columns are actually function calls like SUM()
    for (const auto& col : stmt->columns) {
        if (std::dynamic_pointer_cast<FunctionCall>(col)) {
            hasAggregates = true;
            aggregates.push_back(col);
        }
    }

    if (hasAggregates) {
        // Wrap the current pipeline in an Aggregate node!
        root = std::make_shared<AggregateNode>(root, stmt->groupBy, aggregates);
    }

    // 5. PROJECT
    root = std::make_shared<ProjectNode>(root, stmt->columns);

    return root; 
}

} // namespace tachyon