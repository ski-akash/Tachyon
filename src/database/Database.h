#pragma once

#include "ast/AST.h"
#include "catalog/Catalog.h"
#include "storage/Storage.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tachyon {

// The result of executing one statement: either a status message (CREATE /
// INSERT / a parse or runtime error) or a set of result rows (SELECT /
// EXPLAIN, which also carries the plan text in `message`).
struct QueryResult {
    bool ok = true;
    std::string message;
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
    bool has_rows = false;
};

struct TableInfo {
    std::string name;
    std::vector<std::string> columns;
    size_t row_count;
};

// Owns the set of tables a session has created, and is the single entry
// point that turns raw SQL text into a QueryResult - the piece a REPL (or
// any other front end) drives. CREATE TABLE / INSERT are executed directly
// against the table registry; SELECT / EXPLAIN go through the full
// Lexer -> Parser -> Planner -> Optimizer -> PlanCompiler -> Executor
// pipeline built up in earlier commits.
class Database {
public:
    Database();

    // Executes a single SQL statement (a trailing ';' is expected but not
    // required) and returns its result. Only the first statement in `sql`
    // is executed; feed the REPL one statement per call.
    QueryResult execute(const std::string& sql);

    std::vector<TableInfo> listTables() const;
    std::shared_ptr<Table> getTable(const std::string& name) const;

private:
    std::unordered_map<std::string, std::shared_ptr<Table>> tables_;
    std::shared_ptr<Catalog> catalog_;

    // "ticks" isn't a Table in `tables_` - it's a memory-mapped tick file
    // (see seedTickData) - so its row count is tracked separately for
    // listTables().
    size_t tick_row_count_ = 0;

    QueryResult executeCreateTable(const std::shared_ptr<CreateTableStatement>& stmt);
    QueryResult executeInsert(const std::shared_ptr<InsertStatement>& stmt);
    QueryResult executeSelect(const std::shared_ptr<SelectStatement>& stmt, bool explain);

    void seedDummyData();
    void seedTickData();
};

} // namespace tachyon
