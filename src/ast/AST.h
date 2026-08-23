#pragma once

#include <string>
#include <vector>
#include <memory>

namespace tachyon {

// ---------------------------------------------------------
// BASE INTERFACES
// ---------------------------------------------------------

// The root of all AST nodes
class Node {
public:
    virtual ~Node() = default;
    
    // Every node must be able to print itself for debugging
    virtual std::string toString() const = 0; 
};

// Base class for things that produce values (columns, numbers, math)
class Expression : public Node {
public:
    virtual ~Expression() = default;
};

// Base class for full commands (SELECT, INSERT, CREATE)
class Statement : public Node {
public:
    virtual ~Statement() = default;
};

// ---------------------------------------------------------
// SPECIFIC IMPLEMENTATIONS
// ---------------------------------------------------------

// Represents a column name or a table name
class Identifier : public Expression {
public:
    std::string value;

    explicit Identifier(std::string val) : value(std::move(val)) {}

    std::string toString() const override {
        return value;
    }
};

// NEW: Represents a numeric literal (e.g., 42)
class NumberLiteral : public Expression {
public:
    std::string value;
    explicit NumberLiteral(std::string val) : value(std::move(val)) {}
    std::string toString() const override { return value; }
};

// Represents a quoted string literal (e.g., 'Engineering')
class StringLiteral : public Expression {
public:
    std::string value;
    explicit StringLiteral(std::string val) : value(std::move(val)) {}
    std::string toString() const override { return "'" + value + "'"; }
};

// NEW: Represents operations like 'id = 42' or 'age > 18'
class BinaryExpression : public Expression {
public:
    std::shared_ptr<Expression> left;
    std::string op; // The operator (e.g., "=")
    std::shared_ptr<Expression> right;

    BinaryExpression(std::shared_ptr<Expression> l, std::string o, std::shared_ptr<Expression> r)
        : left(std::move(l)), op(std::move(o)), right(std::move(r)) {}

    std::string toString() const override {
        return "(" + left->toString() + " " + op + " " + right->toString() + ")";
    }
};

// NEW: Represents a JOIN ... ON ... clause
class JoinClause : public Node {
public:
    std::string tableName;
    std::shared_ptr<Expression> condition; // e.g., users.id = orders.user_id

    JoinClause(std::string table, std::shared_ptr<Expression> cond)
        : tableName(std::move(table)), condition(std::move(cond)) {}

    std::string toString() const override {
        return "JOIN " + tableName + " ON " + condition->toString();
    }
};

// NEW: Represents an aggregate function like SUM(amount) or COUNT(id)
class FunctionCall : public Expression {
public:
    std::string functionName; // e.g., "SUM"
    std::vector<std::shared_ptr<Expression>> arguments; // e.g., [amount]

    FunctionCall(std::string name, std::vector<std::shared_ptr<Expression>> args)
        : functionName(std::move(name)), arguments(std::move(args)) {}

    std::string toString() const override {
        std::string result = functionName + "(";
        for (size_t i = 0; i < arguments.size(); ++i) {
            result += arguments[i]->toString();
            if (i < arguments.size() - 1) result += ", ";
        }
        result += ")";
        return result;
    }
};

// NEW: Time-range filtering node
class BetweenExpression : public Expression {
public:
    std::shared_ptr<Expression> left;   // e.g., 'time'
    std::shared_ptr<Expression> lower;  // e.g., '1704067200500000000'
    std::shared_ptr<Expression> upper;  // e.g., '1704067200510000000'

    BetweenExpression(std::shared_ptr<Expression> l, std::shared_ptr<Expression> low, std::shared_ptr<Expression> up)
        : left(std::move(l)), lower(std::move(low)), upper(std::move(up)) {}

    std::string toString() const override {
        return left->toString() + " BETWEEN " + lower->toString() + " AND " + upper->toString();
    }
};

// Represents a full SELECT query
class SelectStatement : public Statement {
public:
    std::vector<std::shared_ptr<Expression>> columns;
    std::string tableName;
    std::vector<std::shared_ptr<JoinClause>> joins;
    std::shared_ptr<Expression> whereClause = nullptr;
    
    // NEW: Columns to group by (e.g., GROUP BY user_id)
    std::vector<std::shared_ptr<Expression>> groupBy;

    std::string toString() const override {
        std::string result = "SELECT [ ";
        for (size_t i = 0; i < columns.size(); ++i) {
            result += columns[i]->toString();
            if (i < columns.size() - 1) result += ", ";
        }
        result += " ] FROM " + tableName;
        
        for (const auto& join : joins) {
            result += " " + join->toString();
        }
        
        if (whereClause != nullptr) {
            result += " WHERE " + whereClause->toString();
        }
        
        // NEW: Print GROUP BY if it exists
        if (!groupBy.empty()) {
            result += " GROUP BY [ ";
            for (size_t i = 0; i < groupBy.size(); ++i) {
                result += groupBy[i]->toString();
                if (i < groupBy.size() - 1) result += ", ";
            }
            result += " ]";
        }
        
        return result;
    }
};

// Represents a CREATE TABLE statement: CREATE TABLE users (id, name);
class CreateTableStatement : public Statement {
public:
    std::string tableName;
    std::vector<std::string> columns;

    CreateTableStatement(std::string table, std::vector<std::string> cols)
        : tableName(std::move(table)), columns(std::move(cols)) {}

    std::string toString() const override {
        std::string result = "CREATE TABLE " + tableName + " (";
        for (size_t i = 0; i < columns.size(); ++i) {
            result += columns[i];
            if (i < columns.size() - 1) result += ", ";
        }
        result += ")";
        return result;
    }
};

// Represents an INSERT statement: INSERT INTO users VALUES (1, 'Alice'), (2, 'Bob');
class InsertStatement : public Statement {
public:
    std::string tableName;
    std::vector<std::vector<std::string>> rows; // One vector of literal values per row

    InsertStatement(std::string table, std::vector<std::vector<std::string>> r)
        : tableName(std::move(table)), rows(std::move(r)) {}

    std::string toString() const override {
        std::string result = "INSERT INTO " + tableName + " VALUES ";
        for (size_t r = 0; r < rows.size(); ++r) {
            result += "(";
            for (size_t i = 0; i < rows[r].size(); ++i) {
                result += rows[r][i];
                if (i < rows[r].size() - 1) result += ", ";
            }
            result += ")";
            if (r < rows.size() - 1) result += ", ";
        }
        return result;
    }
};

// NEW: Represents an EXPLAIN query
class ExplainStatement : public Statement {
public:
    std::shared_ptr<Statement> statement; // The query being explained (e.g., SELECT)

    explicit ExplainStatement(std::shared_ptr<Statement> stmt) : statement(std::move(stmt)) {}

    std::string toString() const override {
        return "EXPLAIN:\n  " + statement->toString();
    }
};

}