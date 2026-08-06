#pragma once

#include "planner/LogicalPlan.h"
#include "catalog/Catalog.h"
#include <memory>

namespace quill {

class Optimizer {
public:
    // The optimizer now requires access to the Catalog to make cost decisions
    explicit Optimizer(std::shared_ptr<Catalog> catalog) : catalog_(std::move(catalog)) {}

    std::shared_ptr<PlanNode> optimize(std::shared_ptr<PlanNode> plan);

private:
    std::shared_ptr<Catalog> catalog_;

    std::shared_ptr<PlanNode> applyPredicatePushdown(std::shared_ptr<PlanNode> plan);
    std::shared_ptr<PlanNode> applyTimeSeriesPushdown(std::shared_ptr<PlanNode> plan);
    std::shared_ptr<PlanNode> applyCostBasedJoinSelection(std::shared_ptr<PlanNode> plan);

    // NEW: Replaces Filter + SeqScan with IndexScan if an index exists
    std::shared_ptr<PlanNode> applyIndexSelection(std::shared_ptr<PlanNode> plan);
};

} // namespace quill