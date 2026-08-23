#include "parser/Parser.h"
#include <unordered_map>

namespace quill {

Parser::Parser(Lexer lexer) : lexer_(std::move(lexer)) {
    // Read two tokens immediately so both current_token_ and peek_token_ are populated
    nextToken();
    nextToken();
}

void Parser::nextToken() {
    current_token_ = peek_token_;
    peek_token_ = lexer_.nextToken();
}

bool Parser::currentTokenIs(TokenType type) const {
    return current_token_.type == type;
}

bool Parser::peekTokenIs(TokenType type) const {
    return peek_token_.type == type;
}

bool Parser::expectPeek(TokenType type) {
    if (peekTokenIs(type)) {
        nextToken();
        return true;
    } else {
        return false;
    }
}

std::vector<std::shared_ptr<Statement>> Parser::parse() {
    std::vector<std::shared_ptr<Statement>> statements;
    
    while (current_token_.type != TokenType::END_OF_FILE) {
        auto stmt = parseStatement();
        if (stmt != nullptr) {
            statements.push_back(stmt);
        }
        nextToken();
    }
    
    return statements;
}

std::shared_ptr<Statement> Parser::parseStatement() {
    if (current_token_.type == TokenType::EXPLAIN) {
        return parseExplainStatement();
    }
    if (current_token_.type == TokenType::SELECT) {
        return parseSelectStatement();
    }
    return nullptr;
}

// NEW: Parses EXPLAIN followed by a SELECT
std::shared_ptr<Statement> Parser::parseExplainStatement() {
    nextToken(); // Move past 'EXPLAIN'

    // In Phase 3, we assume EXPLAIN is always followed by a SELECT query
    if (current_token_.type == TokenType::SELECT) {
        auto stmt = parseSelectStatement();
        return std::make_shared<ExplainStatement>(std::move(stmt));
    }
    
    return nullptr;
}

// NEW: Parses basic expressions and the BETWEEN operator
std::shared_ptr<Expression> Parser::parseExpression() {
    // Grab the left side (e.g., 'time') using your existing helper!
    auto left = parseColumnOrFunction(); 

    // Intercept the Time-Series BETWEEN operator
    if (currentTokenIs(TokenType::BETWEEN)) {
        nextToken(); // Consume 'BETWEEN'
        
        auto lower = parseColumnOrFunction(); // Get the lower bound
        
        if (!currentTokenIs(TokenType::AND)) {
            throw std::runtime_error("Parser Error: Expected 'AND' after 'BETWEEN' lower bound.");
        }
        nextToken(); // Consume 'AND'
        
        auto upper = parseColumnOrFunction(); // Get the upper bound
        
        return std::make_shared<BetweenExpression>(left, lower, upper);
    }

    // Comparison operators: id = 42, age > 18, price <= 100, ...
    static const std::unordered_map<TokenType, std::string> kComparisonOps = {
        {TokenType::EQUALS, "="},
        {TokenType::NOT_EQUALS, "!="},
        {TokenType::LESS_THAN, "<"},
        {TokenType::GREATER_THAN, ">"},
        {TokenType::LESS_EQUAL, "<="},
        {TokenType::GREATER_EQUAL, ">="},
    };

    auto it = kComparisonOps.find(current_token_.type);
    if (it != kComparisonOps.end()) {
        std::string op = it->second;
        nextToken(); // Consume the operator

        auto right = parseColumnOrFunction();
        return std::make_shared<BinaryExpression>(left, op, right);
    }

    return left;
}

// NEW: Build the Join AST Node
std::shared_ptr<JoinClause> Parser::parseJoinClause() {
    nextToken(); // Move past 'JOIN'

    std::string joinTable;
    if (current_token_.type == TokenType::IDENTIFIER) {
        joinTable = current_token_.literal;
        nextToken(); // Move past table name
    }

    if (currentTokenIs(TokenType::ON)) {
        nextToken(); // Move past 'ON'
    }

    auto condition = parseExpression();

    return std::make_shared<JoinClause>(std::move(joinTable), std::move(condition));
}

// NEW: Parse a regular column or a multi-argument function call
std::shared_ptr<Expression> Parser::parseColumnOrFunction() {
    std::string name = current_token_.literal;
    bool is_number = currentTokenIs(TokenType::NUMBER);
    nextToken(); // Advance past the name

    // A numeric literal is never followed by '(' or column semantics
    if (is_number) {
        return std::make_shared<NumberLiteral>(name);
    }

    // If the next token is a '(', this is a function call!
    if (currentTokenIs(TokenType::LPAREN)) {
        nextToken(); // Advance past '('
        
        std::vector<std::shared_ptr<Expression>> args;
        
        // Loop to grab all arguments separated by commas
        while (!currentTokenIs(TokenType::RPAREN) && !currentTokenIs(TokenType::END_OF_FILE)) {
            // We accept identifiers, stars, and string literals (for things like '1m')
            if (currentTokenIs(TokenType::IDENTIFIER) || 
                currentTokenIs(TokenType::STAR) || 
                currentTokenIs(TokenType::STRING_LITERAL)) {
                
                args.push_back(std::make_shared<Identifier>(current_token_.literal));
                nextToken(); // Consume the argument
            }

            // If there's a comma, consume it and continue the loop
            if (currentTokenIs(TokenType::COMMA)) {
                nextToken();
            }
        }

        if (currentTokenIs(TokenType::RPAREN)) {
            nextToken(); // Advance past ')'
        }
        
        // Optional: Strict Validation for Time-Series Functions
        if (name == "TIME_BUCKET" && args.size() != 2) {
            throw std::runtime_error("Parser Error: TIME_BUCKET requires 2 arguments (column, interval).");
        }
        if (name == "VWAP" && args.size() != 2) {
            throw std::runtime_error("Parser Error: VWAP requires 2 arguments (price_col, size_col).");
        }
        
        return std::make_shared<FunctionCall>(name, args);
    }

    // If there was no '(', it's just a regular column
    return std::make_shared<Identifier>(name);
}

std::shared_ptr<SelectStatement> Parser::parseSelectStatement() {
    auto stmt = std::make_shared<SelectStatement>();

    nextToken(); // Move past 'SELECT'

    // Parse columns until we hit 'FROM' or EOF
    while (current_token_.type != TokenType::FROM && current_token_.type != TokenType::END_OF_FILE) {
        if (current_token_.type == TokenType::IDENTIFIER) {
            // NEW: Use our smart helper method
            stmt->columns.push_back(parseColumnOrFunction());
        } else {
            nextToken(); // Move past commas
        }
    }

    // Parse the main table name
    if (currentTokenIs(TokenType::FROM)) {
        if (expectPeek(TokenType::IDENTIFIER)) {
            stmt->tableName = current_token_.literal;
        }
    }
    nextToken(); // Advance past the table name

    // Check for JOIN clauses
    while (currentTokenIs(TokenType::JOIN)) {
        stmt->joins.push_back(parseJoinClause());
        nextToken(); 
    }

    // Check if there is a WHERE clause
    if (currentTokenIs(TokenType::WHERE)) {
        nextToken(); // Move past 'WHERE'
        stmt->whereClause = parseExpression();
    }
    
    // NEW: Check if there is a GROUP BY clause
    if (currentTokenIs(TokenType::GROUP)) {
        nextToken(); // Move past 'GROUP'
        if (currentTokenIs(TokenType::BY)) {
            nextToken(); // Move past 'BY'

            // Parse the grouping columns until the query ends
            while (!currentTokenIs(TokenType::SEMICOLON) && !currentTokenIs(TokenType::END_OF_FILE)) {
                if (currentTokenIs(TokenType::IDENTIFIER)) {
                    // CHANGE: Use parseColumnOrFunction() here so we can Group By Time Buckets!
                    stmt->groupBy.push_back(parseColumnOrFunction());
                } else {
                    nextToken(); // Move past commas
                }
            }
        }
    }

    if (peekTokenIs(TokenType::SEMICOLON) || currentTokenIs(TokenType::SEMICOLON)) {
        if (peekTokenIs(TokenType::SEMICOLON)) nextToken();
    }

    return stmt;
}

} // namespace quill