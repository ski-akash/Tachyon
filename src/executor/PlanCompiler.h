#pragma once

#include "planner/LogicalPlan.h"
#include "executor/Executor.h"
#include "storage/Storage.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace quill {

// Translates an optimized logical plan (the tree of PlanNode produced by the
// Planner/Optimizer) into a runnable Executor tree, resolving table names
// against an in-memory table registry. This is the piece that was missing
// end-to-end: previously every CLI/benchmark entry point hand-wired its own
// Executor tree instead of running whatever the optimizer actually decided.
class PlanCompiler {
public:
    explicit PlanCompiler(std::unordered_map<std::string, std::shared_ptr<Table>> tables)
        : tables_(std::move(tables)) {}

    // Compiles a plan into a runnable Executor. Throws std::runtime_error for
    // plan nodes that don't have a physical executor wired up yet
    // (TickScanNode / IndexRangeScanNode - see IndexRangeScanExecutor.h).
    std::unique_ptr<Executor> compile(const std::shared_ptr<PlanNode>& plan);

private:
    std::unordered_map<std::string, std::shared_ptr<Table>> tables_;

    // A compiled subtree carries both its Executor and the schema (column
    // name -> index mapping) that its output rows conform to, so a parent
    // node (Filter/Project/Aggregate) can resolve column references.
    struct Compiled {
        std::unique_ptr<Executor> executor;
        std::shared_ptr<Table> schema;
    };

    Compiled compileNode(const std::shared_ptr<PlanNode>& plan);
    std::shared_ptr<Table> lookupTable(const std::string& name);
};

} // namespace quill
