#pragma once

#include "lexer/Lexer.h"
#include "ast/AST.h"
#include <vector>
#include <memory>

namespace tachyon {

class Parser {
public:
    // Initialize the parser with a lexer
    explicit Parser(Lexer lexer);

    // Parses the entire token stream and returns a list of executable statements
    std::vector<std::shared_ptr<Statement>> parse();

private:
    Lexer lexer_;
    Token current_token_;
    Token peek_token_;

    // Advances our position in the token stream
    void nextToken();

    // Parsing functions for specific parts of the SQL grammar
    std::shared_ptr<Statement> parseStatement();
    std::shared_ptr<SelectStatement> parseSelectStatement();
    std::shared_ptr<Statement> parseCreateTableStatement();
    std::shared_ptr<Statement> parseInsertStatement();

    // NEW: Parses things like "id = 42"
    std::shared_ptr<Expression> parseExpression();
    
    // NEW: Parses JOIN ... ON ...
    std::shared_ptr<JoinClause> parseJoinClause();

    // NEW: Determines if a token is a standard column or a function call
    std::shared_ptr<Expression> parseColumnOrFunction();

    std::shared_ptr<Statement> parseExplainStatement(); // NEW
    
    // Helper methods to check token types safely
    bool currentTokenIs(TokenType type) const;
    bool peekTokenIs(TokenType type) const;
    bool expectPeek(TokenType type);
};

} // namespace tachyon