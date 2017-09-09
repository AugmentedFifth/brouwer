#include "Token.h"

#include <string>

namespace brouwer
{
    Token::Token(TokenType t, std::string lex) noexcept
        : type(t), lexeme(lex) {}
}
