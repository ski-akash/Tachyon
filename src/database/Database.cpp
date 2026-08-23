#include "database/Database.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "planner/Planner.h"
#include "optimizer/Optimizer.h"
#include "executor/PlanCompiler.h"
#include "storage/Tick.h"
#include "storage/TickStore.h"
#include "storage/TickFlusher.h"
#include <stdexcept>

namespace tachyon {

Database::Database() : catalog_(std::make_shared<Catalog>()) {
    seedDummyData();
    seedTickData();
}

// Writes a small synthetic "ticks_data.bin" so the time-series pipeline
// (TickScanExecutor / TIME_BUCKET / VWAP) has something to query without
// requiring a separate ingestion step first. Not a real Table - it doesn't
// go in `tables_` - since it's read via memory-mapped file rather than the
// in-memory columnar storage the rest of Database manages.
void Database::seedTickData() {
    TickStore store;
    uint16_t aapl = store.getOrInternSymbol("AAPL");

    uint64_t timestamp_ns = 1704067200000000000ULL; // 2024-01-01 00:00:00 UTC
    int64_t price = 1500000;                        // $150.00 (price * 10000)

    // 20 minutes of ticks, a few per second, with a small random-walk in
    // price so VWAP/OHLCV buckets actually differ from one another.
    for (uint32_t i = 0; i < 20000; ++i) {
        timestamp_ns += 50000000 + (i % 7) * 1000000; // ~50-56ms apart
        price += (static_cast<int64_t>(i % 13) - 6) * 50; // wiggle +/- $0.03

        Tick t;
        t.timestamp_ns = timestamp_ns;
        t.price = price;
        t.size = 100 + (i % 50) * 10;
        t.seq_num = i;
        t.symbol_id = aapl;
        t.side = static_cast<uint8_t>(i % 2);
        store.append(t);
    }

    TickFlusher::flushToDisk(store, "ticks_data.bin");
    tick_row_count_ = store.size();
}

void Database::seedDummyData() {
    // "employees" - a small table with an obvious WHERE/index target (department)
    auto employees = std::make_shared<Table>("employees",
        std::vector<std::string>{"id", "name", "department", "salary"});
    employees->insert({"1", "Alice", "Engineering", "95000"});
    employees->insert({"2", "Bob", "Sales", "72000"});
    employees->insert({"3", "Carol", "Engineering", "105000"});
    employees->insert({"4", "Dave", "Marketing", "68000"});
    employees->insert({"5", "Eve", "Sales", "81000"});
    employees->insert({"6", "Frank", "Engineering", "99000"});
    tables_["employees"] = employees;
    catalog_->setTableRowCount("employees", employees->num_rows_);

    // "orders" - references employees.id, so JOIN queries have something real to do
    auto orders = std::make_shared<Table>("orders",
        std::vector<std::string>{"order_id", "employee_id", "amount", "status"});
    orders->insert({"101", "1", "250", "shipped"});
    orders->insert({"102", "2", "125", "pending"});
    orders->insert({"103", "3", "999", "shipped"});
    orders->insert({"104", "1", "60", "cancelled"});
    orders->insert({"105", "5", "430", "shipped"});
    orders->insert({"106", "4", "75", "pending"});
    tables_["orders"] = orders;
    catalog_->setTableRowCount("orders", orders->num_rows_);
}

std::vector<TableInfo> Database::listTables() const {
    std::vector<TableInfo> result;
    for (const auto& [name, table] : tables_) {
        result.push_back({name, table->column_names, table->num_rows_});
    }
    result.push_back({"ticks",
                       {"time", "price", "size", "symbol_id", "side"},
                       tick_row_count_});
    return result;
}

std::shared_ptr<Table> Database::getTable(const std::string& name) const {
    auto it = tables_.find(name);
    return it != tables_.end() ? it->second : nullptr;
}

QueryResult Database::execute(const std::string& sql) {
    QueryResult result;
    try {
        Lexer lexer(sql);
        Parser parser(std::move(lexer));
        auto statements = parser.parse();

        if (statements.empty()) {
            result.ok = false;
            result.message = "Nothing to execute (empty or unrecognized statement).";
            return result;
        }

        auto stmt = statements[0];

        if (auto createStmt = std::dynamic_pointer_cast<CreateTableStatement>(stmt)) {
            return executeCreateTable(createStmt);
        }
        if (auto insertStmt = std::dynamic_pointer_cast<InsertStatement>(stmt)) {
            return executeInsert(insertStmt);
        }
        if (auto explainStmt = std::dynamic_pointer_cast<ExplainStatement>(stmt)) {
            auto selectStmt = std::dynamic_pointer_cast<SelectStatement>(explainStmt->statement);
            if (!selectStmt) {
                result.ok = false;
                result.message = "EXPLAIN only supports SELECT statements.";
                return result;
            }
            return executeSelect(selectStmt, /*explain=*/true);
        }
        if (auto selectStmt = std::dynamic_pointer_cast<SelectStatement>(stmt)) {
            return executeSelect(selectStmt, /*explain=*/false);
        }

        result.ok = false;
        result.message = "Unsupported statement type.";
        return result;

    } catch (const std::exception& e) {
        result.ok = false;
        result.message = std::string("Error: ") + e.what();
        return result;
    }
}

QueryResult Database::executeCreateTable(const std::shared_ptr<CreateTableStatement>& stmt) {
    QueryResult result;
    if (tables_.count(stmt->tableName)) {
        result.ok = false;
        result.message = "Error: table '" + stmt->tableName + "' already exists.";
        return result;
    }

    auto table = std::make_shared<Table>(stmt->tableName, stmt->columns);
    tables_[stmt->tableName] = table;
    catalog_->setTableRowCount(stmt->tableName, 0);

    result.message = "Table '" + stmt->tableName + "' created.";
    return result;
}

QueryResult Database::executeInsert(const std::shared_ptr<InsertStatement>& stmt) {
    QueryResult result;
    auto it = tables_.find(stmt->tableName);
    if (it == tables_.end()) {
        result.ok = false;
        result.message = "Error: table '" + stmt->tableName + "' does not exist.";
        return result;
    }

    for (const auto& row : stmt->rows) {
        it->second->insert(row);
    }
    catalog_->setTableRowCount(stmt->tableName, it->second->num_rows_);

    result.message = std::to_string(stmt->rows.size()) + " row(s) inserted into '" +
                      stmt->tableName + "'.";
    return result;
}

QueryResult Database::executeSelect(const std::shared_ptr<SelectStatement>& stmt, bool explain) {
    QueryResult result;

    // Expand a bare "SELECT *" into the table's actual columns now that we
    // know the table name; the planner/executor only ever work with
    // explicit column identifiers.
    if (stmt->columns.size() == 1) {
        auto ident = std::dynamic_pointer_cast<Identifier>(stmt->columns[0]);
        if (ident && ident->value == "*") {
            std::vector<std::string> columnNames;
            if (stmt->tableName == "ticks") {
                // "ticks" is a memory-mapped tick file, not an entry in
                // `tables_` - its column layout is fixed by TickScanExecutor.
                columnNames = {"time", "price", "size", "symbol_id", "side"};
            } else {
                auto it = tables_.find(stmt->tableName);
                if (it == tables_.end()) {
                    result.ok = false;
                    result.message = "Error: table '" + stmt->tableName + "' does not exist.";
                    return result;
                }
                columnNames = it->second->column_names;
            }
            stmt->columns.clear();
            for (const auto& name : columnNames) {
                stmt->columns.push_back(std::make_shared<Identifier>(name));
            }
        }
    }

    Planner planner;
    auto logicalPlan = planner.createPlan(stmt);
    std::string beforeText = logicalPlan->toString();

    Optimizer optimizer(catalog_);
    auto optimizedPlan = optimizer.optimize(logicalPlan);

    if (explain) {
        result.message = "--- Before optimization ---\n" + beforeText +
                          "\n\n--- After optimization ---\n" + optimizedPlan->toString();
    }

    PlanCompiler compiler(tables_);
    auto executor = compiler.compile(optimizedPlan);

    for (const auto& col : stmt->columns) {
        result.columns.push_back(col->toString());
    }

    executor->init();
    Chunk chunk;
    while (executor->next(chunk)) {
        for (size_t r = 0; r < chunk.size; ++r) {
            std::vector<std::string> row;
            if (chunk.is_numeric) {
                // The time-series / aggregate fast path never touches the
                // legacy string columns - convert its int64 output instead.
                for (size_t c = 0; c < chunk.numeric_columns.size(); ++c) {
                    row.push_back(std::to_string(chunk.numeric_columns[c][r]));
                }
            } else {
                for (size_t c = 0; c < chunk.columns.size(); ++c) {
                    row.push_back(chunk.columns[c][r]);
                }
            }
            result.rows.push_back(std::move(row));
        }
    }

    result.has_rows = true;
    if (!explain) {
        result.message = std::to_string(result.rows.size()) + " row(s)";
    }
    return result;
}

} // namespace tachyon
