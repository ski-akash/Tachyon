#pragma once

#include <string>

namespace quill {

enum class TokenType {
    // Keywords
    SELECT,
    FROM,
    WHERE,

    // Identifiers and Literals
    IDENTIFIER, // e.g., table names, column names
    NUMBER,     // e.g., 42, 3.14
    STRING,     // e.g., 'hello'
    STRING_LITERAL,

    // Operators
    PLUS,       // +
    MINUS,      // -
    STAR,       // *
    SLASH,      // /
    EQUALS,     // =
    NOT_EQUALS, // != or <>
    LESS_THAN,      // <
    GREATER_THAN,   // >
    LESS_EQUAL,     // <=
    GREATER_EQUAL,  // >=
    JOIN,
    ON,
    GROUP,
    BY,
    EXPLAIN,

    // Time Series Tokens 
    BETWEEN, 
    AND,

    // Syntax
    COMMA,      // ,
    SEMICOLON,  // ;
    LPAREN,     // (
    RPAREN,     // )
    
    // Special
    END_OF_FILE,
    ILLEGAL
};

// A simple struct to hold the token type and its actual string value
struct Token {
    TokenType type;
    std::string literal;
};

} // namespace quill