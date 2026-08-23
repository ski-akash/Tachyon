#include "optimizer/Optimizer.h"

namespace quill {

std::shared_ptr<PlanNode> Optimizer::optimize(std::shared_ptr<PlanNode> plan) {
    plan = applyPredicatePushdown(plan);
    plan = applyCostBasedJoinSelection(plan);
    plan = applyIndexSelection(plan); 
    plan = applyTimeSeriesPushdown(plan);
    return plan;
}

std::shared_ptr<PlanNode> Optimizer::applyPredicatePushdown(std::shared_ptr<PlanNode> plan) {
    if (!plan) return nullptr;

    // 1. If it's a Project, recursively optimize its child
    if (auto projectNode = std::dynamic_pointer_cast<ProjectNode>(plan)) {
        projectNode->child = applyPredicatePushdown(projectNode->child);
        return projectNode;
    } 
    // 2. If it's a Filter, check if we can push it down!
    else if (auto filterNode = std::dynamic_pointer_cast<FilterNode>(plan)) {
        
        // First, recursively optimize the child
        filterNode->child = applyPredicatePushdown(filterNode->child);

        // THE PUSHDOWN RULE:
        // If this Filter is sitting on top of a Project, swap them!
        // (Filter -> Project -> Scan) becomes (Project -> Filter -> Scan)
        if (auto childProject = std::dynamic_pointer_cast<ProjectNode>(filterNode->child)) {

            // Rewire the tree
            filterNode->child = childProject->child; // Filter now points to Scan
            childProject->child = filterNode;        // Project now points to Filter

            return childProject; // The Project is now the new top of this sub-tree
        }

        // B+Tree Index Range Scan Pushdown
        if (auto scanNode = std::dynamic_pointer_cast<SeqScanNode>(filterNode->child)) {
            // Check if the predicate is a BETWEEN expression
            if (auto betweenExpr = std::dynamic_pointer_cast<BetweenExpression>(filterNode->predicate)) {
                if (auto leftIdent = std::dynamic_pointer_cast<Identifier>(betweenExpr->left)) {

                    std::string colName = leftIdent->value;

                    // 1. Ask the Catalog if a B+Tree exists for this column!
                    if (catalog_ && catalog_->hasIndex(scanNode->tableName, colName, quill::IndexType::BTREE)) {

                        // 2. Extract the bounds (numeric literals parse as NumberLiteral)
                        auto lowerNum = std::dynamic_pointer_cast<NumberLiteral>(betweenExpr->lower);
                        auto upperNum = std::dynamic_pointer_cast<NumberLiteral>(betweenExpr->upper);

                        if (lowerNum && upperNum) {
                            // 3. REWRITE THE AST: Drop the Filter and SeqScan, replace with IndexRangeScan!
                            return std::make_shared<IndexRangeScanNode>(
                                scanNode->tableName,
                                colName,
                                std::stoll(lowerNum->value),
                                std::stoll(upperNum->value)
                            );
                        }
                    }
                }
            }
        }

        return filterNode;
    }
    // 3. Traverse through Joins
    else if (auto joinNode = std::dynamic_pointer_cast<NestedLoopJoinNode>(plan)) {
        joinNode->left_child = applyPredicatePushdown(joinNode->left_child);
        joinNode->right_child = applyPredicatePushdown(joinNode->right_child);
        return joinNode;
    }
    // 4. Traverse through Aggregates
    else if (auto aggNode = std::dynamic_pointer_cast<AggregateNode>(plan)) {
        aggNode->child = applyPredicatePushdown(aggNode->child);
        return aggNode;
    }

    // Base case (like a Scan Node)
    return plan;
}

std::shared_ptr<PlanNode> Optimizer::applyCostBasedJoinSelection(std::shared_ptr<PlanNode> plan) {
    if (!plan) return nullptr;

    // Traverse the tree recursively
    if (auto projectNode = std::dynamic_pointer_cast<ProjectNode>(plan)) {
        projectNode->child = applyCostBasedJoinSelection(projectNode->child);
    } else if (auto filterNode = std::dynamic_pointer_cast<FilterNode>(plan)) {
        filterNode->child = applyCostBasedJoinSelection(filterNode->child);
    } else if (auto aggNode = std::dynamic_pointer_cast<AggregateNode>(plan)) {
        aggNode->child = applyCostBasedJoinSelection(aggNode->child);
    } 
    // THE CBO LOGIC: If we find a Nested-Loop Join, let's calculate its cost!
    else if (auto joinNode = std::dynamic_pointer_cast<NestedLoopJoinNode>(plan)) {
        joinNode->left_child = applyCostBasedJoinSelection(joinNode->left_child);
        joinNode->right_child = applyCostBasedJoinSelection(joinNode->right_child);

        size_t left_rows = 1000;
        size_t right_rows = 1000;

        // If the children are Scans, ask the Catalog how big the tables are
        if (auto leftScan = std::dynamic_pointer_cast<SeqScanNode>(joinNode->left_child)) {
            left_rows = catalog_->getTableRowCount(leftScan->tableName);
        }
        if (auto rightScan = std::dynamic_pointer_cast<SeqScanNode>(joinNode->right_child)) {
            right_rows = catalog_->getTableRowCount(rightScan->tableName);
        }

        size_t nested_loop_cost = left_rows * right_rows;
        size_t hash_join_cost = left_rows + right_rows;

        // The Threshold: If Hash Join is significantly cheaper, rewrite the AST node!
        // (We require right_rows > 5 because Hash Joins have initial overhead to build the map)
        if (hash_join_cost < nested_loop_cost && right_rows > 5) {
            return std::make_shared<HashJoinNode>(
                joinNode->left_child, 
                joinNode->right_child, 
                joinNode->predicate, 
                hash_join_cost
            );
        }
    }

    return plan;
}

std::shared_ptr<PlanNode> Optimizer::applyIndexSelection(std::shared_ptr<PlanNode> plan) {
    if (!plan) return nullptr;

    // Traverse the tree recursively
    if (auto projectNode = std::dynamic_pointer_cast<ProjectNode>(plan)) {
        projectNode->child = applyIndexSelection(projectNode->child);
    } else if (auto aggNode = std::dynamic_pointer_cast<AggregateNode>(plan)) {
        aggNode->child = applyIndexSelection(aggNode->child);
    } else if (auto joinNode = std::dynamic_pointer_cast<NestedLoopJoinNode>(plan)) {
        joinNode->left_child = applyIndexSelection(joinNode->left_child);
        joinNode->right_child = applyIndexSelection(joinNode->right_child);
    } else if (auto hashJoinNode = std::dynamic_pointer_cast<HashJoinNode>(plan)) {
        hashJoinNode->left_child = applyIndexSelection(hashJoinNode->left_child);
        hashJoinNode->right_child = applyIndexSelection(hashJoinNode->right_child);
    } 
    // THE RULE: If we find a Filter...
    else if (auto filterNode = std::dynamic_pointer_cast<FilterNode>(plan)) {
        filterNode->child = applyIndexSelection(filterNode->child);

        // ... sitting directly on top of a SeqScan ...
        if (auto scanNode = std::dynamic_pointer_cast<SeqScanNode>(filterNode->child)) {
            
            // ... check if it's a basic equality condition (e.g., id = 42)
            if (auto binaryExpr = std::dynamic_pointer_cast<BinaryExpression>(filterNode->predicate)) {
                if (binaryExpr->op == "=") {
                    auto leftIdent = std::dynamic_pointer_cast<Identifier>(binaryExpr->left);
                    auto rightLiteral = std::dynamic_pointer_cast<NumberLiteral>(binaryExpr->right);

                    if (leftIdent && rightLiteral) {
                        std::string colName = leftIdent->value;
                        if (colName.find('.') != std::string::npos) {
                            colName = colName.substr(colName.find('.') + 1); // Strip "users." prefix
                        }

                        // THE MAGIC: Ask the catalog if this column has an index!
                        if (catalog_->hasIndex(scanNode->tableName, colName, quill::IndexType::HASH)) {
                            // YES! Rewrite the tree: Replace Filter+SeqScan with IndexScan
                            return std::make_shared<IndexScanNode>(
                                scanNode->tableName, colName, rightLiteral->value
                            );
                        }
                    }
                }
            }
        }
    }

    return plan;
}

std::shared_ptr<PlanNode> Optimizer::applyTimeSeriesPushdown(std::shared_ptr<PlanNode> plan) {
    if (!plan) return nullptr;

    // Recursive traversal (Join, Aggregate, Project...)
    if (auto projectNode = std::dynamic_pointer_cast<ProjectNode>(plan)) {
        projectNode->child = applyTimeSeriesPushdown(projectNode->child);
    } else if (auto aggNode = std::dynamic_pointer_cast<AggregateNode>(plan)) {
        aggNode->child = applyTimeSeriesPushdown(aggNode->child);
    }
    
    // THE PUSHDOWN RULE
    if (auto filterNode = std::dynamic_pointer_cast<FilterNode>(plan)) {
        filterNode->child = applyTimeSeriesPushdown(filterNode->child);

        // If filtering on a SeqScan of the "ticks" table...
        if (auto scanNode = std::dynamic_pointer_cast<SeqScanNode>(filterNode->child)) {
            if (scanNode->tableName == "ticks") {
                
                // Check if the predicate is our new BETWEEN expression
                if (auto betweenExpr = std::dynamic_pointer_cast<BetweenExpression>(filterNode->predicate)) {

                    auto lowerNum = std::dynamic_pointer_cast<NumberLiteral>(betweenExpr->lower);
                    auto upperNum = std::dynamic_pointer_cast<NumberLiteral>(betweenExpr->upper);
                    
                    if (lowerNum && upperNum) {
                        // REWRITE: Drop the Filter, convert to TickScan!
                        return std::make_shared<TickScanNode>(
                            "ticks", std::stoull(lowerNum->value), std::stoull(upperNum->value)
                        );
                    }
                }
            }
        }
    }
    return plan;
}

} // namespace quill