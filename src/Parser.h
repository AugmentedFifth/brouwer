#pragma once

#include <deque>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_set>

#include "brouwer.h"

namespace brouwer
{
    class Parser
    {
        using AST = Tree<Token>;

        private:
            std::ifstream charstream;

            std::deque<char> charhistory;

            char ch;

        public:
            Parser(std::ifstream& chStream) noexcept;

            AST* parse() noexcept;

            AST* parse_prog() noexcept;

            AST* parse_expr();

            AST* parse_chrLit();

            AST* parse_strLit();

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

            AST* parse_assign();

            AST* parse_var();

            AST* parse_pattern();

            AST* parse_patUnit();

            AST* parse_strChr();

            AST* parse_param();

            void advance() noexcept;

            bool consume_blanks() noexcept;

            bool expect_newline() noexcept;

            bool expect_string(std::string s) noexcept;

            bool expect_char(char c) noexcept;

            std::optional<char> expect_char_not(char c) noexcept;

            std::optional<char> expect_char_of(
                unordered_set<char>& cs
            ) noexcept;

            std::optional<char> expect_char_not_of(
                unordered_set<char>& cs
            ) noexcept;

            static bool isnewline(char c) noexcept;
    };
}
