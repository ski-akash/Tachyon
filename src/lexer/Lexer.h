#pragma once

#include "TokenType.h"
#include <string>
#include <vector>

namespace tachyon {

class Lexer {
public:
    // Constructor takes the raw SQL string
    explicit Lexer(std::string source);

    // The main function that returns all tokens
    std::vector<Token> tokenize();
    
    Token nextToken(); 

private:
    std::string source_;
    size_t position_ = 0;      
    size_t read_position_ = 0; 
    char current_char_ = '\0';

    // Helper methods
    void advance();
    char peekChar() const;
    void skipWhitespace();
    
    // Multi-character helpers
    bool isLetter(char ch);
    bool isDigit(char ch);
    std::string readIdentifier();
    std::string readNumber();
    std::string readString();
    TokenType lookupIdentifier(const std::string& ident);
}; 

} // namespace tachyon 