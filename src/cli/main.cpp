#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "planner/Planner.h"
#include "optimizer/Optimizer.h"
#include "catalog/Catalog.h"
#include <iostream>

int main() {
    std::cout << "=== QuillDB Automated Index Selection Test ===\n\n";

    // 1. Initialize Catalog with Statistics and Indexes
    auto catalog = std::make_shared<quill::Catalog>();
    catalog->setTableRowCount("users", 1000000); // 1 Million rows

    // Register a hash index on the column the demo query actually filters on,
    // so the optimizer's automatic index selection rule has something to find.
    catalog->createIndex("users", "id", quill::IndexType::HASH);

    // 2. Front-End: Lex & Parse
    std::string sql = "EXPLAIN SELECT name FROM users WHERE id = 42;"; 
    std::cout << "Raw Query: " << sql << "\n\n";
    
    quill::Lexer lexer(sql);
    quill::Parser parser(std::move(lexer));
    auto statements = parser.parse();

    auto explainStmt = std::dynamic_pointer_cast<quill::ExplainStatement>(statements[0]);
    std::shared_ptr<quill::Statement> targetStmt = explainStmt ? explainStmt->statement : statements[0];

    // 3. Planner (Generates a naive Filter -> SeqScan plan)
    quill::Planner planner;
    auto logicalPlan = planner.createPlan(targetStmt);

    std::cout << "--- BEFORE OPTIMIZATION (Naive Plan) ---\n";
    std::cout << logicalPlan->toString() << "\n\n";

    // 4. Optimizer (Should automatically rewrite it to an IndexScan)
    quill::Optimizer optimizer(catalog);
    auto optimizedPlan = optimizer.optimize(logicalPlan);

    if (explainStmt) {
        std::cout << "--- AFTER OPTIMIZATION (Index Selection) ---\n";
        std::cout << optimizedPlan->toString() << "\n\n";
    }

    return 0;
}