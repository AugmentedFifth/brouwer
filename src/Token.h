#pragma once

#include <string>
#include <unordered_map>

namespace brouwer
{
    enum class TokenType : size_t
    {
          root
        , prog
        , expr
        , chrLit
        , strLit
        , fnDecl
        , parened
        , case_
        , ifElse
        , try_
        , while_
        , for_
        , fnApp
        , lambda
        , listLit
        , setLit
        , dictLit
        , ident
        , numLit
        , op
        , infix
        , var
        , assign
        , pattern
        , patUnit
        , strChr
        , param
        , realLit
        , intLit
        , chrChr
        , dictEntry
        , equals
        , singleQuote
        , doubleQuote
        , fnKeyword
        , caseKeyword
        , ifKeyword
        , elseKeyword
        , tryKeyword
        , catchKeyword
        , whileKeyword
        , forKeyword
        , inKeyword
        , varKeyword
        , comma
        , colon
        , underscore
        , arrow
        , lParen
        , rParen
        , lSqBracket
        , rSqBracket
        , lCurlyBracket
        , rCurlyBracket
        , backslash
    };

    const std::unordered_map<TokenType, std::string> token_type_names =
    {
          {TokenType::root,          "root"}
        , {TokenType::prog,          "prog"}
        , {TokenType::expr,          "expr"}
        , {TokenType::chrLit,        "chrLit"}
        , {TokenType::strLit,        "strLit"}
        , {TokenType::fnDecl,        "fnDecl"}
        , {TokenType::parened,       "parened"}
        , {TokenType::case_,         "case_"}
        , {TokenType::ifElse,        "ifElse"}
        , {TokenType::try_,          "try_"}
        , {TokenType::while_,        "while_"}
        , {TokenType::for_,          "for_"}
        , {TokenType::fnApp,         "fnApp"}
        , {TokenType::lambda,        "lambda"}
        , {TokenType::listLit,       "listLit"}
        , {TokenType::setLit,        "setLit"}
        , {TokenType::dictLit,       "dictLit"}
        , {TokenType::ident,         "ident"}
        , {TokenType::numLit,        "numLit"}
        , {TokenType::op,            "op"}
        , {TokenType::infix,         "infix"}
        , {TokenType::var,           "var"}
        , {TokenType::assign,        "assign"}
        , {TokenType::pattern,       "pattern"}
        , {TokenType::patUnit,       "patUnit"}
        , {TokenType::strChr,        "strChr"}
        , {TokenType::param,         "param"}
        , {TokenType::realLit,       "realLit"}
        , {TokenType::intLit,        "intLit"}
        , {TokenType::chrChr,        "chrChr"}
        , {TokenType::dictEntry,     "dictEntry"}
        , {TokenType::equals,        "equals"}
        , {TokenType::singleQuote,   "singleQuote"}
        , {TokenType::doubleQuote,   "doubleQuote"}
        , {TokenType::fnKeyword,     "fnKeyword"}
        , {TokenType::caseKeyword,   "caseKeyword"}
        , {TokenType::ifKeyword,     "ifKeyword"}
        , {TokenType::elseKeyword,   "elseKeyword"}
        , {TokenType::tryKeyword,    "tryKeyword"}
        , {TokenType::catchKeyword,  "catchKeyword"}
        , {TokenType::whileKeyword,  "whileKeyword"}
        , {TokenType::forKeyword,    "forKeyword"}
        , {TokenType::inKeyword,     "inKeyword"}
        , {TokenType::varKeyword,    "varKeyword"}
        , {TokenType::comma,         "comma"}
        , {TokenType::colon,         "colon"}
        , {TokenType::underscore,    "underscore"}
        , {TokenType::arrow,         "arrow"}
        , {TokenType::lParen,        "lParen"}
        , {TokenType::rParen,        "rParen"}
        , {TokenType::lSqBracket,    "lSqBracket"}
        , {TokenType::rSqBracket,    "rSqBracket"}
        , {TokenType::lCurlyBracket, "lCurlyBracket"}
        , {TokenType::rCurlyBracket, "rCurlyBracket"}
        , {TokenType::backslash,     "backslash"}
    };

    class Token
    {
        public:
            TokenType type;

            std::string lexeme;

            Token(std::string lex) noexcept;

            Token(TokenType t, std::string lex) noexcept;
    };
}
