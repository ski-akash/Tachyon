#include "executor/PlanCompiler.h"
#include "executor/IndexRangeScanExecutor.h"
#include <stdexcept>

namespace quill {

std::shared_ptr<Table> PlanCompiler::lookupTable(const std::string& name) {
    auto it = tables_.find(name);
    if (it == tables_.end()) {
        throw std::runtime_error("PlanCompiler: unknown table '" + name + "'");
    }
    return it->second;
}

std::unique_ptr<Executor> PlanCompiler::compile(const std::shared_ptr<PlanNode>& plan) {
    return compileNode(plan).executor;
}

PlanCompiler::Compiled PlanCompiler::compileNode(const std::shared_ptr<PlanNode>& plan) {
    if (auto scanNode = std::dynamic_pointer_cast<SeqScanNode>(plan)) {
        auto table = lookupTable(scanNode->tableName);
        return {std::make_unique<SeqScanExecutor>(table), table};
    }

    if (auto indexScanNode = std::dynamic_pointer_cast<IndexScanNode>(plan)) {
        auto table = lookupTable(indexScanNode->tableName);
        return {std::make_unique<IndexScanExecutor>(table, indexScanNode->columnName, indexScanNode->lookupKey), table};
    }

    if (auto filterNode = std::dynamic_pointer_cast<FilterNode>(plan)) {
        auto child = compileNode(filterNode->child);
        auto schema = child.schema;
        return {std::make_unique<FilterExecutor>(std::move(child.executor), filterNode->predicate, schema), schema};
    }

    if (auto projectNode = std::dynamic_pointer_cast<ProjectNode>(plan)) {
        auto child = compileNode(projectNode->child);
        auto schema = child.schema;

        // The projected schema's column names are the raw column
        // expressions, resolved against the child schema when possible.
        std::vector<std::string> outNames;
        for (const auto& col : projectNode->columns) {
            outNames.push_back(col->toString());
        }
        auto projectedSchema = std::make_shared<Table>("projected", outNames);

        return {std::make_unique<ProjectExecutor>(std::move(child.executor), projectNode->columns, schema),
                projectedSchema};
    }

    if (auto aggNode = std::dynamic_pointer_cast<AggregateNode>(plan)) {
        auto child = compileNode(aggNode->child);
        auto schema = child.schema;
        auto aggSchema = std::make_shared<Table>("aggregated", std::vector<std::string>{"group_key", "agg_result"});
        return {std::make_unique<AggregateExecutor>(std::move(child.executor), aggNode->groupBys, aggNode->aggregates, schema),
                aggSchema};
    }

    if (auto joinNode = std::dynamic_pointer_cast<NestedLoopJoinNode>(plan)) {
        auto left = compileNode(joinNode->left_child);
        auto right = compileNode(joinNode->right_child);

        std::vector<std::string> mergedNames = left.schema->column_names;
        mergedNames.insert(mergedNames.end(), right.schema->column_names.begin(), right.schema->column_names.end());
        auto mergedSchema = std::make_shared<Table>("joined", mergedNames);

        auto leftSchema = left.schema, rightSchema = right.schema;
        return {std::make_unique<NestedLoopJoinExecutor>(std::move(left.executor), std::move(right.executor),
                                                          joinNode->predicate, leftSchema, rightSchema),
                mergedSchema};
    }

    if (auto hashJoinNode = std::dynamic_pointer_cast<HashJoinNode>(plan)) {
        auto left = compileNode(hashJoinNode->left_child);
        auto right = compileNode(hashJoinNode->right_child);

        std::vector<std::string> mergedNames = left.schema->column_names;
        mergedNames.insert(mergedNames.end(), right.schema->column_names.begin(), right.schema->column_names.end());
        auto mergedSchema = std::make_shared<Table>("joined", mergedNames);

        auto leftSchema = left.schema, rightSchema = right.schema;
        return {std::make_unique<HashJoinExecutor>(std::move(left.executor), std::move(right.executor),
                                                    hashJoinNode->predicate, leftSchema, rightSchema),
                mergedSchema};
    }

    if (auto rangeScanNode = std::dynamic_pointer_cast<IndexRangeScanNode>(plan)) {
        auto table = lookupTable(rangeScanNode->tableName);
        return {std::make_unique<IndexRangeScanExecutor>(table, rangeScanNode->columnName,
                                                          rangeScanNode->start_val, rangeScanNode->end_val),
                table};
    }

    if (std::dynamic_pointer_cast<TickScanNode>(plan)) {
        throw std::runtime_error(
            "PlanCompiler: TickScan has no physical executor wired up yet "
            "(see TickScanExecutor.h, which reads from a memory-mapped tick file "
            "rather than a Table and isn't part of this table-based pipeline)");
    }

    throw std::runtime_error("PlanCompiler: unrecognized plan node");
}

} // namespace quill
