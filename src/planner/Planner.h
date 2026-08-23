#pragma once

#include "ast/AST.h"
#include "planner/LogicalPlan.h"
#include <memory>
#include <stdexcept>

namespace tachyon {

class Planner {
public:
    // Entry point: takes a Statement and returns a Logical Plan
    std::shared_ptr<PlanNode> createPlan(const std::shared_ptr<Statement>& stmt);

private:
    // Specific planner for SELECT statements
    std::shared_ptr<PlanNode> planSelect(const std::shared_ptr<SelectStatement>& stmt);
};

} // namespace tachyon