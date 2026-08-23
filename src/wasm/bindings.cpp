// Emscripten bindings exposing tachyon::Database to JavaScript.
#include "database/Database.h"
#include <emscripten/bind.h>
#include <sstream>

using namespace tachyon;
using namespace emscripten;

namespace {

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

std::string resultToJson(const QueryResult& r) {
    std::ostringstream out;
    out << "{\"ok\":" << (r.ok ? "true" : "false")
        << ",\"message\":\"" << jsonEscape(r.message) << "\""
        << ",\"has_rows\":" << (r.has_rows ? "true" : "false")
        << ",\"columns\":[";
    for (size_t i = 0; i < r.columns.size(); ++i) {
        out << "\"" << jsonEscape(r.columns[i]) << "\"" << (i + 1 < r.columns.size() ? "," : "");
    }
    out << "],\"rows\":[";
    for (size_t i = 0; i < r.rows.size(); ++i) {
        out << "[";
        for (size_t j = 0; j < r.rows[i].size(); ++j) {
            out << "\"" << jsonEscape(r.rows[i][j]) << "\"" << (j + 1 < r.rows[i].size() ? "," : "");
        }
        out << "]" << (i + 1 < r.rows.size() ? "," : "");
    }
    out << "]}";
    return out.str();
}

std::string tablesToJson(const Database& db) {
    std::ostringstream out;
    out << "[";
    auto tables = db.listTables();
    for (size_t i = 0; i < tables.size(); ++i) {
        const auto& t = tables[i];
        out << "{\"name\":\"" << jsonEscape(t.name) << "\""
            << ",\"row_count\":" << t.row_count
            << ",\"columns\":[";
        for (size_t j = 0; j < t.columns.size(); ++j) {
            out << "\"" << jsonEscape(t.columns[j]) << "\"" << (j + 1 < t.columns.size() ? "," : "");
        }
        out << "]}" << (i + 1 < tables.size() ? "," : "");
    }
    out << "]";
    return out.str();
}

// Thin wrapper: JS talks to this, not Database directly, so the JSON
// boundary lives in one place and Database stays a pure C++ API.
class Engine {
public:
    std::string execute(const std::string& sql) {
        return resultToJson(db_.execute(sql));
    }

    std::string listTables() {
        return tablesToJson(db_);
    }

private:
    Database db_;
};

} // namespace

EMSCRIPTEN_BINDINGS(tachyon_module) {
    class_<Engine>("Engine")
        .constructor<>()
        .function("execute", &Engine::execute)
        .function("listTables", &Engine::listTables);
}
