#include "database/Database.h"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

using namespace tachyon;

namespace {

void printTables(const Database& db) {
    auto tables = db.listTables();
    std::cout << "Tables:\n";
    for (const auto& t : tables) {
        std::cout << "  " << t.name << " (" << t.row_count << " row"
                  << (t.row_count == 1 ? "" : "s") << "): ";
        for (size_t i = 0; i < t.columns.size(); ++i) {
            std::cout << t.columns[i] << (i + 1 < t.columns.size() ? ", " : "");
        }
        std::cout << "\n";
    }
}

// Renders result rows as a simple aligned ASCII table, sqlite3-CLI style.
void printResultTable(const QueryResult& result) {
    if (result.rows.empty()) {
        std::cout << "(0 rows)\n";
        return;
    }

    std::vector<size_t> widths(result.columns.size());
    for (size_t c = 0; c < result.columns.size(); ++c) {
        widths[c] = result.columns[c].size();
    }
    for (const auto& row : result.rows) {
        for (size_t c = 0; c < row.size() && c < widths.size(); ++c) {
            widths[c] = std::max(widths[c], row[c].size());
        }
    }

    auto printSeparator = [&]() {
        for (size_t c = 0; c < widths.size(); ++c) {
            std::cout << "+" << std::string(widths[c] + 2, '-');
        }
        std::cout << "+\n";
    };

    printSeparator();
    std::cout << "|";
    for (size_t c = 0; c < result.columns.size(); ++c) {
        std::cout << " " << result.columns[c]
                  << std::string(widths[c] - result.columns[c].size(), ' ') << " |";
    }
    std::cout << "\n";
    printSeparator();

    for (const auto& row : result.rows) {
        std::cout << "|";
        for (size_t c = 0; c < row.size(); ++c) {
            std::cout << " " << row[c] << std::string(widths[c] - row[c].size(), ' ') << " |";
        }
        std::cout << "\n";
    }
    printSeparator();
}

} // namespace

int main() {
    Database db;

    std::cout << "Tachyon - Time Series Query Engine\n";
    std::cout << "Type SQL statements ending in ';', or one of: .tables, .schema <table>, exit\n\n";
    printTables(db);
    std::cout << "\nTry: SELECT name FROM employees WHERE department = 'Engineering';\n\n";

    std::string line;
    while (true) {
        std::cout << "tachyon> ";
        if (!std::getline(std::cin, line)) break;

        // Trim leading/trailing whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        if (line == "exit" || line == "quit" || line == ".exit" || line == ".quit") break;

        if (line == ".tables") {
            printTables(db);
            continue;
        }
        if (line.rfind(".schema ", 0) == 0) {
            std::string tableName = line.substr(8);
            auto table = db.getTable(tableName);
            if (!table) {
                std::cout << "No such table: " << tableName << "\n";
            } else {
                std::cout << tableName << "(";
                for (size_t i = 0; i < table->column_names.size(); ++i) {
                    std::cout << table->column_names[i]
                              << (i + 1 < table->column_names.size() ? ", " : "");
                }
                std::cout << ")\n";
            }
            continue;
        }

        auto result = db.execute(line);

        if (!result.ok) {
            std::cout << result.message << "\n";
        } else if (result.has_rows) {
            if (!result.message.empty() && result.message.rfind("---", 0) == 0) {
                std::cout << result.message << "\n\n"; // EXPLAIN's before/after plan text
            }
            printResultTable(result);
            if (result.message.rfind("---", 0) != 0) {
                std::cout << result.message << "\n"; // "N row(s)"
            }
        } else {
            std::cout << result.message << "\n";
        }
    }

    std::cout << "Bye.\n";
    return 0;
}
