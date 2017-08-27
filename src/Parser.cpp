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
        '^', '-', ':', ';'
    };

    const std::unordered_set<std::string> Parser::reserved_ops =
    {
        ":", "->", "=>", "<-", "--", "|", "\\", "=",
        ".", "::"
    };

    Parser::Parser(std::string& filename) : charstream(filename.c_str())
    {
        this->currentindent = "";

        if (!this->charstream.is_open())
        {
            throw std::runtime_error("Failed to open " + filename);
        }
    }

    std::string Parser::str_repr(const AST* ast) noexcept
    {
        if (!ast->val().lexeme.empty())
        {
            return ast->val().lexeme;
        }

        std::string ret = "";
        const size_t child_count = ast->child_count();

        for (size_t i = 0; i < child_count; ++i)
        {
            ret += str_repr(ast->get_child(i));
            ret.push_back(' ');
        }

        return ret;
    }

    void Parser::log_depthfirst(const AST* ast, size_t cur_depth)
    {
        for (size_t i = 0; i < cur_depth; ++i)
        {
            std::cout << "  ";
        }

        const std::string& lex = ast->val().lexeme;

        if (lex.empty())
        {
            std::cout << u8" └─ "
                      << token_type_names.at(ast->val().type)
                      << '\n';
        }
        else
        {
            std::cout << u8" └─ "
                      << token_type_names.at(ast->val().type)
                      << " \""
                      << lex
                      << "\"\n";
        }

        const size_t child_count = ast->child_count();

        for (size_t i = 0; i < child_count; ++i)
        {
            log_depthfirst(ast->get_child(i), cur_depth + 1);
        }
    }

    AST* Parser::parse()
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
            return nullptr;
        }

        mainAst->add_child(prog);

        return mainAst;
    }

    AST* Parser::parse_prog()
    {
        AST* prog = new AST({ TokenType::prog, "" });

        AST* module_decl = parse_modDecl();

        if (module_decl)
        {
            prog->add_child(module_decl);
        }

        while (!this->charstream.eof() || !this->charhistory.empty())
        {
            AST* import = parse_import();

            if (!import)
            {
                break;
            }

            prog->add_child(import);
        }

        while (!this->charstream.eof() || !this->charhistory.empty())
        {
            AST* line = parse_line();

            if (!line)
            {
                break;
            }

            prog->add_child(line);
        }

        return prog;
    }

    AST* Parser::parse_modDecl()
    {
        AST* module_keyword = parse_moduleKeyword();

        if (!module_keyword)
        {
            return nullptr;
        }

        AST* mod_decl = new AST({ TokenType::modDecl, "" });
        mod_decl->add_child(module_keyword);

        AST* mod_name = parse_ident();

        if (!mod_name)
        {
            throw std::runtime_error(
                "expected name of module to be plain identifier"
            );
        }

        mod_decl->add_child(mod_name);

        AST* exporting_keyword = parse_exportingKeyword();
        AST* hiding_keyword = nullptr;

        if (!exporting_keyword)
        {
            hiding_keyword = parse_hidingKeyword();
        }

        if (exporting_keyword || hiding_keyword)
        {
            if (exporting_keyword)
            {
                mod_decl->add_child(exporting_keyword);
            }
            else
            {
                mod_decl->add_child(hiding_keyword);
            }

            AST* first_ident = parse_ident();

            if (!first_ident)
            {
                throw std::runtime_error(
                    "expected at least one item in module export/hide list"
                );
            }

            mod_decl->add_child(first_ident);

            consume_blanks();

            while (AST* comma = parse_comma())
            {
                AST* ident = parse_ident();

                if (!ident)
                {
                    break;
                }

                mod_decl->add_child(comma);
                mod_decl->add_child(ident);

                consume_blanks();
            }
        }

        if (!expect_newline())
        {
            throw std::runtime_error(
                "expected newline after module declaration"
            );
        }

        return mod_decl;
    }

    AST* Parser::parse_import()
    {
        AST* import_keyword = parse_importKeyword();

        if (!import_keyword)
        {
            return nullptr;
        }

        AST* import = new AST({ TokenType::import, "" });
        import->add_child(import_keyword);

        AST* mod_name = parse_ident();

        if (!mod_name)
        {
            throw std::runtime_error(
                "expected module name after import keyword"
            );
        }

        import->add_child(mod_name);

        AST* as_keyword = parse_asKeyword();

        if (as_keyword)
        {
            import->add_child(as_keyword);

            AST* qual_name = parse_ident();

            if (!qual_name)
            {
                throw std::runtime_error(
                    "expected namespace alias after as keyword"
                );
            }

            import->add_child(qual_name);
        }
        else
        {
            AST* hiding_keyword = parse_hidingKeyword();

            if (hiding_keyword)
            {
                import->add_child(hiding_keyword);
            }

            consume_blanks();

            AST* l_paren = parse_lParen();

            if (!l_paren)
            {
                throw std::runtime_error(
                    "expected left paren to start import list"
                );
            }

            import->add_child(l_paren);

            AST* first_import_item = parse_ident();

            if (!first_import_item)
            {
                throw std::runtime_error(
                    "expected at least one import item in import list"
                );
            }

            import->add_child(first_import_item);

            consume_blanks();

            while (AST* comma = parse_comma())
            {
                AST* import_item = parse_ident();

                if (!import_item)
                {
                    break;
                }

                import->add_child(comma);
                import->add_child(import_item);

                consume_blanks();
            }

            consume_blanks();

            AST* r_paren = parse_rParen();

            if (!r_paren)
            {
                throw std::runtime_error(
                    "expected right paren to terminate import list"
                );
            }

            import->add_child(r_paren);
        }

        if (!expect_newline())
        {
            throw std::runtime_error(
                "expected newline after import statement"
            );
        }

        return import;
    }

    AST* Parser::parse_line()
    {
        consume_blanks();

        AST* line = new AST({ TokenType::line, "" });

        AST* expr = parse_expr();

        if (expr)
        {
            line->add_child(expr);
        }

        consume_lineComment();

        return line;
    }

    bool Parser::consume_lineComment()
    {
        consume_blanks();

        if (!consume_lineCommentOp())
        {
            return false;
        }

        if (isnewline(this->ch))
        {
            expect_newline();

            return true;
        }

        while (!this->charhistory.empty())
        {
            this->ch = this->charhistory.front();
            this->charhistory.pop_front();

            if (isnewline(this->ch))
            {
                expect_newline();

                return true;
            }
        }

        while (this->charstream >> std::noskipws >> this->ch)
        {
            if (isnewline(this->ch))
            {
                expect_newline();

                return true;
            }
        }

        return true;
    }

    AST* Parser::parse_expr()
    {
        consume_blanks();

        AST* first_subexpr = parse_subexpr();

        if (!first_subexpr)
        {
            return nullptr;
        }

        AST* expr = new AST({ TokenType::expr, "" });
        expr->add_child(first_subexpr);

        while (!expect_newline())
        {
            AST* subexpr = parse_subexpr();

            if (!subexpr)
            {
                throw std::runtime_error(
                    "missing subexpression in expression"
                );
            }

            expr->add_child(subexpr);
        }

        return expr;
    }

    AST* Parser::parse_subexpr()
    {
        consume_blanks();

        AST* subexpr = new AST({ TokenType::subexpr, "" });

        if (AST* var = parse_var())
        {
            subexpr->add_child(var);
        }
        else if (AST* assign = parse_assign())
        {
            subexpr->add_child(assign);
        }
        else if (AST* fnDecl = parse_fnDecl())
        {
            subexpr->add_child(fnDecl);
        }
        else if (AST* parened = parse_parened())
        {
            subexpr->add_child(parened);
        }
        else if (AST* return_ = parse_return())
        {
            subexpr->add_child(return_);
        }
        else if (AST* case_ = parse_case())
        {
            subexpr->add_child(case_);
        }
        else if (AST* ifElse = parse_ifElse())
        {
            subexpr->add_child(ifElse);
        }
        else if (AST* try_ = parse_try())
        {
            subexpr->add_child(try_);
        }
        else if (AST* while_ = parse_while())
        {
            subexpr->add_child(while_);
        }
        else if (AST* for_ = parse_for())
        {
            subexpr->add_child(for_);
        }
        else if (AST* lambda = parse_lambda())
        {
            subexpr->add_child(lambda);
        }
        else if (AST* tupleLit = parse_tupleLit())
        {
            subexpr->add_child(tupleLit);
        }
        else if (AST* listLit = parse_listLit())
        {
            subexpr->add_child(listLit);
        }
        else if (AST* listComp = parse_listComp())
        {
            subexpr->add_child(listComp);
        }
        else if (AST* dictLit = parse_dictLit())
        {
            subexpr->add_child(dictLit);
        }
        else if (AST* dictComp = parse_dictComp())
        {
            subexpr->add_child(dictComp);
        }
        else if (AST* setLit = parse_setLit())
        {
            subexpr->add_child(setLit);
        }
        else if (AST* setComp = parse_setComp())
        {
            subexpr->add_child(setComp);
        }
        else if (AST* qual_ident = parse_qualIdent())
        {
            subexpr->add_child(qual_ident);
        }
        else if (AST* op = parse_op())
        {
            subexpr->add_child(op);
        }
        else if (AST* infixed = parse_infixed())
        {
            subexpr->add_child(infixed);
        }
        else if (AST* numLit = parse_numLit())
        {
            subexpr->add_child(numLit);
        }
        else if (AST* chrLit = parse_chrLit())
        {
            subexpr->add_child(chrLit);
        }
        else if (AST* strLit = parse_strLit())
        {
            subexpr->add_child(strLit);
        }
        else
        {
            return nullptr;
        }

        return subexpr;
    }

    AST* Parser::parse_var()
    {
        AST* var_keyword = parse_varKeyword();

        if (!var_keyword)
        {
            return nullptr;
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
            AST* type = parse_qualIdent();

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
            return nullptr;
        }

        consume_blanks();

        AST* assign = new AST({ TokenType::assign, "" });
        assign->add_child(pattern);

        if (AST* colon = parse_colon())
        {
            AST* type = parse_qualIdent();

            if (!type)
            {
                throw std::runtime_error(
                    "type of binding must be a valid identifier"
                );
            }

            assign->add_child(colon);
            assign->add_child(type);
        }

        consume_blanks();

        AST* equals = parse_equals();

        if (!equals)
        {
            this->charhistory.push_front(this->ch);
            this->charhistory.push_front(' ');

            std::string consumed_pattern = str_repr(pattern);

            while (consumed_pattern.length() > 1)
            {
                this->charhistory.push_front(consumed_pattern.back());
                consumed_pattern.pop_back();
            }

            this->ch = consumed_pattern.back();

            return nullptr;
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

    AST* Parser::parse_fnDecl()
    {
        consume_blanks();

        AST* fn_keyword = parse_fnKeyword();

        if (!fn_keyword)
        {
            return nullptr;
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

            AST* ret_type = parse_qualIdent();

            if (!ret_type)
            {
                throw std::runtime_error("expected type after arrow");
            }

            fnDecl->add_child(ret_type);
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
            return nullptr;
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

    AST* Parser::parse_return()
    {
        consume_blanks();

        AST* return_keyword = parse_returnKeyword();

        if (!return_keyword)
        {
            return nullptr;
        }

        AST* expr = prase_expr();

        if (!expr)
        {
            throw std::runtime_error("expected expression to return");
        }

        AST* return_ = new AST({ TokenType::return_, "" });
        return_->add_child(return_keyword);
        return_->add_child(expr);

        return return_;
    }

    AST* Parser::parse_case()
    {
        consume_blanks();

        AST* case_keyword = parse_caseKeyword();

        if (!case_keyword)
        {
            return nullptr;
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

        get_block(case_, TokenType::caseBranch);

        return case_;
    }

    AST* Parser::parse_ifElse()
    {
        consume_blanks();

        AST* if_keyword = parse_ifKeyword();

        if (!if_keyword)
        {
            return nullptr;
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
            return nullptr;
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
            return nullptr;
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
            return nullptr;
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
            return nullptr;
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

    AST* Parser::parse_tupleLit()
    {
        consume_blanks();

        AST* l_paren = parse_lParen();

        if (!l_paren)
        {
            return nullptr;
        }

        AST* first_expr = parse_expr();

        AST* tupleLit = new AST({ TokenType::tupleLit, "" });
        tupleLit->add_child(l_paren);

        consume_blanks();

        if (first_expr)
        {
            AST* first_comma = parse_comma();

            if (!first_comma)
            {
                throw std::runtime_error(
                    "expected comma after first tuple element"
                );
            }

            AST* second_expr = parse_expr();

            if (!second_expr)
            {
                throw std::runtime_error(
                    "expected 0 or at least 2 elements in tuple"
                );
            }

            tupleLit->add_child(first_expr);
            tupleLit->add_child(first_comma);
            tupleLit->add_child(second_expr);

            consume_blanks();

            while (AST* comma = parse_comma())
            {
                AST* expr = parse_expr();

                if (!expr)
                {
                    break;
                }

                tupleLit->add_child(comma);
                tupleLit->add_child(expr);

                consume_blanks();
            }
        }

        AST* r_paren = parse_rParen();

        if (!r_paren)
        {
            throw std::runtime_error(
                "expected right paren to terminate tuple"
            );
        }

        tupleLit->add_child(r_paren);

        return tupleLit;
    }

    AST* Parser::parse_listLit()
    {
        consume_blanks();

        AST* l_sq_bracket = parse_lSqBracket();

        if (!l_sq_bracket)
        {
            return nullptr;
        }

        AST* first_expr = parse_expr();

        AST* listLit = new AST({ TokenType::listLit, "" });
        listLit->add_child(l_sq_bracket);

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

        AST* r_sq_bracket = parse_rSqBracket();

        if (!r_sq_bracket)
        {
            throw std::runtime_error(
                "left square bracket in list literal requires ]"
            );
        }

        listLit->add_child(r_sq_bracket);

        return listLit;
    }

    AST* Parser::parse_listComp()
    {
        consume_blanks();

        AST* l_sq_bracket = parse_lSqBracket();

        if (!l_sq_bracket)
        {
            return nullptr;
        }

        AST* expr = parse_expr();

        if (!expr)
        {
            throw std::runtime_error(
                "expected expression on left-hand side of list comprehension"
            );
        }

        AST* bar = parse_bar();

        if (!bar)
        {
            throw std::runtime_error("expected | for list comprehension");
        }

        AST* listComp = new AST({ TokenType::listComp, "" });
        listComp->add_child(l_sq_bracket);
        listComp->add_child(expr);
        listComp->add_child(bar);

        AST* first_generator = parse_generator();
        AST* first_condition = nullptr;

        if (!first_generator)
        {
            first_condition = parse_expr();
        }

        if (first_generator || first_condition)
        {
            if (first_generator)
            {
                listComp->add_child(first_generator);
            }
            else
            {
                listComp->add_child(first_condition);
            }

            consume_blanks();

            while (AST* comma = parse_comma())
            {
                if (AST* generator = parse_generator())
                {
                    listComp->add_child(generator);
                }
                else if (AST* condition = parse_expr())
                {
                    listComp->add_child(condition);
                }
                else
                {
                    break;
                }

                consume_blanks();
            }
        }

        AST* r_sq_bracket = parse_rSqBracket();

        if (!r_sq_bracket)
        {
            throw std::runtime_error(
                "expected ] to terminate list comprehension"
            );
        }

        listComp->add_child(r_sq_bracket);

        return listComp;
    }

    AST* Parser::parse_dictLit()
    {
        consume_blanks();

        AST* l_curly_bracket = parse_lCurlyBracket();

        if (!l_curly_bracket)
        {
            return nullptr;
        }

        AST* first_entry = parse_dictEntry();

        AST* dictLit = new AST({ TokenType::dictLit, "" });
        dictLit->add_child(l_curly_bracket);

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

        AST* r_curly_bracket = parse_rCurlyBracket();

        if (!r_curly_bracket)
        {
            throw std::runtime_error(
                "left curly bracket in dict literal requires }"
            );
        }

        dictLit->add_child(r_curly_bracket);

        return dictLit;
    }

    AST* Parser::parse_dictComp()
    {
        consume_blanks();

        AST* l_curly_bracket = parse_lCurlyBracket();

        if (!l_curly_bracket)
        {
            return nullptr;
        }

        AST* entry = parse_dictEntry();

        if (!entry)
        {
            throw std::runtime_error(
                "expected entry on left-hand side of dict comprehension"
            );
        }

        AST* bar = parse_bar();

        if (!bar)
        {
            throw std::runtime_error("expected | for dict comprehension");
        }

        AST* dictComp = new AST({ TokenType::dictComp, "" });
        dictComp->add_child(l_curly_bracket);
        dictComp->add_child(entry);
        dictComp->add_child(bar);

        AST* first_generator = parse_generator();
        AST* first_condition = nullptr;

        if (!first_generator)
        {
            first_condition = parse_expr();
        }

        if (first_generator || first_condition)
        {
            if (first_generator)
            {
                dictComp->add_child(first_generator);
            }
            else
            {
                dictComp->add_child(first_condition);
            }

            consume_blanks();

            while (AST* comma = parse_comma())
            {
                if (AST* generator = parse_generator())
                {
                    dictComp->add_child(generator);
                }
                else if (AST* condition = parse_expr())
                {
                    dictComp->add_child(condition);
                }
                else
                {
                    break;
                }

                consume_blanks();
            }
        }

        AST* r_curly_bracket = parse_rCurlyBracket();

        if (!r_curly_bracket)
        {
            throw std::runtime_error(
                "expected } to terminate dict comprehension"
            );
        }

        dictComp->add_child(r_curly_bracket);

        return dictComp;
    }

    AST* Parser::parse_setLit()
    {
        consume_blanks();

        AST* l_curly_bracket = parse_lCurlyBracket();

        if (!l_curly_bracket)
        {
            return nullptr;
        }

        AST* first_expr = parse_expr();

        AST* setLit = new AST({ TokenType::setLit, "" });
        setLit->add_child(l_curly_bracket);

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

        AST* r_curly_bracket = parse_rCurlyBracket();

        if (!r_curly_bracket)
        {
            throw std::runtime_error(
                "left curly bracket in set literal requires }"
            );
        }

        setLit->add_child(r_curly_bracket);

        return setLit;
    }

    AST* Parser::parse_setComp()
    {
        consume_blanks();

        AST* l_curly_bracket = parse_lCurlyBracket();

        if (!l_curly_bracket)
        {
            return nullptr;
        }

        AST* expr = parse_expr();

        if (!expr)
        {
            throw std::runtime_error(
                "expected expression on left-hand side of set comprehension"
            );
        }

        AST* bar = parse_bar();

        if (!bar)
        {
            throw std::runtime_error("expected | for set comprehension");
        }

        AST* setComp = new AST({ TokenType::setComp, "" });
        setComp->add_child(l_curly_bracket);
        setComp->add_child(expr);
        setComp->add_child(bar);

        AST* first_generator = parse_generator();
        AST* first_condition = nullptr;

        if (!first_generator)
        {
            first_condition = parse_expr();
        }

        if (first_generator || first_condition)
        {
            if (first_generator)
            {
                setComp->add_child(first_generator);
            }
            else
            {
                setComp->add_child(first_condition);
            }

            consume_blanks();

            while (AST* comma = parse_comma())
            {
                if (AST* generator = parse_generator())
                {
                    setComp->add_child(generator);
                }
                else if (AST* condition = parse_expr())
                {
                    setComp->add_child(condition);
                }
                else
                {
                    break;
                }

                consume_blanks();
            }
        }

        AST* r_curly_bracket = parse_rCurlyBracket();

        if (!r_curly_bracket)
        {
            throw std::runtime_error(
                "expected } to terminate set comprehension"
            );
        }

        setComp->add_child(r_curly_bracket);

        return setComp;
    }

    AST* Parser::parse_qualIdent()
    {
        consume_blanks();

        if (AST* member_ident = parse_memberIdent())
        {
            AST* qual_ident = new AST({ TokenType::qualIdent, "" });
            qual_ident->add_child(member_ident);

            return qual_ident;
        }

        if (AST* scoped_ident = parse_scopedIdent())
        {
            AST* qual_ident = new AST({ TokenType::qualIdent, "" });
            qual_ident->add_child(scoped_ident);

            return qual_ident;
        }

        if (AST* ident = parse_ident())
        {
            AST* qual_ident = new AST({ TokenType::qualIdent, "" });
            qual_ident->add_child(ident);

            return qual_ident;
        }

        return nullptr;
    }

    AST* Parser::parse_namespacedIdent()
    {
        consume_blanks();

        if (AST* scoped_ident = parse_scopedIdent())
        {
            AST* namespaced_ident =
                new AST({ TokenType::namespacedIdent, "" });
            namespaced_ident->add_child(scoped_ident);

            return namespaced_ident;
        }

        if (AST* ident = parse_ident())
        {
            AST* namespaced_ident =
                new AST({ TokenType::namespacedIdent, "" });
            namespaced_ident->add_child(ident);

            return namespaced_ident;
        }

        return nullptr;
    }

    AST* Parser::parse_ident()
    {
        consume_blanks();

        if (!isalpha(this->ch) && this->ch != '_')
        {
            return nullptr;
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

                return nullptr;
            }
        }

        while (isalnum(this->ch) || this->ch == '_')
        {
            id.push_back(this->ch);

            if (advance())
            {
                break;
            }
        }

        return new AST({ TokenType::ident, id });
    }

    AST* Parser::parse_memberIdent()
    {
        AST* first_ident = parse_ident();

        if (!first_ident)
        {
            return nullptr;
        }

        AST* dot = parse_dot();

        if (!dot)
        {
            const std::string& first_ident_lex = first_ident->val().lexeme;

            for (auto i = first_ident_lex.length() - 1; i >= 0; --i)
            {
                this->charhistory.push_front(first_ident_lex[i]);
            }

            return nullptr;
        }

        AST* second_ident = parse_ident();

        if (!second_ident)
        {
            throw std::runtime_error("expected identifier after dot operator");
        }

        AST* memberIdent = new AST({ TokenType::memberIdent, "" });
        memberIdent->add_child(first_ident);
        memberIdent->add_child(dot);
        memberIdent->add_child(second_ident);

        return memberIdent;
    }

    AST* Parser::parse_scopedIdent()
    {
        AST* first_ident = parse_ident();

        if (!first_ident)
        {
            return nullptr;
        }

        AST* double_colon = parse_doubleColon();

        if (!double_colon)
        {
            const std::string& first_ident_lex = first_ident->val().lexeme;

            for (auto i = first_ident_lex.length() - 1; i >= 0; --i)
            {
                this->charhistory.push_front(first_ident_lex[i]);
            }

            return nullptr;
        }

        AST* second_ident = parse_ident();

        if (!second_ident)
        {
            throw std::runtime_error("expected identifier after dot operator");
        }

        AST* scopedIdent = new AST({ TokenType::scopedIdent, "" });
        scopedIdent->add_child(first_ident);
        scopedIdent->add_child(double_colon);
        scopedIdent->add_child(second_ident);

        return scopedIdent;
    }

    AST* Parser::parse_typeIdent()
    {
        if (AST* namespaced_ident = parse_namespacedIdent())
        {
            AST* type_ident = new AST({ TokenType::typeIdent, "" });
            type_ident->add_child(namespaced_ident);

            return type_ident;
        }

        if (AST* l_paren = parse_lParen())
        {
            AST* first_ident = parse_typeIdent();

            AST* typeIdent = new AST({ TokenType::typeIdent, "" });
            typeIdent->add_child(l_paren);

            consume_blanks();

            if (first_ident)
            {
                AST* first_comma = parse_comma();

                if (!first_comma)
                {
                    throw std::runtime_error(
                        "expected comma after first type tuple element"
                    );
                }

                AST* second_ident = parse_typeIdent();

                if (!second_ident)
                {
                    throw std::runtime_error(
                        "expected 0 or at least 2 elements in type tuple"
                    );
                }

                typeIdent->add_child(first_ident);
                typeIdent->add_child(first_comma);
                typeIdent->add_child(second_ident);

                consume_blanks();

                while (AST* comma = parse_comma())
                {
                    AST* ident = parse_typeIdent();

                    if (!ident)
                    {
                        break;
                    }

                    typeIdent->add_child(comma);
                    typeIdent->add_child(ident);

                    consume_blanks();
                }
            }

            AST* r_paren = parse_rParen();

            if (!r_paren)
            {
                throw std::runtime_error(
                    "expected right paren to terminate type tuple"
                );
            }

            typeIdent->add_child(r_paren);

            return typeIdent;
        }

        if (AST* l_sq_bracket = parse_lSqBracket())
        {
            AST* ident = parse_typeIdent();

            if (!ident)
            {
                throw std::runtime_error("expected type identifier after [");
            }

            AST* r_sq_bracket = parse_rSqBracket();

            if (!r_sq_bracket)
            {
                throw std::runtime_error("expected closing ] of list type");
            }

            AST* typeIdent = new AST({ TokenType::typeIdent, "" });
            typeIdent->add_child(l_sq_bracket);
            typeIdent->add_child(ident);
            typeIdent->add_child(r_sq_bracket);

            return typeIdent;
        }

        if (AST* l_curly_bracket = parse_lCurlyBracket())
        {
            AST* ident = parse_typeIdent();

            if (!ident)
            {
                throw std::runtime_error("expected type identifier after {");
            }

            consume_blanks();

            AST* typeIdent = new AST({ TokenType::typeIdent, "" });
            typeIdent->add_child(l_curly_bracket);
            typeIdent->add_child(ident);

            AST* comma = parse_comma();

            if (comma)
            {
                AST* second_ident = parse_typeIdent();

                if (!second_ident)
                {
                    throw std::runtime_error(
                        "expected type identifier after ,"
                    );
                }

                typeIdent->add_child(comma);
                typeIdent->add_child(second_ident);
            }

            AST* r_curly_bracket = parse_rCurlyBracket();

            if (!r_curly_bracket)
            {
                throw std::runtime_error(
                    "expected closing } of dict/set type"
                );
            }

            typeIdent->add_child(r_curly_bracket);

            return typeIdent;
        }

        return nullptr;
    }

    AST* Parser::parse_op()
    {
        consume_blanks();

        std::string op = "";

        while (optional<char> op_char = expect_char_of(op_chars))
        {
            op.push_back(*op_char);
        }

        if (op.empty())
        {
            return nullptr;
        }

        if (reserved_ops.find(op) != reserved_ops.end())
        {
            throw std::runtime_error("the operator " + op + " is reserved");
        }

        return new AST({ TokenType::op, op });
    }

    AST* Parser::parse_numLit()
    {
        consume_blanks();

        AST* minus = nullptr;

        if (expect_op("-"))
        {
            minus = new AST({ TokenType::minus, "-" });

            consume_blanks();
        }

        if (expect_keyword("NaN"))
        {
            AST* numLit = new AST({ TokenType::numLit, "" });
            AST* real_lit = new AST({ TokenType::realLit, "" });

            if (minus)
            {
                real_lit->add_child(minus);
            }

            real_lit->add_child(new AST({ TokenType::nanKeyword, "NaN" }));
            numLit->add_child(real_lit);

            return numLit;
        }

        if (expect_keyword("Infinity"))
        {
            AST* numLit = new AST({ TokenType::numLit, "" });
            AST* real_lit = new AST({ TokenType::realLit, "" });

            if (minus)
            {
                real_lit->add_child(minus);
            }

            real_lit->add_child(
                new AST({ TokenType::infinityKeyword, "Infinity" })
            );
            numLit->add_child(real_lit);

            return numLit;
        }

        if (!isdigit(this->ch))
        {
            this->charhistory.push_front(' ');
            this->charhistory.push_front('-');

            return nullptr;
        }

        std::string s = "";

        while (isdigit(this->ch))
        {
            s.push_back(this->ch);

            if (advance())
            {
                break;
            }
        }

        if (this->ch != '.')
        {
            AST* numLit = new AST({ TokenType::numLit, "" });
            AST* int_lit = new AST({ TokenType::intLit, "" });

            if (minus)
            {
                int_lit->add_child(minus);
            }

            int_lit->add_child(new AST({ TokenType::absInt, s }));
            numLit->add_child(int_lit);

            return numLit;
        }

        s.push_back(this->ch);
        advance();

        if (!isdigit(this->ch))
        {
            throw std::runtime_error(
                "expected at least one digit after decimal point"
            );
        }

        while (isdigit(this->ch))
        {
            s.push_back(this->ch);

            if (advance())
            {
                break;
            }
        }

        AST* numLit = new AST({ TokenType::numLit, "" });
        AST* real_lit = new AST({ TokenType::realLit, "" });

        if (minus)
        {
            real_lit->add_child(minus);
        }

        real_lit->add_child(new AST({ TokenType::absReal, s }));
        numLit->add_child(real_lit);

        return numLit;
    }

    AST* Parser::parse_chrLit()
    {
        consume_blanks();

        AST* init_singleQuote = parse_singleQuote();

        if (!init_singleQuote)
        {
            return nullptr;
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
            return nullptr;
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

    AST* Parser::parse_infixed()
    {
        consume_blanks();

        AST* first_backtick = parse_backtick();

        if (!first_backtick)
        {
            return nullptr;
        }

        AST* ident = parse_qualIdent();

        if (!ident)
        {
            throw std::runtime_error("expected identifier after `");
        }

        AST* second_backtick = parse_backtick();

        if (!second_backtick)
        {
            throw std::runtime_error("expected closing `");
        }

        AST* infixed = new AST({ TokenType::infixed, "" });
        infixed->add_child(first_backtick);
        infixed->add_child(ident);
        infixed->add_child(second_backtick);

        return infixed;
    }

    AST* Parser::parse_pattern()
    {
        consume_blanks();

        AST* pattern = new AST({ TokenType::pattern, "" });

        if (AST* ident = parse_ident())
        {
            pattern->add_child(ident);

            return pattern;
        }

        if (AST* chrLit = parse_chrLit())
        {
            pattern->add_child(chrLit);

            return pattern;
        }

        if (AST* strLit = parse_strLit())
        {
            pattern->add_child(strLit);

            return pattern;
        }

        if (AST* numLit = parse_numLit())
        {
            pattern->add_child(numLit);

            return pattern;
        }

        if (AST* underscore = parse_underscore())
        {
            pattern->add_child(underscore);

            return pattern;
        }

        /////////////////////////////////////////////////////

        if (AST* lSqBracket = parse_lSqBracket())
        {
            AST* first_pattern = parse_pattern();

            pattern->add_child(lSqBracket);

            if (first_pattern)
            {
                pattern->add_child(first_pattern);

                consume_blanks();

                while (AST* comma = parse_comma())
                {
                    AST* unit = parse_pattern();

                    if (!unit)
                    {
                        break;
                    }

                    pattern->add_child(comma);
                    pattern->add_child(unit);

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

            pattern->add_child(rSqBracket);

            return pattern;
        }

        return nullptr;
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
            return nullptr;
        }

        if (std::optional<char> esc_char_opt = expect_char_of(esc_chars))
        {
            return new AST({ TokenType::chrChr, {'\\', *esc_char_opt} });
        }

        return nullptr;
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
            return nullptr;
        }

        if (std::optional<char> esc_char_opt = expect_char_of(esc_chars))
        {
            return new AST({ TokenType::strChr, {'\\', *esc_char_opt} });
        }

        return nullptr;
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
                return nullptr;
            }

            AST* colon = parse_colon();

            if (!colon)
            {
                return nullptr;
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

        return nullptr;
    }

    AST* Parser::parse_dictEntry()
    {
        consume_blanks();

        AST* key = parse_expr();

        if (!key)
        {
            return nullptr;
        }

        consume_blanks();

        AST* equals = parse_equals();

        if (!equals)
        {
            return nullptr;
        }

        AST* val = parse_expr();

        if (!val)
        {
            throw std::runtime_error(
                "expected expression to be assigned to dict key"
            );
        }

        AST* dictEntry = new AST({ TokenType::dictEntry, "" });
        dictEntry->add_child(key);
        dictEntry->add_child(equals);
        dictEntry->add_child(val);

        return dictEntry;
    }

    AST* Parser::parse_equals()
    {
        if (!expect_char('='))
        {
            return nullptr;
        }

        return new AST({ TokenType::equals, "=" });
    }

    AST* Parser::parse_singleQuote()
    {
        if (!expect_char('\''))
        {
            return nullptr;
        }

        return new AST({ TokenType::singleQuote, "'" });
    }

    AST* Parser::parse_doubleQuote()
    {
        if (!expect_char('"'))
        {
            return nullptr;
        }

        return new AST({ TokenType::doubleQuote, "\"" });
    }

    AST* Parser::parse_fnKeyword()
    {
        if (!expect_keyword("fn"))
        {
            return nullptr;
        }

        return new AST({ TokenType::fnKeyword, "fn" });
    }

    AST* Parser::parse_caseKeyword()
    {
        if (!expect_keyword("case"))
        {
            return nullptr;
        }

        return new AST({ TokenType::caseKeyword, "case" });
    }

    AST* Parser::parse_ifKeyword()
    {
        if (!expect_keyword("if"))
        {
            return nullptr;
        }

        return new AST({ TokenType::ifKeyword, "if" });
    }

    AST* Parser::parse_elseKeyword()
    {
        if (!expect_keyword("else"))
        {
            return nullptr;
        }

        return new AST({ TokenType::elseKeyword, "else" });
    }

    AST* Parser::parse_tryKeyword()
    {
        if (!expect_keyword("try"))
        {
            return nullptr;
        }

        return new AST({ TokenType::tryKeyword, "try" });
    }

    AST* Parser::parse_catchKeyword()
    {
        if (!expect_keyword("catch"))
        {
            return nullptr;
        }

        return new AST({ TokenType::catchKeyword, "catch" });
    }

    AST* Parser::parse_whileKeyword()
    {
        if (!expect_keyword("while"))
        {
            return nullptr;
        }

        return new AST({ TokenType::whileKeyword, "while" });
    }

    AST* Parser::parse_forKeyword()
    {
        if (!expect_keyword("for"))
        {
            return nullptr;
        }

        return new AST({ TokenType::forKeyword, "for" });
    }

    AST* Parser::parse_inKeyword()
    {
        if (!expect_keyword("in"))
        {
            return nullptr;
        }

        return new AST({ TokenType::inKeyword, "in" });
    }

    AST* Parser::parse_varKeyword()
    {
        if (!expect_keyword("var"))
        {
            return nullptr;
        }

        return new AST({ TokenType::varKeyword, "var" });
    }

    AST* Parser::parse_comma()
    {
        if (!expect_char(','))
        {
            return nullptr;
        }

        return new AST({ TokenType::comma, "," });
    }

    AST* Parser::parse_colon()
    {
        if (!expect_op(":"))
        {
            return nullptr;
        }

        return new AST({ TokenType::colon, ":" });
    }

    AST* Parser::parse_underscore()
    {
        if (!expect_keyword("_"))
        {
            return nullptr;
        }

        return new AST({ TokenType::underscore, "_" });
    }

    AST* Parser::parse_arrow()
    {
        if (!expect_op("->"))
        {
            return nullptr;
        }

        return new AST({ TokenType::arrow, "->" });
    }

    AST* Parser::parse_lParen()
    {
        if (!expect_char('('))
        {
            return nullptr;
        }

        return new AST({ TokenType::lParen, "(" });
    }

    AST* Parser::parse_rParen()
    {
        if (!expect_char(')'))
        {
            return nullptr;
        }

        return new AST({ TokenType::rParen, ")" });
    }

    AST* Parser::parse_lSqBracket()
    {
        if (!expect_char('['))
        {
            return nullptr;
        }

        return new AST({ TokenType::lSqBracket, "[" });
    }

    AST* Parser::parse_rSqBracket()
    {
        if (!expect_char(']'))
        {
            return nullptr;
        }

        return new AST({ TokenType::rSqBracket, "]" });
    }

    AST* Parser::parse_lCurlyBracket()
    {
        if (!expect_char('{'))
        {
            return nullptr;
        }

        return new AST({ TokenType::lCurlyBracket, "{" });
    }

    AST* Parser::parse_rCurlyBracket()
    {
        if (!expect_char('}'))
        {
            return nullptr;
        }

        return new AST({ TokenType::rCurlyBracket, "}" });
    }

    AST* Parser::parse_backslash()
    {
        if (!expect_char('\\'))
        {
            return nullptr;
        }

        return new AST({ TokenType::backslash, "\\" });
    }

    /*!
     * Returns `true` when the EOF is reached and `charhistory` is consumed,
     * otherwise returns `false`.
     */
    bool Parser::advance() noexcept
    {
        if (!this->charhistory.empty())
        {
            this->ch = this->charhistory.front();
            this->charhistory.pop_front();

            return this->charhistory.empty() && this->charstream.eof();
        }

        if (this->charstream >> std::noskipws >> this->ch)
        {
            return false;
        }

        return true;
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

        if (this->charstream.eof() && isnewline(this->ch))
        {
            this->currentindent.clear();
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
        std::vector<char> historicstack;

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
        std::vector<char> historicstack;

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

                if (!historicstack.empty())
                {
                    this->ch = historicstack.back();
                }

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
        std::vector<char> historicstack;

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
            case TokenType::caseBranch:
                first_item = parse_caseBranch();
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
                case TokenType::caseBranch:
                    item = parse_caseBranch();
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
