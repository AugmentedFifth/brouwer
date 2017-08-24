#include <ctype.h>

#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "Parser.h"
#include "Tree.h"
#include "Token.h"

namespace brouwer
{
    using AST = Tree<Token>;

    const std::unordered_set<char> Parser::esc_chars =
        {'\'', '"', 't', 'v', 'n', 'r', 'b', '0'};

    const std::unordered_set<char> Parser::op_chars =
    {
        '?', '<', '>', '=', '%', '\\', '~', '!',
        '@', '#', '$', '|', '&', '*',  '/', '+',
        '^', '-'
    };

    Parser::Parser(std::string& filename) : charstream(filename.c_str())
    {
        this->currentindent = "";

        if (!this->charstream.is_open())
        {
            throw std::runtime_error("Failed to open " + filename);
        }
    }

    void Parser::log_depthfirst(const AST* ast, size_t cur_depth)
    {
        for (size_t i = 0; i < cur_depth; ++i)
        {
            std::cout << "  ";
        }

        std::cout << ast->val().lexeme
                  << " : "
                  << token_type_names.at(ast->val().type)
                  << '\n';

        const size_t child_count = ast->child_count();
        for (size_t i = 0; i < child_count; ++i)
        {
            log_depthfirst(ast->get_child(i), cur_depth + 1);
        }
    }

    AST* Parser::parse() noexcept
    {
        char last_ch = '\0';

        while (this->charstream >> std::noskipws >> this->ch)
        {
            if (!isspace(this->ch))
            {
                break;
            }

            last_ch = this->ch;
        }

        if (last_ch != '\0' && !isnewline(last_ch))
        {
            throw std::runtime_error(
                "source must not start with leading whitespace"
            );
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

        while (expect_newline())
        {
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
        else if (AST* var = parse_var())
        {
            expr->add_child(var);
        }
        else if (AST* assign = parse_assign())
        {
            expr->add_child(assign);
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

        AST* init_singleQuote = parse_singleQuote();

        if (!init_singleQuote)
        {
            return NULL;
        }

        AST* the_char = parse_chrChr();

        if (!the_char)
        {
            throw std::runtime_error("unexpected ' or EOF");
        }

        AST* end_singleQuote = parse_singleQuote();

        if (!end_singleQuote)
        {
            std::string err_msg = "expected ', got: ";
            err_msg += this->ch;

            throw std::runtime_error(err_msg);
        }

        AST* chrLit = new AST({ TokenType::chrLit, "" });
        chrLit->add_child(init_singleQuote);
        chrLit->add_child(the_char);
        chrLit->add_child(end_singleQuote);

        return chrLit;
    }

    AST* Parser::parse_strLit()
    {
        consume_blanks();

        AST* strLit = new AST({ TokenType::strLit, "" });

        AST* init_doubleQuote = parse_doubleQuote();

        if (!init_doubleQuote)
        {
            return NULL;
        }

        strLit->add_child(init_doubleQuote);

        while (this->ch != '"')
        {
            if (AST* a_char = parse_strChr())
            {
                strLit->add_child(a_char);
            }
            else
            {
                throw std::runtime_error(
                    "invalid escape sequence or unexpected EOF"
                );
            }
        }

        AST* end_doubleQuote = parse_doubleQuote();

        if (!end_doubleQuote)
        {
            std::string err_msg = "expected \", got: ";
            err_msg += this->ch;

            throw std::runtime_error(err_msg);
        }

        strLit->add_child(end_doubleQuote);

        return strLit;
    }

    AST* Parser::parse_numLit()
    {
        consume_blanks();

        if (!isdigit(this->ch))
        {
            return NULL;
        }

        std::string s = "";

        while (isdigit(this->ch))
        {
            s.push_back(this->ch);
            advance();
        }

        if (this->ch != '.')
        {
            AST* numLit = new AST({ TokenType::numLit, "" });
            numLit->add_child(new AST({ TokenType::intLit, s }));

            return numLit;
        }

        s.push_back(this->ch);
        advance();

        if (!isdigit(this->ch))
        {
            return NULL;
        }

        while (isdigit(this->ch))
        {
            s.push_back(this->ch);
            advance();
        }

        AST* numLit = new AST({ TokenType::numLit, "" });
        numLit->add_child(new AST({ TokenType::realLit, s }));

        return numLit;
    }

    AST* Parser::parse_fnDecl()
    {
        consume_blanks();

        AST* fn_keyword = parse_fnKeyword();

        if (!fn_keyword)
        {
            return NULL;
        }

        consume_blanks();
        AST* fn_name = parse_ident();

        if (!fn_name)
        {
            throw std::runtime_error("expected function name");
        }

        consume_blanks();
        AST* fnDecl = new AST({ TokenType::fnDecl, "" });
        fnDecl->add_child(fn_keyword);
        fnDecl->add_child(fn_name);

        while (AST* fn_param = parse_param())
        {
            fnDecl->add_child(fn_param);
        }

        consume_blanks();
        AST* arrow = parse_arrow();

        if (arrow)
        {
            fnDecl->add_child(arrow);

            AST* ret_type = parse_ident();

            if (!ret_type)
            {
                throw std::runtime_error("expected type after arrow");
            }

            fnDecl->add_child(ret_type);
        }

        const std::string start_indent = this->currentindent;

        if (!expect_newline())
        {
            throw std::runtime_error("expected newline after function header");
        }

        get_block(fnDecl, TokenType::expr);

        return fnDecl;
    }

    AST* Parser::parse_parened()
    {
        consume_blanks();

        AST* l_paren = parse_lParen();

        if (!l_paren)
        {
            return NULL;
        }

        AST* expr = parse_expr();

        if (!expr)
        {
            throw std::runtime_error("expected expression within parens");
        }

        AST* r_paren = parse_rParen();

        if (!r_paren)
        {
            throw std::runtime_error("expected closing paren");
        }

        AST* parened = new AST({ TokenType::parened, "" });
        parened->add_child(l_paren);
        parened->add_child(expr);
        parened->add_child(r_paren);

        return parened;
    }

    AST* Parser::parse_case()
    {
        consume_blanks();

        AST* case_keyword = parse_caseKeyword();

        if (!case_keyword)
        {
            return NULL;
        }

        consume_blanks();

        AST* subject_expr = parse_expr();

        if (!subject_expr)
        {
            throw std::runtime_error("expected subject expression for case");
        }

        AST* case_ = new AST({ TokenType::case_, "" });
        case_->add_child(case_keyword);
        case_->add_child(subject_expr);

        get_block(case_, TokenType::case_);

        return case_;
    }

    AST* Parser::parse_ifElse()
    {
        consume_blanks();

        AST* if_keyword = parse_ifKeyword();

        if (!if_keyword)
        {
            return NULL;
        }

        consume_blanks();

        AST* if_condition = parse_expr();

        if (!if_condition)
        {
            throw std::runtime_error("expected expression as if condition");
        }

        AST* ifElse = new AST({ TokenType::ifElse, "" });
        ifElse->add_child(if_keyword);
        ifElse->add_child(if_condition);

        const std::string start_indent = get_block(ifElse, TokenType::expr);

        if (this->currentindent != start_indent)
        {
            return ifElse;
        }

        AST* else_keyword = parse_elseKeyword();

        if (!else_keyword)
        {
            return ifElse;
        }

        ifElse->add_child(else_keyword);

        if (AST* if_else = parse_ifElse())
        {
            ifElse->add_child(if_else);

            return ifElse;
        }

        get_block(ifElse, TokenType::expr);

        return ifElse;
    }

    AST* Parser::parse_try()
    {
        consume_blanks();

        AST* try_keyword = parse_tryKeyword();

        if (!try_keyword)
        {
            return NULL;
        }

        consume_blanks();

        AST* try_ = new AST({ TokenType::try_, "" });
        try_->add_child(try_keyword);

        const std::string start_indent = get_block(try_, TokenType::expr);

        if (this->currentindent != start_indent)
        {
            throw std::runtime_error(
                "try must have corresponsing catch on same indent level"
            );
        }

        AST* catch_keyword = parse_catchKeyword();

        if (!catch_keyword)
        {
            throw std::runtime_error("try must have corresponding catch");
        }

        AST* exception_ident = parse_ident();

        if (!exception_ident)
        {
            throw std::runtime_error("catch must name the caught exception");
        }

        try_->add_child(catch_keyword);
        try_->add_child(exception_ident);

        get_block(try_, TokenType::expr);

        return try_;
    }

    AST* Parser::parse_while()
    {
        consume_blanks();

        AST* while_keyword = parse_whileKeyword();

        if (!while_keyword)
        {
            return NULL;
        }

        consume_blanks();

        AST* while_condition = parse_expr();

        if (!while_condition)
        {
            throw std::runtime_error("expected expression as while condition");
        }

        AST* while_ = new AST({ TokenType::while_, "" });
        while_->add_child(while_keyword);
        while_->add_child(while_condition);

        get_block(while_, TokenType::expr);

        return while_;
    }

    AST* Parser::parse_for()
    {
        consume_blanks();

        AST* for_keyword = parse_forKeyword();

        if (!for_keyword)
        {
            return NULL;
        }

        consume_blanks();

        AST* for_pattern = parse_pattern();

        if (!for_pattern)
        {
            throw std::runtime_error(
                "expected pattern as first part of for header"
            );
        }

        consume_blanks();

        AST* in_keyword = parse_inKeyword();

        if (!in_keyword)
        {
            throw std::runtime_error("missing in keyword of for loop");
        }

        AST* iterated = parse_expr();

        if (!iterated)
        {
            throw std::runtime_error("for must iterate over an expression");
        }

        AST* for_ = new AST({ TokenType::for_, "" });
        for_->add_child(for_keyword);
        for_->add_child(for_pattern);
        for_->add_child(in_keyword);
        for_->add_child(iterated);

        get_block(for_, TokenType::expr);

        return for_;
    }

    AST* Parser::parse_lambda()
    {
        consume_blanks();

        AST* backslash = parse_backslash();

        if (!backslash)
        {
            return NULL;
        }

        AST* first_param = parse_param();

        if (!first_param)
        {
            throw std::runtime_error("lambda expression requires 1+ args");
        }

        AST* lambda = new AST({ TokenType::lambda, "" });
        lambda->add_child(backslash);
        lambda->add_child(first_param);

        consume_blanks();

        while (AST* comma = parse_comma())
        {
            AST* param = parse_param();

            if (!param)
            {
                break;
            }

            lambda->add_child(comma);
            lambda->add_child(param);

            consume_blanks();
        }

        AST* arrow = parse_arrow();

        if (!arrow)
        {
            throw std::runtime_error("lambda expression requires ->");
        }

        AST* expr = parse_expr();

        if (!expr)
        {
            throw std::runtime_error("lambda body must be expression");
        }

        lambda->add_child(arrow);
        lambda->add_child(expr);

        return lambda;
    }

    AST* Parser::parse_listLit()
    {
        consume_blanks();

        AST* lSqBracket = parse_lSqBracket();

        if (!lSqBracket)
        {
            return NULL;
        }

        AST* first_expr = parse_expr();

        AST* listLit = new AST({ TokenType::listLit, "" });
        listLit->add_child(lSqBracket);

        if (first_expr)
        {
            listLit->add_child(first_expr);

            consume_blanks();

            while (AST* comma = parse_comma())
            {
                AST* expr = parse_expr();

                if (!expr)
                {
                    break;
                }

                listLit->add_child(comma);
                listLit->add_child(expr);

                consume_blanks();
            }
        }

        AST* rSqBracket = parse_rSqBracket();

        if (!rSqBracket)
        {
            throw std::runtime_error(
                "left square bracket in list literal requires ]"
            );
        }

        listLit->add_child(rSqBracket);

        return listLit;
    }

    AST* Parser::parse_setLit()
    {
        consume_blanks();

        AST* lCurlyBracket = parse_lCurlyBracket();

        if (!lCurlyBracket)
        {
            return NULL;
        }

        AST* first_expr = parse_expr();

        AST* setLit = new AST({ TokenType::setLit, "" });
        setLit->add_child(lCurlyBracket);

        if (first_expr)
        {
            consume_blanks();

            while (AST* comma = parse_comma())
            {
                AST* expr = parse_expr();

                if (!expr)
                {
                    break;
                }

                setLit->add_child(comma);
                setLit->add_child(expr);

                consume_blanks();
            }
        }

        AST* rCurlyBracket = parse_rCurlyBracket();

        if (!rCurlyBracket)
        {
            throw std::runtime_error(
                "left curly bracket in set literal requires }"
            );
        }

        setLit->add_child(rCurlyBracket);

        return setLit;
    }

    AST* Parser::parse_dictLit()
    {
        consume_blanks();

        AST* lCurlyBracket = parse_lCurlyBracket();

        if (!lCurlyBracket)
        {
            return NULL;
        }

        AST* first_entry = parse_dictEntry();

        AST* dictLit = new AST({ TokenType::dictLit, "" });
        dictLit->add_child(lCurlyBracket);

        if (first_entry)
        {
            consume_blanks();

            while (AST* comma = parse_comma())
            {
                AST* entry = parse_dictEntry();

                if (!entry)
                {
                    break;
                }

                dictLit->add_child(comma);
                dictLit->add_child(entry);

                consume_blanks();
            }
        }

        AST* rCurlyBracket = parse_rCurlyBracket();

        if (!rCurlyBracket)
        {
            throw std::runtime_error(
                "left curly bracket in dict literal requires }"
            );
        }

        dictLit->add_child(rCurlyBracket);

        return dictLit;
    }

    AST* Parser::parse_ident()
    {
        consume_blanks();

        if (!isalpha(this->ch) && this->ch != '_')
        {
            return NULL;
        }

        std::string id = "";

        if (this->ch == '_')
        {
            id.push_back('_');
            advance();

            if (this->ch != '_' && !isalnum(this->ch))
            {
                this->charhistory.push_front(this->ch);
                this->ch = '_';

                return NULL;
            }
        }

        while (isalnum(this->ch) || this->ch == '_')
        {
            id.push_back(this->ch);
            advance();
        }

        return new AST({ TokenType::ident, id });
    }

    AST* Parser::parse_var()
    {
        AST* var_keyword = parse_varKeyword();

        if (!var_keyword)
        {
            return NULL;
        }

        AST* pattern = parse_pattern();

        if (!pattern)
        {
            throw std::runtime_error(
                "left-hand side of var assignment must be a pattern"
            );
        }

        consume_blanks();

        AST* var = new AST({ TokenType::var, "" });
        var->add_child(var_keyword);
        var->add_child(pattern);

        if (AST* colon = parse_colon())
        {
            AST* type = parse_ident();

            if (!type)
            {
                throw std::runtime_error(
                    "type of var binding must be a valid identifier"
                );
            }

            var->add_child(colon);
            var->add_child(type);
        }

        AST* equals = parse_equals();

        if (!equals)
        {
            throw std::runtime_error("var assignment must use =");
        }

        AST* expr = parse_expr();

        if (!expr)
        {
            throw std::runtime_error(
                "right-hand side of var assignment must be a valid expression"
            );
        }

        var->add_child(equals);
        var->add_child(expr);

        return var;
    }

    AST* Parser::parse_assign()
    {
        AST* pattern = parse_pattern();

        if (!pattern)
        {
            return NULL;
        }

        consume_blanks();

        AST* assign = new AST({ TokenType::assign, "" });
        assign->add_child(pattern);

        if (AST* colon = parse_colon())
        {
            AST* type = parse_ident();

            if (!type)
            {
                throw std::runtime_error(
                    "type of binding must be a valid identifier"
                );
            }

            assign->add_child(colon);
            assign->add_child(type);
        }

        AST* equals = parse_equals();

        if (!equals)
        {
            throw std::runtime_error("assignment must use =");
        }

        AST* expr = parse_expr();

        if (!expr)
        {
            throw std::runtime_error(
                "right-hand side of assignment must be a valid expression"
            );
        }

        assign->add_child(equals);
        assign->add_child(expr);

        return assign;
    }

    AST* Parser::parse_pattern()
    {
        AST* first_unit = parse_patUnit();

        if (!first_unit)
        {
            return NULL;
        }

        AST* pattern = new AST({ TokenType::pattern, "" });
        pattern->add_child(first_unit);

        while (AST* unit = parse_patUnit())
        {
            pattern->add_child(unit);
        }

        return pattern;
    }

    AST* Parser::parse_patUnit()
    {
        consume_blanks();

        AST* patUnit = new AST({ TokenType::patUnit, "" });

        if (AST* ident = parse_ident())
        {
            patUnit->add_child(ident);

            return patUnit;
        }

        if (AST* chrLit = parse_chrLit())
        {
            patUnit->add_child(chrLit);

            return patUnit;
        }

        if (AST* strLit = parse_strLit())
        {
            patUnit->add_child(strLit);

            return patUnit;
        }

        if (AST* numLit = parse_numLit())
        {
            patUnit->add_child(numLit);

            return patUnit;
        }

        if (AST* underscore = parse_underscore())
        {
            patUnit->add_child(underscore);

            return patUnit;
        }

        if (AST* lSqBracket = parse_lSqBracket())
        {
            AST* first_patUnit = parse_patUnit();

            patUnit->add_child(lSqBracket);

            if (first_patUnit)
            {
                consume_blanks();

                while (AST* comma = parse_comma())
                {
                    AST* unit = parse_patUnit();

                    if (!unit)
                    {
                        break;
                    }

                    patUnit->add_child(comma);
                    patUnit->add_child(unit);

                    consume_blanks();
                }
            }

            AST* rSqBracket = parse_rSqBracket();

            if (!rSqBracket)
            {
                throw std::runtime_error(
                    "left square bracket in pattern requires ]"
                );
            }

            patUnit->add_child(rSqBracket);

            return patUnit;
        }

        return NULL;
    }

    AST* Parser::parse_chrChr()
    {
        static const std::unordered_set<char> ctrl_chars = {'\'', '\\'};

        if (std::optional<char> char_opt = expect_char_not_of(ctrl_chars))
        {
            return new AST({ TokenType::chrChr, {*char_opt} });
        }

        if (!expect_char('\\'))
        {
            return NULL;
        }

        if (std::optional<char> esc_char_opt = expect_char_of(esc_chars))
        {
            return new AST({ TokenType::chrChr, {'\\', *esc_char_opt} });
        }

        return NULL;
    }

    AST* Parser::parse_strChr()
    {
        static const std::unordered_set<char> ctrl_chars = {'"', '\\'};

        if (std::optional<char> char_opt = expect_char_not_of(ctrl_chars))
        {
            return new AST({ TokenType::strChr, {*char_opt} });
        }

        if (!expect_char('\\'))
        {
            return NULL;
        }

        if (std::optional<char> esc_char_opt = expect_char_of(esc_chars))
        {
            return new AST({ TokenType::strChr, {'\\', *esc_char_opt} });
        }

        return NULL;
    }

    AST* Parser::parse_param()
    {
        consume_blanks();

        if (this->ch == '(')
        {
            AST* l_paren = parse_lParen();

            if (!l_paren)
            {
                throw std::logic_error(
                    "should have successfully parsed left paren"
                );
            }

            AST* pattern = parse_pattern();

            if (!pattern)
            {
                return NULL;
            }

            AST* colon = parse_colon();

            if (!colon)
            {
                return NULL;
            }

            AST* type_ident = parse_ident();

            if (!type_ident)
            {
                throw std::runtime_error("expected type");
            }

            AST* r_paren = parse_rParen();

            if (!r_paren)
            {
                throw std::runtime_error("expected ) after type");
            }

            AST* param = new AST({ TokenType::param, "" });
            param->add_child(l_paren);
            param->add_child(pattern);
            param->add_child(colon);
            param->add_child(type_ident);
            param->add_child(r_paren);

            return param;
        }

        AST* pattern = parse_pattern();

        if (pattern)
        {
            AST* param = new AST({ TokenType::param, "" });
            param->add_child(pattern);

            return param;
        }

        return NULL;
    }

    AST* Parser::parse_dictEntry()
    {
        consume_blanks();

        AST* ident = parse_ident();

        if (!ident)
        {
            return NULL;
        }

        consume_blanks();

        AST* equals = parse_equals();

        if (!equals)
        {
            return NULL;
        }

        AST* val = parse_expr();

        if (!val)
        {
            throw std::runtime_error(
                "expected expression to be assigned to dict key"
            );
        }

        AST* dictEntry = new AST({ TokenType::dictEntry, "" });
        dictEntry->add_child(ident);
        dictEntry->add_child(equals);
        dictEntry->add_child(val);

        return dictEntry;
    }

    AST* Parser::parse_equals()
    {
        if (!expect_char('='))
        {
            return NULL;
        }

        return new AST({ TokenType::equals, "=" });
    }

    AST* Parser::parse_singleQuote()
    {
        if (!expect_char('\''))
        {
            return NULL;
        }

        return new AST({ TokenType::singleQuote, "'" });
    }

    AST* Parser::parse_doubleQuote()
    {
        if (!expect_char('"'))
        {
            return NULL;
        }

        return new AST({ TokenType::doubleQuote, "\"" });
    }

    AST* Parser::parse_fnKeyword()
    {
        if (!expect_keyword("fn"))
        {
            return NULL;
        }

        return new AST({ TokenType::fnKeyword, "fn" });
    }

    AST* Parser::parse_caseKeyword()
    {
        if (!expect_keyword("case"))
        {
            return NULL;
        }

        return new AST({ TokenType::caseKeyword, "case" });
    }

    AST* Parser::parse_ifKeyword()
    {
        if (!expect_keyword("if"))
        {
            return NULL;
        }

        return new AST({ TokenType::ifKeyword, "if" });
    }

    AST* Parser::parse_elseKeyword()
    {
        if (!expect_keyword("else"))
        {
            return NULL;
        }

        return new AST({ TokenType::elseKeyword, "else" });
    }

    AST* Parser::parse_tryKeyword()
    {
        if (!expect_keyword("try"))
        {
            return NULL;
        }

        return new AST({ TokenType::tryKeyword, "try" });
    }

    AST* Parser::parse_catchKeyword()
    {
        if (!expect_keyword("catch"))
        {
            return NULL;
        }

        return new AST({ TokenType::catchKeyword, "catch" });
    }

    AST* Parser::parse_whileKeyword()
    {
        if (!expect_keyword("while"))
        {
            return NULL;
        }

        return new AST({ TokenType::whileKeyword, "while" });
    }

    AST* Parser::parse_forKeyword()
    {
        if (!expect_keyword("for"))
        {
            return NULL;
        }

        return new AST({ TokenType::forKeyword, "for" });
    }

    AST* Parser::parse_inKeyword()
    {
        if (!expect_keyword("in"))
        {
            return NULL;
        }

        return new AST({ TokenType::inKeyword, "in" });
    }

    AST* Parser::parse_varKeyword()
    {
        if (!expect_keyword("var"))
        {
            return NULL;
        }

        return new AST({ TokenType::varKeyword, "var" });
    }

    AST* Parser::parse_comma()
    {
        if (!expect_char(','))
        {
            return NULL;
        }

        return new AST({ TokenType::comma, "," });
    }

    AST* Parser::parse_colon()
    {
        if (!expect_op(":"))
        {
            return NULL;
        }

        return new AST({ TokenType::colon, ":" });
    }

    AST* Parser::parse_underscore()
    {
        if (!expect_keyword("_"))
        {
            return NULL;
        }

        return new AST({ TokenType::underscore, "_" });
    }

    AST* Parser::parse_arrow()
    {
        if (!expect_op("->"))
        {
            return NULL;
        }

        return new AST({ TokenType::arrow, "->" });
    }

    AST* Parser::parse_lParen()
    {
        if (!expect_char('('))
        {
            return NULL;
        }

        return new AST({ TokenType::lParen, "(" });
    }

    AST* Parser::parse_rParen()
    {
        if (!expect_char(')'))
        {
            return NULL;
        }

        return new AST({ TokenType::rParen, ")" });
    }

    AST* Parser::parse_lSqBracket()
    {
        if (!expect_char('['))
        {
            return NULL;
        }

        return new AST({ TokenType::lSqBracket, "[" });
    }

    AST* Parser::parse_rSqBracket()
    {
        if (!expect_char(']'))
        {
            return NULL;
        }

        return new AST({ TokenType::rSqBracket, "]" });
    }

    AST* Parser::parse_lCurlyBracket()
    {
        if (!expect_char('{'))
        {
            return NULL;
        }

        return new AST({ TokenType::lCurlyBracket, "{" });
    }

    AST* Parser::parse_rCurlyBracket()
    {
        if (!expect_char('}'))
        {
            return NULL;
        }

        return new AST({ TokenType::rCurlyBracket, "}" });
    }

    AST* Parser::parse_backslash()
    {
        if (!expect_char('\\'))
        {
            return NULL;
        }

        return new AST({ TokenType::backslash, "\\" });
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

            if (isnewline(this->ch))
            {
                this->currentindent.clear();
            }
            else if (isblank(this->ch))
            {
                this->currentindent.push_back(this->ch);
            }
            else
            {
                return true;
            }
        }

        while (this->charstream >> std::noskipws >> this->ch)
        {
            if (isnewline(this->ch))
            {
                this->currentindent.clear();
            }
            else if (isblank(this->ch))
            {
                this->currentindent.push_back(this->ch);
            }
            else
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
        const std::unordered_set<char>& cs
    ) noexcept
    {
        if (cs.find(this->ch) == cs.end())
        {
            return {};
        }

        const char tmp = this->ch;
        advance();

        return tmp;
    }

    std::optional<char> Parser::expect_char_not_of(
        const std::unordered_set<char>& cs
    ) noexcept
    {
        if (cs.find(this->ch) != cs.end())
        {
            return {};
        }

        const char tmp = this->ch;
        advance();

        return tmp;
    }

    bool Parser::expect_string(const std::string& s) noexcept
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
        size_t history_pushbacks = 1;

        while (i < s.length() && this->charstream >> std::noskipws >> this->ch)
        {
            if (this->ch != s[i])
            {
                return false;
            }

            this->charhistory.push_back(this->ch);
            history_pushbacks++;

            i++;
        }

        this->charstream >> std::noskipws >> this->ch;

        for (size_t i = 0; i < history_pushbacks; ++i)
        {
            this->charhistory.pop_back();
        }

        return i == s.length();
    }

    bool Parser::expect_keyword(const std::string& kwd)
    {
        if (kwd.empty())
        {
            throw std::logic_error("empty keyword");
        }

        size_t i = 0;

        if (this->ch != kwd[i])
        {
            return false;
        }

        i++;
        std::vector<char> historicstack(kwd.length());

        while (i < kwd.length() && !this->charhistory.empty())
        {
            if (this->charhistory.front() != kwd[i])
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

        if (i == kwd.length())
        {
            bool not_keyword;

            if (this->charhistory.empty())
            {
                const char temp_ch = this->ch;

                this->charstream >> std::noskipws >> this->ch;
                not_keyword = isalnum(this->ch) || this->ch == '_';

                this->charhistory.push_back(this->ch);
                this->ch = temp_ch;
            }
            else
            {
                const char front_char = this->charhistory.front();
                not_keyword = isalnum(front_char) || front_char == '_';
            }

            if (not_keyword)
            {
                while (!historicstack.empty())
                {
                    this->charhistory.push_front(this->ch);

                    this->ch = historicstack.back();
                    historicstack.pop_back();
                }

                return false;
            }

            advance();

            return true;
        }

        this->charhistory.push_back(this->ch);
        size_t history_pushbacks = 1;

        while (
            i < kwd.length() &&
            this->charstream >> std::noskipws >> this->ch
        ) {
            if (this->ch != kwd[i])
            {
                while (historicstack.size() > 1)
                {
                    this->charhistory.push_front(historicstack.back());
                    historicstack.pop_back();
                }

                this->ch = historicstack.back();

                return false;
            }

            this->charhistory.push_back(this->ch);
            history_pushbacks++;

            i++;
        }

        this->charstream >> std::noskipws >> this->ch;

        if (isalnum(this->ch) || this->ch == '_')
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
        }

        for (size_t i = 0; i < history_pushbacks; ++i)
        {
            this->charhistory.pop_back();
        }

        return i == kwd.length();
    }

    bool Parser::expect_op(const std::string& op)
    {
        if (op.empty())
        {
            throw std::logic_error("empty operator");
        }

        size_t i = 0;

        if (this->ch != op[i])
        {
            return false;
        }

        i++;
        std::vector<char> historicstack(op.length());

        while (i < op.length() && !this->charhistory.empty())
        {
            if (this->charhistory.front() != op[i])
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

        if (i == op.length())
        {
            bool not_op;

            if (this->charhistory.empty())
            {
                const char temp_ch = this->ch;

                this->charstream >> std::noskipws >> this->ch;
                not_op = op_chars.find(this->ch) != op_chars.end();

                this->charhistory.push_back(this->ch);
                this->ch = temp_ch;
            }
            else
            {
                const char front_char = this->charhistory.front();
                not_op = op_chars.find(front_char) != op_chars.end();
            }

            if (not_op)
            {
                while (!historicstack.empty())
                {
                    this->charhistory.push_front(this->ch);

                    this->ch = historicstack.back();
                    historicstack.pop_back();
                }

                return false;
            }

            advance();

            return true;
        }

        this->charhistory.push_back(this->ch);
        size_t history_pushbacks = 1;

        while (
            i < op.length() &&
            this->charstream >> std::noskipws >> this->ch
        ) {
            if (this->ch != op[i])
            {
                while (historicstack.size() > 1)
                {
                    this->charhistory.push_front(historicstack.back());
                    historicstack.pop_back();
                }

                this->ch = historicstack.back();

                return false;
            }

            this->charhistory.push_back(this->ch);
            history_pushbacks++;

            i++;
        }

        this->charstream >> std::noskipws >> this->ch;

        if (op_chars.find(this->ch) != op_chars.end())
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
        }

        for (size_t i = 0; i < history_pushbacks; ++i)
        {
            this->charhistory.pop_back();
        }

        return i == op.length();
    }

    std::string Parser::get_block(AST* main_ast, TokenType body_item_type)
    {
        const std::string start_indent = this->currentindent;

        if (!expect_newline())
        {
            throw std::runtime_error("expected newline after header");
        }

        const std::string block_indent = this->currentindent;

        if (
            start_indent.length() >= block_indent.length() ||
            !isprefixof(start_indent, block_indent)
        ) {
            throw std::runtime_error(
                "improper indentation after header"
            );
        }

        AST* first_item;

        switch (body_item_type)
        {
            case TokenType::expr:
                first_item = parse_expr();
                break;
            case TokenType::case_:
                first_item = parse_case();
                break;
            default:
                throw std::logic_error("unhandled body item type");
        }

        if (!first_item)
        {
            throw std::runtime_error("expected at least one item in block");
        }

        main_ast->add_child(first_item);

        if (!expect_newline())
        {
            throw std::runtime_error(
                "expected newline after first item of block"
            );
        }

        while (this->currentindent == block_indent)
        {
            AST* item;

            switch (body_item_type)
            {
                case TokenType::expr:
                    item = parse_expr();
                    break;
                case TokenType::case_:
                    item = parse_case();
                    break;
                default:
                    throw std::logic_error("unhandled body item type");
            }

            if (!item)
            {
                throw std::runtime_error("expected item in block");
            }

            main_ast->add_child(item);

            if (!expect_newline())
            {
                throw std::runtime_error("expected newline after block item");
            }
        }

        return start_indent;
    }

    bool Parser::isnewline(char c) noexcept
    {
        return c == '\n' || c == '\r';
    }

    bool Parser::isprefixof(const std::string& a,
                            const std::string& b) noexcept
    {
        if (a.size() <= b.size())
        {
            return b.substr(0, a.size()) == a;
        }

        return false;
    }
}
