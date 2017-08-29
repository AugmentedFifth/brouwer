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

            Parser(const std::string& filename);

            std::optional<AST> parse();

            static std::string str_repr(const AST& ast) noexcept;

            static void log_depthfirst(const AST& ast, size_t cur_depth);

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

            static const std::unordered_set<std::string> reserved_ops;

            std::optional<AST> parse_prog();

            std::optional<AST> parse_modDecl();

            std::optional<AST> parse_import();

            std::optional<AST> parse_line(bool consume_newline);

            bool consume_lineComment(bool consume_newline);

            std::optional<AST> parse_expr();

            std::optional<AST> parse_subexpr();

            std::optional<AST> parse_chrLit();

            std::optional<AST> parse_strLit();

            std::optional<AST> parse_numLit();

            std::optional<AST> parse_fnDecl();

            std::optional<AST> parse_parened();

            std::optional<AST> parse_return();

            std::optional<AST> parse_case();

            std::optional<AST> parse_caseBranch();

            std::optional<AST> parse_ifElse();

            std::optional<AST> parse_try();

            std::optional<AST> parse_while();

            std::optional<AST> parse_for();

            std::optional<AST> parse_lambda();

            std::optional<AST> parse_tupleLit();

            std::optional<AST> parse_listLit();

            std::optional<AST> parse_listComp();

            std::optional<AST> parse_dictLit();

            std::optional<AST> parse_dictComp();

            std::optional<AST> parse_setLit();

            std::optional<AST> parse_setComp();

            std::optional<AST> parse_qualIdent();

            std::optional<AST> parse_namespacedIdent();

            std::optional<AST> parse_op();

            std::optional<AST> parse_infixed();

            std::optional<AST> parse_ident();

            std::optional<AST> parse_memberIdent();

            std::optional<AST> parse_scopedIdent();

            std::optional<AST> parse_typeIdent();

            std::optional<AST> parse_var();

            std::optional<AST> parse_assign();

            std::optional<AST> parse_pattern();

            std::optional<AST> parse_chrChr();

            std::optional<AST> parse_strChr();

            std::optional<AST> parse_param();

            std::optional<AST> parse_generator();

            std::optional<AST> parse_dictEntry();

            std::optional<AST> parse_equals();

            std::optional<AST> parse_singleQuote();

            std::optional<AST> parse_doubleQuote();

            std::optional<AST> parse_fnKeyword();

            std::optional<AST> parse_caseKeyword();

            std::optional<AST> parse_ifKeyword();

            std::optional<AST> parse_elseKeyword();

            std::optional<AST> parse_tryKeyword();

            std::optional<AST> parse_catchKeyword();

            std::optional<AST> parse_whileKeyword();

            std::optional<AST> parse_forKeyword();

            std::optional<AST> parse_inKeyword();

            std::optional<AST> parse_varKeyword();

            std::optional<AST> parse_moduleKeyword();

            std::optional<AST> parse_exposingKeyword();

            std::optional<AST> parse_hidingKeyword();

            std::optional<AST> parse_importKeyword();

            std::optional<AST> parse_asKeyword();

            std::optional<AST> parse_returnKeyword();

            bool consume_lineCommentOp();

            std::optional<AST> parse_dot();

            std::optional<AST> parse_comma();

            std::optional<AST> parse_colon();

            std::optional<AST> parse_doubleColon();

            std::optional<AST> parse_underscore();

            std::optional<AST> parse_lArrow();

            std::optional<AST> parse_rArrow();

            std::optional<AST> parse_fatRArrow();

            std::optional<AST> parse_lParen();

            std::optional<AST> parse_rParen();

            std::optional<AST> parse_lSqBracket();

            std::optional<AST> parse_rSqBracket();

            std::optional<AST> parse_lCurlyBracket();

            std::optional<AST> parse_rCurlyBracket();

            std::optional<AST> parse_backslash();

            std::optional<AST> parse_bar();

            std::optional<AST> parse_backtick();

            bool advance() noexcept;

            bool consume_blanks() noexcept;

            bool expect_newline() noexcept;

            bool expect_string(const std::string& s) noexcept;

            bool expect_keyword(const std::string& kwd);

            bool expect_op(const std::string& op);

            std::string get_block(AST& main_ast, TokenType body_item_type);

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
