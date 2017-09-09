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
    {
        '\'', '"', 't', 'v', 'n', 'r', 'b', '0'
    };

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

    Parser::Parser(const std::string& filename) : charstream(filename.c_str())
    {
        this->currentindent = "";

        this->ch = ' '; // Dummy value

        if (!this->charstream.is_open())
        {
            throw std::runtime_error("Failed to open " + filename);
        }
    }

    std::string Parser::str_repr(const AST& ast) noexcept
    {
        if (!ast.val().lexeme.empty())
        {
            return ast.val().lexeme;
        }

        std::string ret = "";
        const size_t child_count = ast.child_count();

        for (size_t i = 0; i < child_count; ++i)
        {
            const AST& child_ast = ast.get_child(i);
            ret += str_repr(child_ast);

            const TokenType child_type = child_ast.val().type;

            if (
                child_type != TokenType::strChr      &&
                child_type != TokenType::chrChr      &&
                child_type != TokenType::doubleQuote &&
                child_type != TokenType::singleQuote
            ) {
                ret.push_back(' ');
            }
        }

        return ret;
    }

    void Parser::log_depthfirst(const AST& ast, size_t cur_depth)
    {
        for (size_t i = 0; i < cur_depth; ++i)
        {
            std::cout << "  ";
        }

        const std::string& lex = ast.val().lexeme;

        if (lex.empty())
        {
            std::cout << u8" └─ "
                      << token_type_names.at(ast.val().type)
                      << '\n';
        }
        else
        {
            std::cout << u8" └─ "
                      << token_type_names.at(ast.val().type)
                      << " \""
                      << lex
                      << "\"\n";
        }

        const size_t child_count = ast.child_count();

        for (size_t i = 0; i < child_count; ++i)
        {
            log_depthfirst(ast.get_child(i), cur_depth + 1);
        }
    }

    std::optional<AST> Parser::parse()
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

        AST mainAst({ TokenType::root, "" });
        std::optional<AST> prog = parse_prog();

        if (!prog)
        {
            return {};
        }

        mainAst.add_child(std::move(*prog));

        return mainAst;
    }

    std::optional<AST> Parser::parse_prog()
    {
        AST prog({ TokenType::prog, "" });

        std::optional<AST> module_decl = parse_modDecl();

        if (module_decl)
        {
            prog.add_child(std::move(*module_decl));
        }

        while (!this->charstream.eof() || !this->charhistory.empty())
        {
            std::optional<AST> import = parse_import();

            if (!import)
            {
                break;
            }

            prog.add_child(std::move(*import));
        }

        while (!this->charstream.eof() || !this->charhistory.empty())
        {
            std::optional<AST> line = parse_line(true);

            if (!line)
            {
                break;
            }

            prog.add_child(std::move(*line));
        }

        return prog;
    }

    std::optional<AST> Parser::parse_modDecl()
    {
        std::optional<AST> module_keyword = parse_moduleKeyword();

        if (!module_keyword)
        {
            return {};
        }

        AST mod_decl({ TokenType::modDecl, "" });
        mod_decl.add_child(std::move(*module_keyword));

        std::optional<AST> mod_name = parse_ident();

        if (!mod_name)
        {
            throw std::runtime_error(
                "expected name of module to be plain identifier"
            );
        }

        mod_decl.add_child(std::move(*mod_name));

        consume_blanks();
        std::optional<AST> exposing_keyword = parse_exposingKeyword();
        std::optional<AST> hiding_keyword = {};

        if (!exposing_keyword)
        {
            hiding_keyword = parse_hidingKeyword();
        }

        if (exposing_keyword || hiding_keyword)
        {
            if (exposing_keyword)
            {
                mod_decl.add_child(std::move(*exposing_keyword));
            }
            else
            {
                mod_decl.add_child(std::move(*hiding_keyword));
            }

            std::optional<AST> first_ident = parse_ident();

            if (!first_ident)
            {
                throw std::runtime_error(
                    "expected at least one item in module export/hide list"
                );
            }

            mod_decl.add_child(std::move(*first_ident));

            consume_blanks();

            while (std::optional<AST> comma = parse_comma())
            {
                std::optional<AST> ident = parse_ident();

                if (!ident)
                {
                    break;
                }

                mod_decl.add_child(std::move(*comma));
                mod_decl.add_child(std::move(*ident));

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

    std::optional<AST> Parser::parse_import()
    {
        std::optional<AST> import_keyword = parse_importKeyword();

        if (!import_keyword)
        {
            return {};
        }

        AST import({ TokenType::import, "" });
        import.add_child(std::move(*import_keyword));

        std::optional<AST> mod_name = parse_ident();

        if (!mod_name)
        {
            throw std::runtime_error(
                "expected module name after import keyword"
            );
        }

        import.add_child(std::move(*mod_name));

        consume_blanks();
        std::optional<AST> as_keyword = parse_asKeyword();

        if (as_keyword)
        {
            import.add_child(std::move(*as_keyword));

            std::optional<AST> qual_name = parse_ident();

            if (!qual_name)
            {
                throw std::runtime_error(
                    "expected namespace alias after as keyword"
                );
            }

            import.add_child(std::move(*qual_name));
        }
        else
        {
            std::optional<AST> hiding_keyword = parse_hidingKeyword();

            if (hiding_keyword)
            {
                import.add_child(std::move(*hiding_keyword));
            }

            consume_blanks();

            std::optional<AST> l_paren = parse_lParen();

            if (!l_paren)
            {
                throw std::runtime_error(
                    "expected left paren to start import list"
                );
            }

            import.add_child(std::move(*l_paren));

            std::optional<AST> first_import_item = parse_ident();

            if (!first_import_item)
            {
                throw std::runtime_error(
                    "expected at least one import item in import list"
                );
            }

            import.add_child(std::move(*first_import_item));

            consume_blanks();

            while (std::optional<AST> comma = parse_comma())
            {
                std::optional<AST> import_item = parse_ident();

                if (!import_item)
                {
                    break;
                }

                import.add_child(std::move(*comma));
                import.add_child(std::move(*import_item));

                consume_blanks();
            }

            consume_blanks();

            std::optional<AST> r_paren = parse_rParen();

            if (!r_paren)
            {
                throw std::runtime_error(
                    "expected right paren to terminate import list"
                );
            }

            import.add_child(std::move(*r_paren));
        }

        if (!expect_newline())
        {
            throw std::runtime_error(
                "expected newline after import statement"
            );
        }

        return import;
    }

    std::optional<AST> Parser::parse_line(bool consume_newline)
    {
        consume_blanks();

        AST line({ TokenType::line, "" });

        std::optional<AST> expr = parse_expr();

        if (expr)
        {
            line.add_child(std::move(*expr));
        }

        consume_lineComment(consume_newline);

        if (consume_newline)
        {
            expect_newline();
        }

        return line;
    }

    bool Parser::consume_lineComment(bool consume_newline)
    {
        consume_blanks();

        if (!consume_lineCommentOp())
        {
            return false;
        }

        if (isnewline(this->ch))
        {
            if (consume_newline)
            {
                expect_newline();
            }

            return true;
        }

        while (!this->charhistory.empty())
        {
            this->ch = this->charhistory.front();
            this->charhistory.pop_front();

            if (isnewline(this->ch))
            {
                if (consume_newline)
                {
                    expect_newline();
                }

                return true;
            }
        }

        while (this->charstream >> std::noskipws >> this->ch)
        {
            if (isnewline(this->ch))
            {
                if (consume_newline)
                {
                    expect_newline();
                }

                return true;
            }
        }

        return true;
    }

    std::optional<AST> Parser::parse_expr()
    {
        consume_blanks();

        std::optional<AST> first_subexpr = parse_subexpr();

        if (!first_subexpr)
        {
            return {};
        }

        AST expr({ TokenType::expr, "" });
        expr.add_child(std::move(*first_subexpr));

        while (std::optional<AST> subexpr = parse_subexpr())
        {
            expr.add_child(std::move(*subexpr));
        }

        return expr;
    }

    std::optional<AST> Parser::parse_subexpr()
    {
        consume_blanks();

        AST subexpr({ TokenType::subexpr, "" });

        if (std::optional<AST> var = parse_var())
        {
            subexpr.add_child(std::move(*var));
        }
        else if (std::optional<AST> assign = parse_assign())
        {
            subexpr.add_child(std::move(*assign));
        }
        else if (std::optional<AST> fnDecl = parse_fnDecl())
        {
            subexpr.add_child(std::move(*fnDecl));
        }
        else if (std::optional<AST> parened = parse_parened())
        {
            subexpr.add_child(std::move(*parened));
        }
        else if (std::optional<AST> return_ = parse_return())
        {
            subexpr.add_child(std::move(*return_));
        }
        else if (std::optional<AST> case_ = parse_case())
        {
            subexpr.add_child(std::move(*case_));
        }
        else if (std::optional<AST> ifElse = parse_ifElse())
        {
            subexpr.add_child(std::move(*ifElse));
        }
        else if (std::optional<AST> try_ = parse_try())
        {
            subexpr.add_child(std::move(*try_));
        }
        else if (std::optional<AST> while_ = parse_while())
        {
            subexpr.add_child(std::move(*while_));
        }
        else if (std::optional<AST> for_ = parse_for())
        {
            subexpr.add_child(std::move(*for_));
        }
        else if (std::optional<AST> lambda = parse_lambda())
        {
            subexpr.add_child(std::move(*lambda));
        }
        else if (std::optional<AST> tupleLit = parse_tupleLit())
        {
            subexpr.add_child(std::move(*tupleLit));
        }
        else if (std::optional<AST> listLit = parse_listLit())
        {
            subexpr.add_child(std::move(*listLit));
        }
        else if (std::optional<AST> listComp = parse_listComp())
        {
            subexpr.add_child(std::move(*listComp));
        }
        else if (std::optional<AST> dictLit = parse_dictLit())
        {
            subexpr.add_child(std::move(*dictLit));
        }
        else if (std::optional<AST> dictComp = parse_dictComp())
        {
            subexpr.add_child(std::move(*dictComp));
        }
        else if (std::optional<AST> setLit = parse_setLit())
        {
            subexpr.add_child(std::move(*setLit));
        }
        else if (std::optional<AST> setComp = parse_setComp())
        {
            subexpr.add_child(std::move(*setComp));
        }
        else if (std::optional<AST> qual_ident = parse_qualIdent())
        {
            subexpr.add_child(std::move(*qual_ident));
        }
        else if (std::optional<AST> infixed = parse_infixed())
        {
            subexpr.add_child(std::move(*infixed));
        }
        else if (std::optional<AST> numLit = parse_numLit())
        {
            subexpr.add_child(std::move(*numLit));
        }
        else if (std::optional<AST> chrLit = parse_chrLit())
        {
            subexpr.add_child(std::move(*chrLit));
        }
        else if (std::optional<AST> strLit = parse_strLit())
        {
            subexpr.add_child(std::move(*strLit));
        }
        else if (std::optional<AST> op = parse_op())
        {
            subexpr.add_child(std::move(*op));
        }
        else
        {
            return {};
        }

        return subexpr;
    }

    std::optional<AST> Parser::parse_var()
    {
        std::optional<AST> var_keyword = parse_varKeyword();

        if (!var_keyword)
        {
            return {};
        }

        std::optional<AST> pattern = parse_pattern();

        if (!pattern)
        {
            throw std::runtime_error(
                "left-hand side of var assignment must be a pattern"
            );
        }

        consume_blanks();

        AST var({ TokenType::var, "" });
        var.add_child(std::move(*var_keyword));
        var.add_child(std::move(*pattern));

        if (std::optional<AST> colon = parse_colon())
        {
            std::optional<AST> type = parse_qualIdent();

            if (!type)
            {
                throw std::runtime_error(
                    "type of var binding must be a valid identifier"
                );
            }

            var.add_child(std::move(*colon));
            var.add_child(std::move(*type));
        }

        std::optional<AST> equals = parse_equals();

        if (!equals)
        {
            throw std::runtime_error("var assignment must use =");
        }

        std::optional<AST> expr = parse_expr();

        if (!expr)
        {
            throw std::runtime_error(
                "right-hand side of var assignment must be a valid expression"
            );
        }

        var.add_child(std::move(*equals));
        var.add_child(std::move(*expr));

        return var;
    }

    std::optional<AST> Parser::parse_assign()
    {
        std::optional<AST> pattern = parse_pattern();

        if (!pattern)
        {
            return {};
        }

        consume_blanks();

        AST assign({ TokenType::assign, "" });
        assign.add_child(AST(*pattern));

        if (std::optional<AST> colon = parse_colon())
        {
            std::optional<AST> type = parse_typeIdent();

            if (!type)
            {
                throw std::runtime_error(
                    "type of binding must be a valid identifier"
                );
            }

            assign.add_child(std::move(*colon));
            assign.add_child(std::move(*type));
        }

        consume_blanks();

        std::optional<AST> equals = parse_equals();

        if (!equals)
        {
            this->charhistory.push_front(this->ch);
            this->charhistory.push_front(' ');

            std::string consumed_pattern = str_repr(*pattern);

            while (consumed_pattern.length() > 1)
            {
                this->charhistory.push_front(consumed_pattern.back());
                consumed_pattern.pop_back();
            }

            this->ch = consumed_pattern.back();

            return {};
        }

        std::optional<AST> expr = parse_expr();

        if (!expr)
        {
            throw std::runtime_error(
                "right-hand side of assignment must be a valid expression"
            );
        }

        assign.add_child(std::move(*equals));
        assign.add_child(std::move(*expr));

        return assign;
    }

    std::optional<AST> Parser::parse_fnDecl()
    {
        consume_blanks();

        std::optional<AST> fn_keyword = parse_fnKeyword();

        if (!fn_keyword)
        {
            return {};
        }

        consume_blanks();

        std::optional<AST> fn_name = parse_ident();

        if (!fn_name)
        {
            throw std::runtime_error("expected function name");
        }

        consume_blanks();

        AST fnDecl({ TokenType::fnDecl, "" });
        fnDecl.add_child(std::move(*fn_keyword));
        fnDecl.add_child(std::move(*fn_name));

        while (std::optional<AST> fn_param = parse_param())
        {
            fnDecl.add_child(std::move(*fn_param));
        }

        consume_blanks();

        std::optional<AST> r_arrow = parse_rArrow();

        if (r_arrow)
        {
            fnDecl.add_child(std::move(*r_arrow));

            std::optional<AST> ret_type = parse_qualIdent();

            if (!ret_type)
            {
                throw std::runtime_error("expected type after arrow");
            }

            fnDecl.add_child(std::move(*ret_type));
        }

        get_block(fnDecl, TokenType::line);

        return fnDecl;
    }

    std::optional<AST> Parser::parse_parened()
    {
        consume_blanks();

        std::optional<AST> l_paren = parse_lParen();

        if (!l_paren)
        {
            return {};
        }

        std::optional<AST> expr = parse_expr();

        if (!expr)
        {
            throw std::runtime_error("expected expression within parens");
        }

        std::optional<AST> r_paren = parse_rParen();

        if (!r_paren)
        {
            throw std::runtime_error("expected closing paren");
        }

        AST parened({ TokenType::parened, "" });
        parened.add_child(std::move(*l_paren));
        parened.add_child(std::move(*expr));
        parened.add_child(std::move(*r_paren));

        return parened;
    }

    std::optional<AST> Parser::parse_return()
    {
        consume_blanks();

        std::optional<AST> return_keyword = parse_returnKeyword();

        if (!return_keyword)
        {
            return {};
        }

        std::optional<AST> expr = parse_expr();

        if (!expr)
        {
            throw std::runtime_error("expected expression to return");
        }

        AST return_({ TokenType::return_, "" });
        return_.add_child(std::move(*return_keyword));
        return_.add_child(std::move(*expr));

        return return_;
    }

    std::optional<AST> Parser::parse_case()
    {
        consume_blanks();

        std::optional<AST> case_keyword = parse_caseKeyword();

        if (!case_keyword)
        {
            return {};
        }

        consume_blanks();

        std::optional<AST> subject_expr = parse_expr();

        if (!subject_expr)
        {
            throw std::runtime_error("expected subject expression for case");
        }

        AST case_({ TokenType::case_, "" });
        case_.add_child(std::move(*case_keyword));
        case_.add_child(std::move(*subject_expr));

        get_block(case_, TokenType::caseBranch);

        return case_;
    }

    std::optional<AST> Parser::parse_caseBranch()
    {
        consume_blanks();

        std::optional<AST> pattern = parse_pattern();

        if (!pattern)
        {
            return {};
        }

        std::optional<AST> fat_r_arrow = parse_fatRArrow();

        if (!fat_r_arrow)
        {
            throw std::runtime_error("expected => while parsing case branch");
        }

        std::optional<AST> line = parse_line(false);

        if (!line)
        {
            throw std::runtime_error("expected expression(s) after =>");
        }

        AST caseBranch({ TokenType::caseBranch, "" });
        caseBranch.add_child(std::move(*pattern));
        caseBranch.add_child(std::move(*fat_r_arrow));
        caseBranch.add_child(std::move(*line));

        return caseBranch;
    }

    std::optional<AST> Parser::parse_ifElse()
    {
        consume_blanks();

        std::optional<AST> if_keyword = parse_ifKeyword();

        if (!if_keyword)
        {
            return {};
        }

        consume_blanks();

        std::optional<AST> if_condition = parse_expr();

        if (!if_condition)
        {
            throw std::runtime_error("expected expression as if condition");
        }

        AST ifElse({ TokenType::ifElse, "" });
        ifElse.add_child(std::move(*if_keyword));
        ifElse.add_child(std::move(*if_condition));

        const std::string start_indent = get_block(ifElse, TokenType::line);

        if (this->currentindent != start_indent)
        {
            return ifElse;
        }

        std::optional<AST> else_keyword = parse_elseKeyword();

        if (!else_keyword)
        {
            return ifElse;
        }

        ifElse.add_child(std::move(*else_keyword));

        if (std::optional<AST> if_else = parse_ifElse())
        {
            ifElse.add_child(std::move(*if_else));

            return ifElse;
        }

        get_block(ifElse, TokenType::line);

        return ifElse;
    }

    std::optional<AST> Parser::parse_try()
    {
        consume_blanks();

        std::optional<AST> try_keyword = parse_tryKeyword();

        if (!try_keyword)
        {
            return {};
        }

        consume_blanks();

        AST try_({ TokenType::try_, "" });
        try_.add_child(std::move(*try_keyword));

        const std::string start_indent = get_block(try_, TokenType::line);

        if (this->currentindent != start_indent)
        {
            throw std::runtime_error(
                "try must have corresponsing catch on same indent level"
            );
        }

        std::optional<AST> catch_keyword = parse_catchKeyword();

        if (!catch_keyword)
        {
            throw std::runtime_error("try must have corresponding catch");
        }

        std::optional<AST> exception_ident = parse_ident();

        if (!exception_ident)
        {
            throw std::runtime_error("catch must name the caught exception");
        }

        try_.add_child(std::move(*catch_keyword));
        try_.add_child(std::move(*exception_ident));

        get_block(try_, TokenType::line);

        return try_;
    }

    std::optional<AST> Parser::parse_while()
    {
        consume_blanks();

        std::optional<AST> while_keyword = parse_whileKeyword();

        if (!while_keyword)
        {
            return {};
        }

        consume_blanks();

        std::optional<AST> while_condition = parse_expr();

        if (!while_condition)
        {
            throw std::runtime_error("expected expression as while condition");
        }

        AST while_({ TokenType::while_, "" });
        while_.add_child(std::move(*while_keyword));
        while_.add_child(std::move(*while_condition));

        get_block(while_, TokenType::line);

        return while_;
    }

    std::optional<AST> Parser::parse_for()
    {
        consume_blanks();

        std::optional<AST> for_keyword = parse_forKeyword();

        if (!for_keyword)
        {
            return {};
        }

        consume_blanks();

        std::optional<AST> for_pattern = parse_pattern();

        if (!for_pattern)
        {
            throw std::runtime_error(
                "expected pattern as first part of for header"
            );
        }

        consume_blanks();

        std::optional<AST> in_keyword = parse_inKeyword();

        if (!in_keyword)
        {
            throw std::runtime_error("missing in keyword of for loop");
        }

        std::optional<AST> iterated = parse_expr();

        if (!iterated)
        {
            throw std::runtime_error("for must iterate over an expression");
        }

        AST for_({ TokenType::for_, "" });
        for_.add_child(std::move(*for_keyword));
        for_.add_child(std::move(*for_pattern));
        for_.add_child(std::move(*in_keyword));
        for_.add_child(std::move(*iterated));

        get_block(for_, TokenType::line);

        return for_;
    }

    std::optional<AST> Parser::parse_lambda()
    {
        consume_blanks();

        std::optional<AST> backslash = parse_backslash();

        if (!backslash)
        {
            return {};
        }

        std::optional<AST> first_param = parse_param();

        if (!first_param)
        {
            throw std::runtime_error("lambda expression requires 1+ args");
        }

        AST lambda({ TokenType::lambda, "" });
        lambda.add_child(std::move(*backslash));
        lambda.add_child(std::move(*first_param));

        consume_blanks();

        while (std::optional<AST> comma = parse_comma())
        {
            std::optional<AST> param = parse_param();

            if (!param)
            {
                break;
            }

            lambda.add_child(std::move(*comma));
            lambda.add_child(std::move(*param));

            consume_blanks();
        }

        std::optional<AST> arrow = parse_rArrow();

        if (!arrow)
        {
            throw std::runtime_error("lambda expression requires ->");
        }

        std::optional<AST> expr = parse_expr();

        if (!expr)
        {
            throw std::runtime_error("lambda body must be expression");
        }

        lambda.add_child(std::move(*arrow));
        lambda.add_child(std::move(*expr));

        return lambda;
    }

    std::optional<AST> Parser::parse_tupleLit()
    {
        consume_blanks();

        std::optional<AST> l_paren = parse_lParen();

        if (!l_paren)
        {
            return {};
        }

        std::optional<AST> first_expr = parse_expr();

        AST tupleLit({ TokenType::tupleLit, "" });
        tupleLit.add_child(std::move(*l_paren));

        consume_blanks();

        if (first_expr)
        {
            std::optional<AST> first_comma = parse_comma();

            if (!first_comma)
            {
                throw std::runtime_error(
                    "expected comma after first tuple element"
                );
            }

            std::optional<AST> second_expr = parse_expr();

            if (!second_expr)
            {
                throw std::runtime_error(
                    "expected 0 or at least 2 elements in tuple"
                );
            }

            tupleLit.add_child(std::move(*first_expr));
            tupleLit.add_child(std::move(*first_comma));
            tupleLit.add_child(std::move(*second_expr));

            consume_blanks();

            while (std::optional<AST> comma = parse_comma())
            {
                std::optional<AST> expr = parse_expr();

                if (!expr)
                {
                    break;
                }

                tupleLit.add_child(std::move(*comma));
                tupleLit.add_child(std::move(*expr));

                consume_blanks();
            }
        }

        std::optional<AST> r_paren = parse_rParen();

        if (!r_paren)
        {
            throw std::runtime_error(
                "expected right paren to terminate tuple"
            );
        }

        tupleLit.add_child(std::move(*r_paren));

        return tupleLit;
    }

    std::optional<AST> Parser::parse_listLit()
    {
        consume_blanks();

        std::optional<AST> l_sq_bracket = parse_lSqBracket();

        if (!l_sq_bracket)
        {
            return {};
        }

        std::optional<AST> first_expr = parse_expr();

        AST listLit({ TokenType::listLit, "" });
        listLit.add_child(std::move(*l_sq_bracket));

        if (first_expr)
        {
            listLit.add_child(std::move(*first_expr));

            consume_blanks();

            while (std::optional<AST> comma = parse_comma())
            {
                std::optional<AST> expr = parse_expr();

                if (!expr)
                {
                    break;
                }

                listLit.add_child(std::move(*comma));
                listLit.add_child(std::move(*expr));

                consume_blanks();
            }
        }

        std::optional<AST> r_sq_bracket = parse_rSqBracket();

        if (!r_sq_bracket)
        {
            throw std::runtime_error(
                "left square bracket in list literal requires ]"
            );
        }

        listLit.add_child(std::move(*r_sq_bracket));

        return listLit;
    }

    std::optional<AST> Parser::parse_listComp()
    {
        consume_blanks();

        std::optional<AST> l_sq_bracket = parse_lSqBracket();

        if (!l_sq_bracket)
        {
            return {};
        }

        std::optional<AST> expr = parse_expr();

        if (!expr)
        {
            throw std::runtime_error(
                "expected expression on left-hand side of list comprehension"
            );
        }

        std::optional<AST> bar = parse_bar();

        if (!bar)
        {
            throw std::runtime_error("expected | for list comprehension");
        }

        AST listComp({ TokenType::listComp, "" });
        listComp.add_child(std::move(*l_sq_bracket));
        listComp.add_child(std::move(*expr));
        listComp.add_child(std::move(*bar));

        std::optional<AST> first_generator = parse_generator();
        std::optional<AST> first_condition = {};

        if (!first_generator)
        {
            first_condition = parse_expr();
        }

        if (first_generator || first_condition)
        {
            if (first_generator)
            {
                listComp.add_child(std::move(*first_generator));
            }
            else
            {
                listComp.add_child(std::move(*first_condition));
            }

            consume_blanks();

            while (std::optional<AST> comma = parse_comma())
            {
                if (std::optional<AST> generator = parse_generator())
                {
                    listComp.add_child(std::move(*generator));
                }
                else if (std::optional<AST> condition = parse_expr())
                {
                    listComp.add_child(std::move(*condition));
                }
                else
                {
                    break;
                }

                consume_blanks();
            }
        }

        std::optional<AST> r_sq_bracket = parse_rSqBracket();

        if (!r_sq_bracket)
        {
            throw std::runtime_error(
                "expected ] to terminate list comprehension"
            );
        }

        listComp.add_child(std::move(*r_sq_bracket));

        return listComp;
    }

    std::optional<AST> Parser::parse_dictLit()
    {
        consume_blanks();

        std::optional<AST> l_curly_bracket = parse_lCurlyBracket();

        if (!l_curly_bracket)
        {
            return {};
        }

        std::optional<AST> first_entry = parse_dictEntry();

        AST dictLit({ TokenType::dictLit, "" });
        dictLit.add_child(std::move(*l_curly_bracket));

        if (first_entry)
        {
            consume_blanks();

            while (std::optional<AST> comma = parse_comma())
            {
                std::optional<AST> entry = parse_dictEntry();

                if (!entry)
                {
                    break;
                }

                dictLit.add_child(std::move(*comma));
                dictLit.add_child(std::move(*entry));

                consume_blanks();
            }
        }

        std::optional<AST> r_curly_bracket = parse_rCurlyBracket();

        if (!r_curly_bracket)
        {
            throw std::runtime_error(
                "left curly bracket in dict literal requires }"
            );
        }

        dictLit.add_child(std::move(*r_curly_bracket));

        return dictLit;
    }

    std::optional<AST> Parser::parse_dictComp()
    {
        consume_blanks();

        std::optional<AST> l_curly_bracket = parse_lCurlyBracket();

        if (!l_curly_bracket)
        {
            return {};
        }

        std::optional<AST> entry = parse_dictEntry();

        if (!entry)
        {
            throw std::runtime_error(
                "expected entry on left-hand side of dict comprehension"
            );
        }

        std::optional<AST> bar = parse_bar();

        if (!bar)
        {
            throw std::runtime_error("expected | for dict comprehension");
        }

        AST dictComp({ TokenType::dictComp, "" });
        dictComp.add_child(std::move(*l_curly_bracket));
        dictComp.add_child(std::move(*entry));
        dictComp.add_child(std::move(*bar));

        std::optional<AST> first_generator = parse_generator();
        std::optional<AST> first_condition = {};

        if (!first_generator)
        {
            first_condition = parse_expr();
        }

        if (first_generator || first_condition)
        {
            if (first_generator)
            {
                dictComp.add_child(std::move(*first_generator));
            }
            else
            {
                dictComp.add_child(std::move(*first_condition));
            }

            consume_blanks();

            while (std::optional<AST> comma = parse_comma())
            {
                if (std::optional<AST> generator = parse_generator())
                {
                    dictComp.add_child(std::move(*generator));
                }
                else if (std::optional<AST> condition = parse_expr())
                {
                    dictComp.add_child(std::move(*condition));
                }
                else
                {
                    break;
                }

                consume_blanks();
            }
        }

        std::optional<AST> r_curly_bracket = parse_rCurlyBracket();

        if (!r_curly_bracket)
        {
            throw std::runtime_error(
                "expected } to terminate dict comprehension"
            );
        }

        dictComp.add_child(std::move(*r_curly_bracket));

        return dictComp;
    }

    std::optional<AST> Parser::parse_setLit()
    {
        consume_blanks();

        std::optional<AST> l_curly_bracket = parse_lCurlyBracket();

        if (!l_curly_bracket)
        {
            return {};
        }

        std::optional<AST> first_expr = parse_expr();

        AST setLit({ TokenType::setLit, "" });
        setLit.add_child(std::move(*l_curly_bracket));

        if (first_expr)
        {
            consume_blanks();

            while (std::optional<AST> comma = parse_comma())
            {
                std::optional<AST> expr = parse_expr();

                if (!expr)
                {
                    break;
                }

                setLit.add_child(std::move(*comma));
                setLit.add_child(std::move(*expr));

                consume_blanks();
            }
        }

        std::optional<AST> r_curly_bracket = parse_rCurlyBracket();

        if (!r_curly_bracket)
        {
            throw std::runtime_error(
                "left curly bracket in set literal requires }"
            );
        }

        setLit.add_child(std::move(*r_curly_bracket));

        return setLit;
    }

    std::optional<AST> Parser::parse_setComp()
    {
        consume_blanks();

        std::optional<AST> l_curly_bracket = parse_lCurlyBracket();

        if (!l_curly_bracket)
        {
            return {};
        }

        std::optional<AST> expr = parse_expr();

        if (!expr)
        {
            throw std::runtime_error(
                "expected expression on left-hand side of set comprehension"
            );
        }

        std::optional<AST> bar = parse_bar();

        if (!bar)
        {
            throw std::runtime_error("expected | for set comprehension");
        }

        AST setComp({ TokenType::setComp, "" });
        setComp.add_child(std::move(*l_curly_bracket));
        setComp.add_child(std::move(*expr));
        setComp.add_child(std::move(*bar));

        std::optional<AST> first_generator = parse_generator();
        std::optional<AST> first_condition = {};

        if (!first_generator)
        {
            first_condition = parse_expr();
        }

        if (first_generator || first_condition)
        {
            if (first_generator)
            {
                setComp.add_child(std::move(*first_generator));
            }
            else
            {
                setComp.add_child(std::move(*first_condition));
            }

            consume_blanks();

            while (std::optional<AST> comma = parse_comma())
            {
                if (std::optional<AST> generator = parse_generator())
                {
                    setComp.add_child(std::move(*generator));
                }
                else if (std::optional<AST> condition = parse_expr())
                {
                    setComp.add_child(std::move(*condition));
                }
                else
                {
                    break;
                }

                consume_blanks();
            }
        }

        std::optional<AST> r_curly_bracket = parse_rCurlyBracket();

        if (!r_curly_bracket)
        {
            throw std::runtime_error(
                "expected } to terminate set comprehension"
            );
        }

        setComp.add_child(std::move(*r_curly_bracket));

        return setComp;
    }

    std::optional<AST> Parser::parse_qualIdent()
    {
        consume_blanks();

        if (std::optional<AST> member_ident = parse_memberIdent())
        {
            AST qual_ident({ TokenType::qualIdent, "" });
            qual_ident.add_child(std::move(*member_ident));

            return qual_ident;
        }

        if (std::optional<AST> scoped_ident = parse_scopedIdent())
        {
            AST qual_ident({ TokenType::qualIdent, "" });
            qual_ident.add_child(std::move(*scoped_ident));

            return qual_ident;
        }

        if (std::optional<AST> ident = parse_ident())
        {
            AST qual_ident({ TokenType::qualIdent, "" });
            qual_ident.add_child(std::move(*ident));

            return qual_ident;
        }

        return {};
    }

    std::optional<AST> Parser::parse_namespacedIdent()
    {
        consume_blanks();

        if (std::optional<AST> scoped_ident = parse_scopedIdent())
        {
            AST namespaced_ident({ TokenType::namespacedIdent, "" });
            namespaced_ident.add_child(std::move(*scoped_ident));

            return namespaced_ident;
        }

        if (std::optional<AST> ident = parse_ident())
        {
            AST namespaced_ident({ TokenType::namespacedIdent, "" });
            namespaced_ident.add_child(std::move(*ident));

            return namespaced_ident;
        }

        return {};
    }

    std::optional<AST> Parser::parse_ident()
    {
        consume_blanks();

        if (!isalpha(this->ch) && this->ch != '_')
        {
            return {};
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

                return {};
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

        return AST({ TokenType::ident, id });
    }

    std::optional<AST> Parser::parse_memberIdent()
    {
        std::optional<AST> first_ident = parse_ident();

        if (!first_ident)
        {
            return {};
        }

        std::optional<AST> dot = parse_dot();

        if (!dot)
        {
            const std::string& first_ident_lex = first_ident->val().lexeme;

            this->charhistory.push_front(this->ch);

            for (auto i = first_ident_lex.length() - 1; i > 0; --i)
            {
                this->charhistory.push_front(first_ident_lex[i]);
            }

            this->ch = first_ident_lex[0];

            return {};
        }

        std::optional<AST> second_ident = parse_ident();

        if (!second_ident)
        {
            throw std::runtime_error("expected identifier after dot operator");
        }

        AST memberIdent({ TokenType::memberIdent, "" });
        memberIdent.add_child(std::move(*first_ident));
        memberIdent.add_child(std::move(*dot));
        memberIdent.add_child(std::move(*second_ident));

        return memberIdent;
    }

    std::optional<AST> Parser::parse_scopedIdent()
    {
        std::optional<AST> first_ident = parse_ident();

        if (!first_ident)
        {
            return {};
        }

        std::optional<AST> double_colon = parse_doubleColon();

        if (!double_colon)
        {
            const std::string& first_ident_lex = first_ident->val().lexeme;

            this->charhistory.push_front(this->ch);

            for (auto i = first_ident_lex.length() - 1; i > 0; --i)
            {
                this->charhistory.push_front(first_ident_lex[i]);
            }

            this->ch = first_ident_lex[0];

            return {};
        }

        std::optional<AST> second_ident = parse_ident();

        if (!second_ident)
        {
            throw std::runtime_error("expected identifier after dot operator");
        }

        AST scopedIdent({ TokenType::scopedIdent, "" });
        scopedIdent.add_child(std::move(*first_ident));
        scopedIdent.add_child(std::move(*double_colon));
        scopedIdent.add_child(std::move(*second_ident));

        return scopedIdent;
    }

    std::optional<AST> Parser::parse_typeIdent()
    {
        consume_blanks();

        if (std::optional<AST> namespaced_ident = parse_namespacedIdent())
        {
            AST type_ident({ TokenType::typeIdent, "" });
            type_ident.add_child(std::move(*namespaced_ident));

            return type_ident;
        }

        if (std::optional<AST> l_paren = parse_lParen())
        {
            std::optional<AST> first_ident = parse_typeIdent();

            AST typeIdent({ TokenType::typeIdent, "" });
            typeIdent.add_child(std::move(*l_paren));

            consume_blanks();

            if (first_ident)
            {
                std::optional<AST> first_comma = parse_comma();

                if (!first_comma)
                {
                    throw std::runtime_error(
                        "expected comma after first type tuple element"
                    );
                }

                std::optional<AST> second_ident = parse_typeIdent();

                if (!second_ident)
                {
                    throw std::runtime_error(
                        "expected 0 or at least 2 elements in type tuple"
                    );
                }

                typeIdent.add_child(std::move(*first_ident));
                typeIdent.add_child(std::move(*first_comma));
                typeIdent.add_child(std::move(*second_ident));

                consume_blanks();

                while (std::optional<AST> comma = parse_comma())
                {
                    std::optional<AST> ident = parse_typeIdent();

                    if (!ident)
                    {
                        break;
                    }

                    typeIdent.add_child(std::move(*comma));
                    typeIdent.add_child(std::move(*ident));

                    consume_blanks();
                }
            }

            std::optional<AST> r_paren = parse_rParen();

            if (!r_paren)
            {
                throw std::runtime_error(
                    "expected right paren to terminate type tuple"
                );
            }

            typeIdent.add_child(std::move(*r_paren));

            return typeIdent;
        }

        if (std::optional<AST> l_sq_bracket = parse_lSqBracket())
        {
            std::optional<AST> ident = parse_typeIdent();

            if (!ident)
            {
                throw std::runtime_error("expected type identifier after [");
            }

            std::optional<AST> r_sq_bracket = parse_rSqBracket();

            if (!r_sq_bracket)
            {
                throw std::runtime_error("expected closing ] of list type");
            }

            AST typeIdent({ TokenType::typeIdent, "" });
            typeIdent.add_child(std::move(*l_sq_bracket));
            typeIdent.add_child(std::move(*ident));
            typeIdent.add_child(std::move(*r_sq_bracket));

            return typeIdent;
        }

        if (std::optional<AST> l_curly_bracket = parse_lCurlyBracket())
        {
            std::optional<AST> ident = parse_typeIdent();

            if (!ident)
            {
                throw std::runtime_error("expected type identifier after {");
            }

            consume_blanks();

            AST typeIdent({ TokenType::typeIdent, "" });
            typeIdent.add_child(std::move(*l_curly_bracket));
            typeIdent.add_child(std::move(*ident));

            std::optional<AST> comma = parse_comma();

            if (comma)
            {
                std::optional<AST> second_ident = parse_typeIdent();

                if (!second_ident)
                {
                    throw std::runtime_error(
                        "expected type identifier after ,"
                    );
                }

                typeIdent.add_child(std::move(*comma));
                typeIdent.add_child(std::move(*second_ident));
            }

            std::optional<AST> r_curly_bracket = parse_rCurlyBracket();

            if (!r_curly_bracket)
            {
                throw std::runtime_error(
                    "expected closing } of dict/set type"
                );
            }

            typeIdent.add_child(std::move(*r_curly_bracket));

            return typeIdent;
        }

        return {};
    }

    std::optional<AST> Parser::parse_op()
    {
        consume_blanks();

        std::string op = "";

        while (std::optional<char> op_char = expect_char_of(op_chars))
        {
            op.push_back(*op_char);
        }

        if (op.empty())
        {
            return {};
        }

        if (reserved_ops.find(op) != reserved_ops.end())
        {
            throw std::runtime_error("the operator " + op + " is reserved");
        }

        return AST({ TokenType::op, op });
    }

    std::optional<AST> Parser::parse_numLit()
    {
        consume_blanks();

        std::optional<AST> minus = {};

        if (expect_op("-"))
        {
            minus = { { TokenType::minus, "-" } };

            consume_blanks();
        }

        if (expect_keyword("NaN"))
        {
            AST numLit({ TokenType::numLit, "" });
            AST real_lit({ TokenType::realLit, "" });

            if (minus)
            {
                real_lit.add_child(std::move(*minus));
            }

            real_lit.add_child({ { TokenType::nanKeyword, "NaN" } });
            numLit.add_child(std::move(real_lit));

            return numLit;
        }

        if (expect_keyword("Infinity"))
        {
            AST numLit({ TokenType::numLit, "" });
            AST real_lit({ TokenType::realLit, "" });

            if (minus)
            {
                real_lit.add_child(std::move(*minus));
            }

            real_lit.add_child({
                { TokenType::infinityKeyword, "Infinity" }
            });
            numLit.add_child(std::move(real_lit));

            return numLit;
        }

        if (!isdigit(this->ch))
        {
            if (minus)
            {
                this->charhistory.push_front(' ');
                this->charhistory.push_front('-');
            }

            return {};
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
            AST numLit({ TokenType::numLit, "" });
            AST int_lit({ TokenType::intLit, "" });

            if (minus)
            {
                int_lit.add_child(std::move(*minus));
            }

            int_lit.add_child({ { TokenType::absInt, s } });
            numLit.add_child(std::move(int_lit));

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

        AST numLit({ TokenType::numLit, "" });
        AST real_lit({ TokenType::realLit, "" });

        if (minus)
        {
            real_lit.add_child(std::move(*minus));
        }

        real_lit.add_child({ { TokenType::absReal, s } });
        numLit.add_child(std::move(real_lit));

        return numLit;
    }

    std::optional<AST> Parser::parse_chrLit()
    {
        consume_blanks();

        std::optional<AST> init_singleQuote = parse_singleQuote();

        if (!init_singleQuote)
        {
            return {};
        }

        std::optional<AST> the_char = parse_chrChr();

        if (!the_char)
        {
            throw std::runtime_error("unexpected ' or EOF");
        }

        std::optional<AST> end_singleQuote = parse_singleQuote();

        if (!end_singleQuote)
        {
            std::string err_msg = "expected ', got: ";
            err_msg += this->ch;

            throw std::runtime_error(err_msg);
        }

        AST chrLit({ TokenType::chrLit, "" });
        chrLit.add_child(std::move(*init_singleQuote));
        chrLit.add_child(std::move(*the_char));
        chrLit.add_child(std::move(*end_singleQuote));

        return chrLit;
    }

    std::optional<AST> Parser::parse_strLit()
    {
        consume_blanks();

        AST strLit({ TokenType::strLit, "" });

        std::optional<AST> init_doubleQuote = parse_doubleQuote();

        if (!init_doubleQuote)
        {
            return {};
        }

        strLit.add_child(std::move(*init_doubleQuote));

        while (this->ch != '"')
        {
            if (std::optional<AST> a_char = parse_strChr())
            {
                strLit.add_child(std::move(*a_char));
            }
            else
            {
                throw std::runtime_error(
                    "invalid escape sequence or unexpected EOF"
                );
            }
        }

        std::optional<AST> end_doubleQuote = parse_doubleQuote();

        if (!end_doubleQuote)
        {
            std::string err_msg = "expected \", got: ";
            err_msg += this->ch;

            throw std::runtime_error(err_msg);
        }

        strLit.add_child(std::move(*end_doubleQuote));

        return strLit;
    }

    std::optional<AST> Parser::parse_infixed()
    {
        consume_blanks();

        std::optional<AST> first_backtick = parse_backtick();

        if (!first_backtick)
        {
            return {};
        }

        std::optional<AST> ident = parse_qualIdent();

        if (!ident)
        {
            throw std::runtime_error("expected identifier after `");
        }

        std::optional<AST> second_backtick = parse_backtick();

        if (!second_backtick)
        {
            throw std::runtime_error("expected closing `");
        }

        AST infixed({ TokenType::infixed, "" });
        infixed.add_child(std::move(*first_backtick));
        infixed.add_child(std::move(*ident));
        infixed.add_child(std::move(*second_backtick));

        return infixed;
    }

    std::optional<AST> Parser::parse_pattern()
    {
        consume_blanks();

        AST pattern({ TokenType::pattern, "" });

        if (std::optional<AST> ident = parse_ident())
        {
            pattern.add_child(std::move(*ident));

            return pattern;
        }

        if (std::optional<AST> chrLit = parse_chrLit())
        {
            pattern.add_child(std::move(*chrLit));

            return pattern;
        }

        if (std::optional<AST> strLit = parse_strLit())
        {
            pattern.add_child(std::move(*strLit));

            return pattern;
        }

        if (std::optional<AST> numLit = parse_numLit())
        {
            pattern.add_child(std::move(*numLit));

            return pattern;
        }

        if (std::optional<AST> underscore = parse_underscore())
        {
            pattern.add_child(std::move(*underscore));

            return pattern;
        }

        if (std::optional<AST> lParen = parse_lParen())
        {
            std::optional<AST> first_pattern = parse_pattern();

            pattern.add_child(std::move(*lParen));

            if (first_pattern)
            {
                std::optional<AST> first_comma = parse_comma();

                if (!first_comma)
                {
                    throw std::runtime_error(
                        "expected comma after first element of pattern tuple"
                    );
                }

                std::optional<AST> second_pattern = parse_pattern();

                if (!second_pattern)
                {
                    throw std::runtime_error(
                        "expected 0 or at least 2 elements in pattern tuple"
                    );
                }

                pattern.add_child(std::move(*first_pattern));
                pattern.add_child(std::move(*first_comma));
                pattern.add_child(std::move(*second_pattern));

                consume_blanks();

                while (std::optional<AST> comma = parse_comma())
                {
                    std::optional<AST> unit = parse_pattern();

                    if (!unit)
                    {
                        break;
                    }

                    pattern.add_child(std::move(*comma));
                    pattern.add_child(std::move(*unit));

                    consume_blanks();
                }
            }

            std::optional<AST> rParen = parse_rParen();

            if (!rParen)
            {
                throw std::runtime_error("left paren in pattern requires )");
            }

            pattern.add_child(std::move(*rParen));

            return pattern;
        }

        if (std::optional<AST> lSqBracket = parse_lSqBracket())
        {
            std::optional<AST> first_pattern = parse_pattern();

            pattern.add_child(std::move(*lSqBracket));

            if (first_pattern)
            {
                pattern.add_child(std::move(*first_pattern));

                consume_blanks();

                while (std::optional<AST> comma = parse_comma())
                {
                    std::optional<AST> unit = parse_pattern();

                    if (!unit)
                    {
                        break;
                    }

                    pattern.add_child(std::move(*comma));
                    pattern.add_child(std::move(*unit));

                    consume_blanks();
                }
            }

            std::optional<AST> rSqBracket = parse_rSqBracket();

            if (!rSqBracket)
            {
                throw std::runtime_error(
                    "left square bracket in pattern requires ]"
                );
            }

            pattern.add_child(std::move(*rSqBracket));

            return pattern;
        }

        if (std::optional<AST> lCurlyBracket = parse_lCurlyBracket())
        {
            std::optional<AST> first_key = parse_pattern();

            pattern.add_child(std::move(*lCurlyBracket));

            if (first_key)
            {
                consume_blanks();

                std::optional<AST> first_equals = parse_equals();

                if (!first_equals)
                {
                    pattern.add_child(std::move(*first_key));

                    consume_blanks();

                    while (std::optional<AST> comma = parse_comma())
                    {
                        std::optional<AST> unit = parse_pattern();

                        if (!unit)
                        {
                            break;
                        }

                        pattern.add_child(std::move(*comma));
                        pattern.add_child(std::move(*unit));

                        consume_blanks();
                    }
                }
                else
                {
                    std::optional<AST> first_val = parse_pattern();

                    if (!first_val)
                    {
                        throw std::runtime_error(
                            "expected value pattern after "
                            "first = of dict pattern"
                        );
                    }

                    pattern.add_child(std::move(*first_key));
                    pattern.add_child(std::move(*first_equals));
                    pattern.add_child(std::move(*first_val));

                    consume_blanks();

                    while (std::optional<AST> comma = parse_comma())
                    {
                        std::optional<AST> key = parse_pattern();

                        if (!key)
                        {
                            break;
                        }

                        consume_blanks();
                        std::optional<AST> equals = parse_equals();

                        if (!equals)
                        {
                            throw std::runtime_error(
                                "expected = after key of dict pattern"
                            );
                        }

                        std::optional<AST> val = parse_pattern();

                        if (!val)
                        {
                            throw std::runtime_error(
                                "expected value pattern after "
                                "= of dict pattern"
                            );
                        }

                        pattern.add_child(std::move(*comma));
                        pattern.add_child(std::move(*key));
                        pattern.add_child(std::move(*equals));
                        pattern.add_child(std::move(*val));

                        consume_blanks();
                    }
                }
            }

            std::optional<AST> rCurlyBracket = parse_rCurlyBracket();

            if (!rCurlyBracket)
            {
                throw std::runtime_error(
                    "left curly bracket in pattern requires }"
                );
            }

            pattern.add_child(std::move(*rCurlyBracket));

            return pattern;
        }

        return {};
    }

    std::optional<AST> Parser::parse_chrChr()
    {
        static const std::unordered_set<char> ctrl_chars = {'\'', '\\'};

        if (std::optional<char> char_opt = expect_char_not_of(ctrl_chars))
        {
            return AST({ TokenType::chrChr, {*char_opt} });
        }

        if (!expect_char('\\'))
        {
            return {};
        }

        if (std::optional<char> esc_char_opt = expect_char_of(esc_chars))
        {
            return AST({ TokenType::chrChr, {'\\', *esc_char_opt} });
        }

        return {};
    }

    std::optional<AST> Parser::parse_strChr()
    {
        static const std::unordered_set<char> ctrl_chars = {'"', '\\'};

        if (std::optional<char> char_opt = expect_char_not_of(ctrl_chars))
        {
            return AST({ TokenType::strChr, {*char_opt} });
        }

        if (!expect_char('\\'))
        {
            return {};
        }

        if (std::optional<char> esc_char_opt = expect_char_of(esc_chars))
        {
            return AST({ TokenType::strChr, {'\\', *esc_char_opt} });
        }

        return {};
    }

    std::optional<AST> Parser::parse_param()
    {
        consume_blanks();

        if (this->ch == '(')
        {
            std::optional<AST> l_paren = parse_lParen();

            if (!l_paren)
            {
                throw std::logic_error(
                    "should have successfully parsed left paren"
                );
            }

            std::optional<AST> pattern = parse_pattern();

            if (!pattern)
            {
                return {};
            }

            consume_blanks();
            std::optional<AST> colon = parse_colon();

            if (!colon)
            {
                return {};
            }

            std::optional<AST> type_ident = parse_typeIdent();

            if (!type_ident)
            {
                throw std::runtime_error("expected type");
            }

            std::optional<AST> r_paren = parse_rParen();

            if (!r_paren)
            {
                throw std::runtime_error("expected ) after type");
            }

            AST param({ TokenType::param, "" });
            param.add_child(std::move(*l_paren));
            param.add_child(std::move(*pattern));
            param.add_child(std::move(*colon));
            param.add_child(std::move(*type_ident));
            param.add_child(std::move(*r_paren));

            return param;
        }

        std::optional<AST> pattern = parse_pattern();

        if (pattern)
        {
            AST param({ TokenType::param, "" });
            param.add_child(std::move(*pattern));

            return param;
        }

        return {};
    }

    std::optional<AST> Parser::parse_generator()
    {
        std::optional<AST> pattern = parse_pattern();

        if (!pattern)
        {
            return {};
        }

        std::optional<AST> l_arrow = parse_lArrow();

        if (!l_arrow)
        {
            this->charhistory.push_front(this->ch);
            this->charhistory.push_front(' ');

            std::string consumed_pattern = str_repr(*pattern);

            while (consumed_pattern.length() > 1)
            {
                this->charhistory.push_front(consumed_pattern.back());
                consumed_pattern.pop_back();
            }

            this->ch = consumed_pattern.back();

            return {};
        }

        std::optional<AST> expr = parse_expr();

        if (!expr)
        {
            throw std::runtime_error("expected expression after <-");
        }

        AST generator({ TokenType::generator, "" });
        generator.add_child(std::move(*pattern));
        generator.add_child(std::move(*l_arrow));
        generator.add_child(std::move(*expr));

        return generator;
    }

    std::optional<AST> Parser::parse_dictEntry()
    {
        consume_blanks();

        std::optional<AST> key = parse_expr();

        if (!key)
        {
            return {};
        }

        consume_blanks();

        std::optional<AST> equals = parse_equals();

        if (!equals)
        {
            return {};
        }

        std::optional<AST> val = parse_expr();

        if (!val)
        {
            throw std::runtime_error(
                "expected expression to be assigned to dict key"
            );
        }

        AST dictEntry({ TokenType::dictEntry, "" });
        dictEntry.add_child(std::move(*key));
        dictEntry.add_child(std::move(*equals));
        dictEntry.add_child(std::move(*val));

        return dictEntry;
    }

    std::optional<AST> Parser::parse_equals()
    {
        if (!expect_char('='))
        {
            return {};
        }

        return AST({ TokenType::equals, "=" });
    }

    std::optional<AST> Parser::parse_singleQuote()
    {
        if (!expect_char('\''))
        {
            return {};
        }

        return AST({ TokenType::singleQuote, "'" });
    }

    std::optional<AST> Parser::parse_doubleQuote()
    {
        if (!expect_char('"'))
        {
            return {};
        }

        return AST({ TokenType::doubleQuote, "\"" });
    }

    std::optional<AST> Parser::parse_fnKeyword()
    {
        if (!expect_keyword("fn"))
        {
            return {};
        }

        return AST({ TokenType::fnKeyword, "fn" });
    }

    std::optional<AST> Parser::parse_caseKeyword()
    {
        if (!expect_keyword("case"))
        {
            return {};
        }

        return AST({ TokenType::caseKeyword, "case" });
    }

    std::optional<AST> Parser::parse_ifKeyword()
    {
        if (!expect_keyword("if"))
        {
            return {};
        }

        return AST({ TokenType::ifKeyword, "if" });
    }

    std::optional<AST> Parser::parse_elseKeyword()
    {
        if (!expect_keyword("else"))
        {
            return {};
        }

        return AST({ TokenType::elseKeyword, "else" });
    }

    std::optional<AST> Parser::parse_tryKeyword()
    {
        if (!expect_keyword("try"))
        {
            return {};
        }

        return AST({ TokenType::tryKeyword, "try" });
    }

    std::optional<AST> Parser::parse_catchKeyword()
    {
        if (!expect_keyword("catch"))
        {
            return {};
        }

        return AST({ TokenType::catchKeyword, "catch" });
    }

    std::optional<AST> Parser::parse_whileKeyword()
    {
        if (!expect_keyword("while"))
        {
            return {};
        }

        return AST({ TokenType::whileKeyword, "while" });
    }

    std::optional<AST> Parser::parse_forKeyword()
    {
        if (!expect_keyword("for"))
        {
            return {};
        }

        return AST({ TokenType::forKeyword, "for" });
    }

    std::optional<AST> Parser::parse_inKeyword()
    {
        if (!expect_keyword("in"))
        {
            return {};
        }

        return AST({ TokenType::inKeyword, "in" });
    }

    std::optional<AST> Parser::parse_varKeyword()
    {
        if (!expect_keyword("var"))
        {
            return {};
        }

        return AST({ TokenType::varKeyword, "var" });
    }

    std::optional<AST> Parser::parse_moduleKeyword()
    {
        if (!expect_keyword("module"))
        {
            return {};
        }

        return AST({ TokenType::moduleKeyword, "module" });
    }

    std::optional<AST> Parser::parse_exposingKeyword()
    {
        if (!expect_keyword("exposing"))
        {
            return {};
        }

        return AST({ TokenType::exposingKeyword, "exposing" });
    }

    std::optional<AST> Parser::parse_hidingKeyword()
    {
        if (!expect_keyword("hiding"))
        {
            return {};
        }

        return AST({ TokenType::hidingKeyword, "hiding" });
    }

    std::optional<AST> Parser::parse_importKeyword()
    {
        if (!expect_keyword("import"))
        {
            return {};
        }

        return AST({ TokenType::importKeyword, "import" });
    }

    std::optional<AST> Parser::parse_asKeyword()
    {
        if (!expect_keyword("as"))
        {
            return {};
        }

        return AST({ TokenType::asKeyword, "as" });
    }

    std::optional<AST> Parser::parse_returnKeyword()
    {
        if (!expect_keyword("return"))
        {
            return {};
        }

        return AST({ TokenType::returnKeyword, "return" });
    }

    bool Parser::consume_lineCommentOp()
    {
        if (!expect_op("--"))
        {
            return false;
        }

        return true;
    }

    std::optional<AST> Parser::parse_dot()
    {
        if (!expect_op("."))
        {
            return {};
        }

        return AST({ TokenType::dot, "." });
    }

    std::optional<AST> Parser::parse_comma()
    {
        if (!expect_char(','))
        {
            return {};
        }

        return AST({ TokenType::comma, "," });
    }

    std::optional<AST> Parser::parse_colon()
    {
        if (!expect_op(":"))
        {
            return {};
        }

        return AST({ TokenType::colon, ":" });
    }

    std::optional<AST> Parser::parse_doubleColon()
    {
        if (!expect_op("::"))
        {
            return {};
        }

        return AST({ TokenType::doubleColon, "::" });
    }

    std::optional<AST> Parser::parse_underscore()
    {
        if (!expect_keyword("_"))
        {
            return {};
        }

        return AST({ TokenType::underscore, "_" });
    }

    std::optional<AST> Parser::parse_lArrow()
    {
        if (!expect_op("<-"))
        {
            return {};
        }

        return AST({ TokenType::lArrow, "<-" });
    }

    std::optional<AST> Parser::parse_rArrow()
    {
        if (!expect_op("->"))
        {
            return {};
        }

        return AST({ TokenType::rArrow, "->" });
    }

    std::optional<AST> Parser::parse_fatRArrow()
    {
        if (!expect_op("=>"))
        {
            return {};
        }

        return AST({ TokenType::fatRArrow, "=>" });
    }

    std::optional<AST> Parser::parse_lParen()
    {
        if (!expect_char('('))
        {
            return {};
        }

        return AST({ TokenType::lParen, "(" });
    }

    std::optional<AST> Parser::parse_rParen()
    {
        if (!expect_char(')'))
        {
            return {};
        }

        return AST({ TokenType::rParen, ")" });
    }

    std::optional<AST> Parser::parse_lSqBracket()
    {
        if (!expect_char('['))
        {
            return {};
        }

        return AST({ TokenType::lSqBracket, "[" });
    }

    std::optional<AST> Parser::parse_rSqBracket()
    {
        if (!expect_char(']'))
        {
            return {};
        }

        return AST({ TokenType::rSqBracket, "]" });
    }

    std::optional<AST> Parser::parse_lCurlyBracket()
    {
        if (!expect_char('{'))
        {
            return {};
        }

        return AST({ TokenType::lCurlyBracket, "{" });
    }

    std::optional<AST> Parser::parse_rCurlyBracket()
    {
        if (!expect_char('}'))
        {
            return {};
        }

        return AST({ TokenType::rCurlyBracket, "}" });
    }

    std::optional<AST> Parser::parse_backslash()
    {
        if (!expect_char('\\'))
        {
            return {};
        }

        return AST({ TokenType::backslash, "\\" });
    }

    std::optional<AST> Parser::parse_bar()
    {
        if (!expect_char('|'))
        {
            return {};
        }

        return AST({ TokenType::bar, "|" });
    }

    std::optional<AST> Parser::parse_backtick()
    {
        if (!expect_char('`'))
        {
            return {};
        }

        return AST({ TokenType::backtick, "`" });
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

        for (size_t j = 0; j < history_pushbacks; ++j)
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

        for (size_t j = 0; j < history_pushbacks; ++j)
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

        for (size_t j = 0; j < history_pushbacks; ++j)
        {
            this->charhistory.pop_back();
        }

        return i == op.length();
    }

    std::string Parser::get_block(AST& main_ast, TokenType body_item_type)
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

        std::optional<AST> first_item;

        switch (body_item_type)
        {
            case TokenType::line:
                first_item = parse_line(false);
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

        main_ast.add_child(std::move(*first_item));

        if (!expect_newline())
        {
            throw std::runtime_error(
                "expected newline after first item of block"
            );
        }

        while (this->currentindent == block_indent)
        {
            std::optional<AST> item;

            switch (body_item_type)
            {
                case TokenType::line:
                    item = parse_line(false);
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

            main_ast.add_child(std::move(*item));

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
