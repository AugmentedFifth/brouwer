#include <ctype.h>

#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "Parser.h"
#include "brouwer.h"

namespace brouwer
{
    using AST = Tree<Token>;

    Parser::Parser(std::ifstream& chstream) noexcept : charstream(chstream) {}

    AST* Parser::parse() noexcept
    {
        while (this->charstream >> std::noskipws >> this->ch)
        {
            if (!isspace(this->ch))
            {
                break;
            }
        }

        AST* mainAst = new AST({ TokenType::root, "" });
        AST* prog = parse_prog();

        if (!prog)
        {
            return NULL;
        }

        mainAst->add_child(prog);

        return mainAst;
    }

    AST* Parser::parse_prog() noexcept
    {
        AST* expr = parse_expr();

        if (expr == NULL)
        {
            return NULL;
        }

        AST* prog = new AST({ TokenType::prog, "" });
        prog->add_child(expr);

        while (expect_newline()) {
            if (
                !(this->charstream.eof() && this->charhistory.empty()) &&
                !isnewline(this->ch)                                   &&
                !isblank(this->ch)
            ) {
                AST* moreExpr = parse_expr();

                if (!moreExpr)
                {
                    return NULL;
                }

                prog->add_child(moreExpr);
            }
            else
            {
                break;
            }
        }

        return prog;
    }

    AST* Parser::parse_expr()
    {
        consume_blanks();

        AST* expr = new AST({ TokenType::expr, "" });

        if (AST* chrLit = parse_chrLit())
        {
            expr->add_child(chrLit);
        }
        else if (AST* strLit = parse_strLit())
        {
            expr->add_child(strLit);
        }
        else if (AST* fnDecl = parse_fnDecl())
        {
            expr->add_child(fnDecl);
        }
        else if (AST* parened = parse_parened())
        {
            expr->add_child(parened);
        }
        else if (AST* case_ = parse_case())
        {
            expr->add_child(case_);
        }
        else if (AST* ifElse = parse_ifElse())
        {
            expr->add_child(ifElse);
        }
        else if (AST* try_ = parse_try())
        {
            expr->add_child(try_);
        }
        else if (AST* while_ = parse_while())
        {
            expr->add_child(while_);
        }
        else if (AST* for_ = parse_for())
        {
            expr->add_child(for_);
        }
        else if (AST* lambda = parse_lambda())
        {
            expr->add_child(lambda);
        }
        else if (AST* listLit = parse_listLit())
        {
            expr->add_child(listLit);
        }
        else if (AST* setLit = parse_setLit())
        {
            expr->add_child(setLit);
        }
        else if (AST* dictLit = parse_dictLit())
        {
            expr->add_child(dictLit);
        }
        else if (AST* ident = parse_ident())
        {
            expr->add_child(ident);
        }
        else if (AST* numLit = parse_numLit())
        {
            expr->add_child(numLit);
        }
        else if (AST* assign = parse_assign())
        {
            expr->add_child(assign);
        }
        else if (AST* var = parse_var())
        {
            expr->add_child(var);
        }
        else
        {
            return NULL;
        }

        return expr;
    }

    AST* Parser::parse_chrLit()
    {
        consume_blanks();

        if (!expect_char('\''))
        {
            return NULL;
        }

        if (expect_char_not('\''))
        {

        }
    }

    AST* Parser::parse_strLit()
    {

    }

    AST* Parser::parse_fnDecl()
    {

    }

    AST* Parser::parse_parened()
    {

    }

    AST* Parser::parse_case()
    {

    }

    AST* Parser::parse_ifElse()
    {

    }

    AST* Parser::parse_try()
    {

    }

    AST* Parser::parse_while()
    {

    }

    AST* Parser::parse_for()
    {

    }

    AST* Parser::parse_lambda()
    {

    }

    AST* Parser::parse_listLit()
    {

    }

    AST* Parser::parse_setLit()
    {

    }

    AST* Parser::parse_dictLit()
    {

    }

    AST* Parser::parse_ident()
    {

    }

    AST* Parser::parse_assign()
    {

    }

    AST* Parser::parse_var()
    {

    }

    AST* Parser::parse_pattern()
    {

    }

    AST* Parser::parse_patUnit()
    {

    }

    AST* Parser::parse_strChr()
    {

    }

    AST* Parser::parse_param()
    {

    }

    void Parser::advance() noexcept
    {
        if (!this->charhistory.empty())
        {
            this->ch = this->charhistory.front();
            this->charhistory.pop_front();
        }
        else
        {
            this->charstream >> std::noskipws >> this->ch;
        }
    }

    bool Parser::consume_blanks() noexcept
    {
        if (!isblank(this->ch))
        {
            return false;
        }

        while (!this->charhistory.empty())
        {
            this->ch = this->charhistory.front();
            this->charhistory.pop_front();

            if (!isblank(this->ch))
            {
                return true;
            }
        }

        while (this->charstream >> std::noskipws >> this->ch)
        {
            if (!isblank(this->ch))
            {
                return true;
            }
        }

        return true;
    }

    bool Parser::expect_newline() noexcept
    {
        consume_blanks();

        if (!isnewline(this->ch))
        {
            return false;
        }

        while (!this->charhistory.empty())
        {
            this->ch = this->charhistory.front();
            this->charhistory.pop_front();

            if (!isnewline(this->ch) && !isblank(this->ch))
            {
                return true;
            }
        }

        while (this->charstream >> std::noskipws >> this->ch)
        {
            if (!isnewline(this->ch) && !isblank(this->ch))
            {
                return true;
            }
        }

        return true;
    }

    bool Parser::expect_char(char c) noexcept
    {
        if (this->ch != c)
        {
            return false;
        }

        advance();

        return true;
    }

    std::optional<char> Parser::expect_char_not(char c) noexcept
    {
        if (this->ch == c)
        {
            return {};
        }

        const char tmp = this->ch;
        advance();

        return tmp;
    }

    std::optional<char> Parser::expect_char_of(
        unordered_set<char>& cs
    ) noexcept
    {
        if (cs.find(this->ch) == cs.end())
        {
            return {};
        }

        advance();

        return true;
    }

    std::optional<char> Parser::expect_char_not_of(
        unordered_set<char>& cs
    ) noexcept
    {
        if (cs.find(this->ch) != cs.end())
        {
            return false;
        }

        advance();

        return true;
    }

    bool Parser::expect_string(const std::string s) noexcept
    {
        if (s.empty())
        {
            return true;
        }

        size_t i = 0;

        if (this->ch != s[i])
        {
            return false;
        }

        i++;
        std::vector<char> historicstack(s.length());

        while (i < s.length() && !this->charhistory.empty())
        {
            if (this->charhistory.front() != s[i])
            {
                while (historicstack.size() > 1)
                {
                    this->charhistory.push_front(historicstack.back());
                    historicstack.pop_back();
                }

                if (!historicstack.empty())
                {
                    this->ch = historicstack.back();
                }

                return false;
            }

            historicstack.push_back(this->ch);
            this->ch = this->charhistory.front();
            this->charhistory.pop_front();

            i++;
        }

        if (i == s.length())
        {
            advance();

            return true;
        }

        this->charhistory.push_back(this->ch);

        while (i < s.length() && this->charstream >> std::noskipws >> this->ch)
        {
            if (this->ch != s[i])
            {
                return false;
            }

            this->charhistory.push_back(this->ch);
            i++;
        }

        this->charstream >> std::noskipws >> this->ch;
        this->charhistory.clear();

        return i == s.length();
    }

    bool Parser::isnewline(char c) noexcept
    {
        return c == '\n' || c == '\r';
    }
}
