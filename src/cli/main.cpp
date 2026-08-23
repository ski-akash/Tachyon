#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "planner/Planner.h"
#include "optimizer/Optimizer.h"
#include "catalog/Catalog.h"
#include "executor/PlanCompiler.h"
#include <iostream>
#include <unordered_map>

int main() {
    std::cout << "=== QuillDB Automated Index Selection Test ===\n\n";

    // 1. Build a small in-memory "users" table
    auto users = std::make_shared<quill::Table>("users", std::vector<std::string>{"id", "name"});
    for (int i = 0; i < 100; ++i) {
        users->insert({std::to_string(i), "User_" + std::to_string(i)});
    }
    users->createIndex("id"); // Backs the IndexScanExecutor's O(1) lookup

    // 2. Initialize Catalog with Statistics and Indexes
    auto catalog = std::make_shared<quill::Catalog>();
    catalog->setTableRowCount("users", users->num_rows_);

    // Register a hash index on the column the demo query actually filters on,
    // so the optimizer's automatic index selection rule has something to find.
    catalog->createIndex("users", "id", quill::IndexType::HASH);

    // 3. Front-End: Lex & Parse
    std::string sql = "EXPLAIN SELECT name FROM users WHERE id = 42;";
    std::cout << "Raw Query: " << sql << "\n\n";

    quill::Lexer lexer(sql);
    quill::Parser parser(std::move(lexer));
    auto statements = parser.parse();

    auto explainStmt = std::dynamic_pointer_cast<quill::ExplainStatement>(statements[0]);
    std::shared_ptr<quill::Statement> targetStmt = explainStmt ? explainStmt->statement : statements[0];

    // 4. Planner (Generates a naive Filter -> SeqScan plan)
    quill::Planner planner;
    auto logicalPlan = planner.createPlan(targetStmt);

    std::cout << "--- BEFORE OPTIMIZATION (Naive Plan) ---\n";
    std::cout << logicalPlan->toString() << "\n\n";

    // 5. Optimizer (Should automatically rewrite it to an IndexScan)
    quill::Optimizer optimizer(catalog);
    auto optimizedPlan = optimizer.optimize(logicalPlan);

    std::cout << "--- AFTER OPTIMIZATION (Index Selection) ---\n";
    std::cout << optimizedPlan->toString() << "\n\n";

    // 6. Compile the optimized plan into a runnable Executor tree and
    //    actually execute it - EXPLAIN prints the plan first, then runs it,
    //    the same way Postgres's EXPLAIN ANALYZE does.
    quill::PlanCompiler compiler({{"users", users}});
    auto executor = compiler.compile(optimizedPlan);

    std::cout << "--- QUERY RESULTS ---\n";
    executor->init();
    quill::Chunk chunk;
    size_t total_rows = 0;
    while (executor->next(chunk)) {
        for (size_t r = 0; r < chunk.size; ++r) {
            for (size_t c = 0; c < chunk.columns.size(); ++c) {
                std::cout << chunk.columns[c][r] << (c + 1 < chunk.columns.size() ? " | " : "");
            }
            std::cout << "\n";
        }
        total_rows += chunk.size;
    }
    std::cout << "(" << total_rows << " row" << (total_rows == 1 ? "" : "s") << ")\n";

    return 0;
}