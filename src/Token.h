#pragma once

#include <string>
#include <unordered_map>

namespace brouwer
{
    enum class TokenType : size_t
    {
          root
        , prog
        , modDecl
        , import
        , line
        , expr
        , subexpr
        , chrLit
        , strLit
        , fnDecl
        , parened
        , return_
        , case_
        , ifElse
        , try_
        , while_
        , for_
        , fnApp
        , lambda
        , tupleLit
        , listLit
        , listComp
        , dictLit
        , dictComp
        , setLit
        , setComp
        , qualIdent
        , namespacedIdent
        , ident
        , memberIdent
        , scopedIdent
        , typeIdent
        , numLit
        , op
        , infixed
        , var
        , assign
        , pattern
        , strChr
        , param
        , generator
        , realLit
        , intLit
        , absInt
        , absReal
        , chrChr
        , dictEntry
        , caseBranch
        , equals
        , singleQuote
        , doubleQuote
        , moduleKeyword
        , exposingKeyword
        , hidingKeyword
        , importKeyword
        , asKeyword
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
        , nanKeyword
        , infinityKeyword
        , returnKeyword
        , dot
        , comma
        , colon
        , underscore
        , lArrow
        , rArrow
        , fatRArrow
        , lParen
        , rParen
        , lSqBracket
        , rSqBracket
        , lCurlyBracket
        , rCurlyBracket
        , backslash
        , doubleColon
        , minus
        , bar
        , backtick
    };

    const std::unordered_map<TokenType, std::string> token_type_names =
    {
          {TokenType::root,            "root"}
        , {TokenType::prog,            "prog"}
        , {TokenType::modDecl,         "modDecl"}
        , {TokenType::import,          "import"}
        , {TokenType::line,            "line"}
        , {TokenType::expr,            "expr"}
        , {TokenType::subexpr,         "subexpr"}
        , {TokenType::chrLit,          "chrLit"}
        , {TokenType::strLit,          "strLit"}
        , {TokenType::fnDecl,          "fnDecl"}
        , {TokenType::parened,         "parened"}
        , {TokenType::return_,         "return_"}
        , {TokenType::case_,           "case_"}
        , {TokenType::ifElse,          "ifElse"}
        , {TokenType::try_,            "try_"}
        , {TokenType::while_,          "while_"}
        , {TokenType::for_,            "for_"}
        , {TokenType::fnApp,           "fnApp"}
        , {TokenType::lambda,          "lambda"}
        , {TokenType::tupleLit,        "tupleLit"}
        , {TokenType::listLit,         "listLit"}
        , {TokenType::listComp,        "listComp"}
        , {TokenType::dictLit,         "dictLit"}
        , {TokenType::dictComp,        "dictComp"}
        , {TokenType::setLit,          "setLit"}
        , {TokenType::setComp,         "setComp"}
        , {TokenType::ident,           "ident"}
        , {TokenType::qualIdent,       "qualIdent"}
        , {TokenType::namespacedIdent, "namespacedIdent"}
        , {TokenType::memberIdent,     "memberIdent"}
        , {TokenType::scopedIdent,     "scopedIdent"}
        , {TokenType::typeIdent,       "typeIdent"}
        , {TokenType::numLit,          "numLit"}
        , {TokenType::op,              "op"}
        , {TokenType::infixed,         "infixed"}
        , {TokenType::var,             "var"}
        , {TokenType::assign,          "assign"}
        , {TokenType::pattern,         "pattern"}
        , {TokenType::strChr,          "strChr"}
        , {TokenType::param,           "param"}
        , {TokenType::generator,       "generator"}
        , {TokenType::realLit,         "realLit"}
        , {TokenType::intLit,          "intLit"}
        , {TokenType::absInt,          "absInt"}
        , {TokenType::absReal,         "absReal"}
        , {TokenType::chrChr,          "chrChr"}
        , {TokenType::dictEntry,       "dictEntry"}
        , {TokenType::caseBranch,      "caseBranch"}
        , {TokenType::equals,          "equals"}
        , {TokenType::singleQuote,     "singleQuote"}
        , {TokenType::doubleQuote,     "doubleQuote"}
        , {TokenType::moduleKeyword,   "moduleKeyword"}
        , {TokenType::exposingKeyword, "exposingKeyword"}
        , {TokenType::hidingKeyword,   "hidingKeyword"}
        , {TokenType::importKeyword,   "importKeyword"}
        , {TokenType::asKeyword,       "asKeyword"}
        , {TokenType::fnKeyword,       "fnKeyword"}
        , {TokenType::caseKeyword,     "caseKeyword"}
        , {TokenType::ifKeyword,       "ifKeyword"}
        , {TokenType::elseKeyword,     "elseKeyword"}
        , {TokenType::tryKeyword,      "tryKeyword"}
        , {TokenType::catchKeyword,    "catchKeyword"}
        , {TokenType::whileKeyword,    "whileKeyword"}
        , {TokenType::forKeyword,      "forKeyword"}
        , {TokenType::inKeyword,       "inKeyword"}
        , {TokenType::varKeyword,      "varKeyword"}
        , {TokenType::nanKeyword,      "nanKeyword"}
        , {TokenType::infinityKeyword, "infinityKeyword"}
        , {TokenType::returnKeyword,   "returnKeyword"}
        , {TokenType::dot,             "dot"}
        , {TokenType::comma,           "comma"}
        , {TokenType::colon,           "colon"}
        , {TokenType::underscore,      "underscore"}
        , {TokenType::lArrow,          "lArrow"}
        , {TokenType::rArrow,          "rArrow"}
        , {TokenType::fatRArrow,       "fatRArrow"}
        , {TokenType::lParen,          "lParen"}
        , {TokenType::rParen,          "rParen"}
        , {TokenType::lSqBracket,      "lSqBracket"}
        , {TokenType::rSqBracket,      "rSqBracket"}
        , {TokenType::lCurlyBracket,   "lCurlyBracket"}
        , {TokenType::rCurlyBracket,   "rCurlyBracket"}
        , {TokenType::backslash,       "backslash"}
        , {TokenType::doubleColon,     "doubleColon"}
        , {TokenType::minus,           "minus"}
        , {TokenType::bar,             "bar"}
        , {TokenType::backtick,        "backtick"}
    };

    struct Token
    {
        TokenType type;

        std::string lexeme;

        Token(TokenType t, std::string lex) noexcept;
    };
}
