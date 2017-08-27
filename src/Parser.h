#pragma once

#include <deque>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_set>

#include "Tree.h"
#include "Token.h"

namespace brouwer
{
    class Parser
    {
        public:
            using AST = Tree<Token>;

            Parser(std::string& filename);

            AST* parse();

            static std::string str_repr(const AST* ast) noexcept;

            static void log_depthfirst(const AST* ast, size_t cur_depth);

            static bool isnewline(char c) noexcept;

            static bool isprefixof(const std::string& a,
                                   const std::string& b) noexcept;

        private:
            std::ifstream charstream;

            std::deque<char> charhistory;

            char ch;

            std::string currentindent;

            static const std::unordered_set<char> esc_chars;

            static const std::unordered_set<char> op_chars;

            AST* parse_prog();

            AST* parse_stmt();

            AST* parse_expr();

            AST* parse_chrLit();

            AST* parse_strLit();

            AST* parse_numLit();

            AST* parse_fnDecl();

            AST* parse_parened();

            AST* parse_case();

            AST* parse_ifElse();

            AST* parse_try();

            AST* parse_while();

            AST* parse_for();

            AST* parse_lambda();

            AST* parse_listLit();

            AST* parse_setLit();

            AST* parse_dictLit();

            AST* parse_ident();

            AST* parse_var();

            AST* parse_assign();

            AST* parse_pattern();

            AST* parse_chrChr();

            AST* parse_strChr();

            AST* parse_param();

            AST* parse_dictEntry();

            AST* parse_equals();

            AST* parse_singleQuote();

            AST* parse_doubleQuote();

            AST* parse_fnKeyword();

            AST* parse_caseKeyword();

            AST* parse_ifKeyword();

            AST* parse_elseKeyword();

            AST* parse_tryKeyword();

            AST* parse_catchKeyword();

            AST* parse_whileKeyword();

            AST* parse_forKeyword();

            AST* parse_inKeyword();

            AST* parse_varKeyword();

            AST* parse_comma();

            AST* parse_colon();

            AST* parse_underscore();

            AST* parse_arrow();

            AST* parse_lParen();

            AST* parse_rParen();

            AST* parse_lSqBracket();

            AST* parse_rSqBracket();

            AST* parse_lCurlyBracket();

            AST* parse_rCurlyBracket();

            AST* parse_backslash();

            bool advance() noexcept;

            bool consume_blanks() noexcept;

            bool expect_newline() noexcept;

            bool expect_string(const std::string& s) noexcept;

            bool expect_keyword(const std::string& kwd);

            bool expect_op(const std::string& op);

            std::string get_block(AST* main_ast, TokenType body_item_type);

            bool expect_char(char c) noexcept;

            std::optional<char> expect_char_not(char c) noexcept;

            std::optional<char> expect_char_of(
                const std::unordered_set<char>& cs
            ) noexcept;

            std::optional<char> expect_char_not_of(
                const std::unordered_set<char>& cs
            ) noexcept;
    };
}
