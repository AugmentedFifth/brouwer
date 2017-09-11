use std::collections::VecDeque;
use std::convert::AsRef;
use std::error::Error;
use std::fs::File;
use std::io;
use std::io::{Chars, Read};
use std::path::Path;

use token::{Token, TokenType};
use tree::Tree;


pub type AST = Tree<Token>;

pub struct Parser {
    charstream:    Chars<File>,
    eof:           bool,
    charhistory:   VecDeque<char>,
    ch:            char,
    currentindent: String,
}


impl Parser {
    pub fn new<P: AsRef<Path>>(filename: P) -> io::Result<Self> {
        let file = File::open(filename)?;

        Ok(Parser {
            charstream:    file.chars(),
            eof:           false,
            charhistory:   VecDeque::new(),
            ch:            ' ', // Dummy value.
            currentindent: String::new(),
        })
    }

    pub fn parse(&mut self) -> Result<Option<AST>, String> {
        let mut last_ch = '\0'; // Dummy value.
        let mut hit_eof = true;

        while let Some(temp_ch) = self.charstream.next() {
            self.ch = match temp_ch {
                Ok(c)  => c,
                Err(e) => return Err(e.description().to_string()),
            };

            if !self.ch.is_whitespace() {
                hit_eof = false;

                break;
            }

            last_ch = self.ch;
        }

        if hit_eof {
            self.eof = true;
        }

        if last_ch != '\0' && !is_newline(last_ch) {
            return Err(
                "source must not start with leading whitespace".to_string()
            );
        }

        let mut main_ast = new_ast_node(TokenType::Root);
        let prog = if let Some(p) = self.parse_prog()? {
            p
        } else {
            return Ok(None);
        };

        main_ast.add_child(prog);

        Ok(Some(main_ast))
    }

    fn parse_prog(&mut self) -> Result<Option<AST>, String> {
        let mut prog = new_ast_node(TokenType::Prog);

        if let Some(mod_decl) = self.parse_mod_decl()? {
            prog.add_child(mod_decl);
        } else {
            return Ok(None);
        }

        while !self.eof || !self.charhistory.is_empty() {
            if let Some(import) = self.parse_import()? {
                prog.add_child(import);
            } else {
                break;
            }
        }

        while !self.eof || !self.charhistory.is_empty() {
            if let Some(line) = self.parse_line(true)? {
                prog.add_child(line);
            } else {
                break;
            }
        }

        Ok(Some(prog))
    }

    fn parse_mod_decl(&mut self) -> Result<Option<AST>, String> {
        let mut mod_decl = new_ast_node(TokenType::ModDecl);

        if let Some(mod_kwd) = self.parse_module_keyword()? {
            mod_decl.add_child(mod_kwd);
        } else {
            return Ok(None);
        }

        if let Some(mod_name) = self.parse_ident()? {
            mod_decl.add_child(mod_name);
        } else {
            return Err(
                "expected name of module to be plain identifier".to_string()
            );
        }

        self.consume_blanks()?;

        let mut expose_or_hide = true;
        if let Some(exposing_kwd) = self.parse_exposing_keyword()? {
            mod_decl.add_child(exposing_kwd);
        } else {
            if let Some(hiding_kwd) = self.parse_hiding_keyword()? {
                mod_decl.add_child(hiding_kwd);
            } else {
                expose_or_hide = false;
            }
        }

        if expose_or_hide {
            if let Some(first_ident) = self.parse_ident()? {
                mod_decl.add_child(first_ident);
            } else {
                return Err(
                    "expected at least one item in module export/hide list"
                        .to_string()
                );
            }

            self.consume_blanks()?;

            while let Some(comma) = self.parse_comma()? {
                if let Some(ident) = self.parse_ident()? {
                    mod_decl.add_child(comma);
                    mod_decl.add_child(ident);

                    self.consume_blanks()?;
                } else {
                    break;
                }
            }
        }

        if !self.expect_newline()? {
            Err("expected newline after module declaration".to_string())
        } else {
            Ok(Some(mod_decl))
        }
    }

    fn parse_import(&mut self) -> Result<Option<AST>, String> {
        let mut import = new_ast_node(TokenType::Import);

        if let Some(import_kwd) = self.parse_import_keyword()? {
            import.add_child(import_kwd);
        } else {
            return Ok(None);
        }

        if let Some(mod_name) = self.parse_ident()? {
            import.add_child(mod_name);
        } else {
            return Err(
                "expected module name after import keyword".to_string()
            );
        }

        self.consume_blanks()?;

        if let Some(as_kwd) = self.parse_as_keyword()? {
            import.add_child(as_kwd);

            if let Some(qual_name) = self.parse_ident()? {
                import.add_child(qual_name);
            } else {
                return Err(
                    "expected namespace alias after as keyword".to_string()
                );
            }
        } else {
            if let Some(hiding_kwd) = self.parse_hiding_keyword()? {
                import.add_child(hiding_kwd);
            }

            self.consume_blanks()?;

            if let Some(l_paren) = self.parse_l_paren()? {
                import.add_child(l_paren);
            } else {
                return Err(
                    "expected left paren to start import list".to_string()
                );
            }

            if let Some(first_import_item) = self.parse_ident()? {
                import.add_child(first_import_item);
            } else {
                return Err(
                    "expected at least one import item in import list"
                        .to_string()
                );
            }

            self.consume_blanks()?;

            while let Some(comma) = self.parse_comma()? {
                if let Some(import_item) = self.parse_ident()? {
                    import.add_child(comma);
                    import.add_child(import_item);

                    self.consume_blanks()?;
                } else {
                    break;
                }
            }

            self.consume_blanks()?;

            if let Some(r_paren) = self.parse_r_paren()? {
                import.add_child(r_paren);
            } else {
                return Err(
                    "expected right paren to terminate import list".to_string()
                );
            }
        }

        if !self.expect_newline()? {
            Err("expected newline after import statement".to_string())
        } else {
            Ok(Some(import))
        }
    }

    fn parse_line(
        &mut self,
        consume_newline: bool
    ) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let mut line = new_ast_node(TokenType::Line);

        if let Some(expr) = self.parse_expr()? {
            line.add_child(expr);
        }

        self.consume_line_comment(consume_newline)?;

        if consume_newline {
            self.expect_newline()?;
        }

        Ok(Some(line))
    }

    fn consume_line_comment(
        &mut self,
        consume_newline: bool
    ) -> Result<bool, String> {
        self.consume_blanks()?;

        if !self.consume_line_comment_op()? {
            return Ok(false);
        }

        if is_newline(self.ch) {
            if consume_newline {
                self.expect_newline()?;
            }

            return Ok(true);
        }

        while let Some(&front_ch) = self.charhistory.front() {
            self.ch = front_ch;
            self.charhistory.pop_front();

            if is_newline(self.ch) {
                if consume_newline {
                    self.expect_newline()?;
                }

                return Ok(true);
            }
        }

        while let Some(temp_ch) = self.charstream.next() {
            self.ch = match temp_ch {
                Ok(c)  => c,
                Err(e) => return Err(e.description().to_string()),
            };

            if is_newline(self.ch) {
                if consume_newline {
                    self.expect_newline()?;
                }

                return Ok(true);
            }
        }

        self.eof = true;

        Ok(true)
    }

    fn parse_expr(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let first_subexpr = if let Some(subexpr) = self.parse_subexpr()? {
            subexpr
        } else {
            return Ok(None);
        };

        let mut expr = new_ast_node(TokenType::Expr);
        expr.add_child(first_subexpr);

        while let Some(subexpr) = self.parse_subexpr()? {
            expr.add_child(subexpr);
        }

        Ok(Some(expr))
    }

    fn parse_subexpr(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let mut subexpr = new_ast_node(TokenType::Subexpr);

        if let Some(var) = self.parse_var()? {
            subexpr.add_child(var);
        } else if let Some(assign) = self.parse_assign()? {
            subexpr.add_child(assign);
        } else if let Some(fn_decl) = self.parse_fn_decl()? {
            subexpr.add_child(fn_decl);
        } else if let Some(parened) = self.parse_parened()? {
            subexpr.add_child(parened);
        } else if let Some(return_) = self.parse_return()? {
            subexpr.add_child(return_);
        } else if let Some(case) = self.parse_case()? {
            subexpr.add_child(case);
        } else if let Some(if_else) = self.parse_if_else()? {
            subexpr.add_child(if_else);
        } else if let Some(try) = self.parse_try()? {
            subexpr.add_child(try);
        } else if let Some(while_) = self.parse_while()? {
            subexpr.add_child(while_);
        } else if let Some(for_) = self.parse_for()? {
            subexpr.add_child(for_);
        } else if let Some(lambda) = self.parse_lambda()? {
            subexpr.add_child(lambda);
        } else if let Some(tuple_lit) = self.parse_tuple_lit()? {
            subexpr.add_child(tuple_lit);
        } else if let Some(list_lit) = self.parse_list_lit()? {
            subexpr.add_child(list_lit);
        } else if let Some(list_comp) = self.parse_list_comp()? {
            subexpr.add_child(list_comp);
        } else if let Some(dict_lit) = self.parse_dict_lit()? {
            subexpr.add_child(dict_lit);
        } else if let Some(dict_comp) = self.parse_dict_comp()? {
            subexpr.add_child(dict_comp);
        } else if let Some(set_lit) = self.parse_set_lit()? {
            subexpr.add_child(set_lit);
        } else if let Some(set_comp) = self.parse_set_comp()? {
            subexpr.add_child(set_comp);
        } else if let Some(qual_ident) = self.parse_qual_ident()? {
            subexpr.add_child(qual_ident);
        } else if let Some(infixed) = self.parse_infixed()? {
            subexpr.add_child(infixed);
        } else if let Some(num_lit) = self.parse_num_lit()? {
            subexpr.add_child(num_lit);
        } else if let Some(chr_lit) = self.parse_chr_lit()? {
            subexpr.add_child(chr_lit);
        } else if let Some(str_lit) = self.parse_str_lit()? {
            subexpr.add_child(str_lit);
        } else if let Some(op) = self.parse_op()? {
            subexpr.add_child(op);
        } else {
            return Ok(None);
        }

        Ok(Some(subexpr))
    }

    fn parse_var(&mut self) -> Result<Option<AST>, String> {
        let var_keyword = if let Some(var_kwd) = self.parse_var_keyword()? {
            var_kwd
        } else {
            return Ok(None);
        };

        let pattern = if let Some(pat) = self.parse_pattern()? {
            pat
        } else {
            return Err(
                "left-hand side of var assignment must be a pattern"
                    .to_string()
            );
        };

        self.consume_blanks()?;

        let mut var = new_ast_node(TokenType::Var);
        var.add_child(var_keyword);
        var.add_child(pattern);

        if let Some(colon) = self.parse_colon()? {
            if let Some(type_) = self.parse_type_ident()? {
                var.add_child(colon);
                var.add_child(type_);
            } else {
                return Err(
                    "type of var binding must be a valid type identifier"
                        .to_string()
                );
            }
        }

        let equals = if let Some(eq) = self.parse_equals()? {
            eq
        } else {
            return Err("var assignment must use =".to_string());
        };

        let expr = if let Some(xpr) = self.parse_expr()? {
            xpr
        } else {
            return Err(
                "right-hand side of var assignment must be a valid expression"
                    .to_string()
            );
        };

        var.add_child(equals);
        var.add_child(expr);

        Ok(Some(var))
    }

    fn parse_assign(&mut self) -> Result<Option<AST>, String> {
        let pattern = if let Some(pat) = self.parse_pattern()? {
            pat
        } else {
            return Ok(None);
        };

        self.consume_blanks()?;

        let mut assign = new_ast_node(TokenType::Assign);
        assign.add_child(pattern.clone());

        if let Some(colon) = self.parse_colon()? {
            let type_ = if let Some(ty) = self.parse_type_ident()? {
                ty
            } else {
                return Err(
                    "type of binding must be a valid identifier".to_string()
                );
            };

            assign.add_child(colon);
            assign.add_child(type_);
        }

        self.consume_blanks()?;

        let equals = if let Some(eq) = self.parse_equals()? {
            eq
        } else {
            self.charhistory.push_front(self.ch);
            self.charhistory.push_front(' ');

            let mut consumed_pattern = str_repr(&pattern);

            while consumed_pattern.len() > 1 {
                if let Some(consumed_ch) = consumed_pattern.pop() {
                    self.charhistory.push_front(consumed_ch);
                }
            }

            if let Some(c) = consumed_pattern.pop() {
                self.ch = c;
            }

            return Ok(None);
        };

        let expr = if let Some(xpr) = self.parse_expr()? {
            xpr
        } else {
            return Err(
                "right-hand side of assignment must be a valid expression"
                    .to_string()
            );
        };

        assign.add_child(equals);
        assign.add_child(expr);

        Ok(Some(assign))
    }

    fn parse_fn_decl(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let fn_keyword = if let Some(fn_kwd) = self.parse_fn_keyword()? {
            fn_kwd
        } else {
            return Ok(None);
        };

        self.consume_blanks()?;

        let fn_name = if let Some(f_name) = self.parse_ident()? {
            f_name
        } else {
            return Err("expected function name".to_string());
        };

        self.consume_blanks()?;

        let mut fn_decl = new_ast_node(TokenType::FnDecl);
        fn_decl.add_child(fn_keyword);
        fn_decl.add_child(fn_name);

        while let Some(fn_param) = self.parse_param()? {
            fn_decl.add_child(fn_param);
        }

        self.consume_blanks()?;

        if let Some(r_arrow) = self.parse_r_arrow()? {
            fn_decl.add_child(r_arrow);

            let ret_type = if let Some(ret_ty) = self.parse_qual_ident()? {
                ret_ty
            } else {
                return Err("expected type after arrow".to_string());
            };

            fn_decl.add_child(ret_type);
        }

        self.get_block(&mut fn_decl, TokenType::Line)?;

        Ok(Some(fn_decl))
    }

    fn parse_parened(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let l_paren = if let Some(l_prn) = self.parse_l_paren()? {
            l_prn
        } else {
            return Ok(None);
        };

        let expr = if let Some(xpr) = self.parse_expr()? {
            xpr
        } else {
            return Err("expected expression within parens".to_string());
        };

        let r_paren = if let Some(r_prn) = self.parse_r_paren()? {
            r_prn
        } else {
            return Err("expected closing paren".to_string());
        };

        let mut parened = new_ast_node(TokenType::Parened);
        parened.add_child(l_paren);
        parened.add_child(expr);
        parened.add_child(r_paren);

        Ok(Some(parened))
    }

    fn parse_return(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let return_keyword =
            if let Some(ret_kwd) = self.parse_return_keyword()? {
                ret_kwd
            } else {
                return Ok(None);
            };

        let expr = if let Some(xpr) = self.parse_expr()? {
            xpr
        } else {
            return Err("expected expression to return".to_string());
        };

        let mut return_ = new_ast_node(TokenType::Return);
        return_.add_child(return_keyword);
        return_.add_child(expr);

        Ok(Some(return_))
    }

    fn parse_case(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let case_keyword = if let Some(case_kwd) = self.parse_case_keyword()? {
            case_kwd
        } else {
            return Ok(None);
        };

        self.consume_blanks()?;

        let subject_expr = if let Some(subj_expr) = self.parse_expr()? {
            subj_expr
        } else {
            return Err("expected subject expression for case".to_string());
        };

        let mut case = new_ast_node(TokenType::Case);
        case.add_child(case_keyword);
        case.add_child(subject_expr);

        self.get_block(&mut case, TokenType::CaseBranch)?;

        Ok(Some(case))
    }

    fn parse_case_branch(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let pattern = if let Some(pat) = self.parse_pattern()? {
            pat
        } else {
            return Ok(None);
        };

        let fat_r_arrow = if let Some(fat_r_arr) = self.parse_fat_r_arrow()? {
            fat_r_arr
        } else {
            return Err("expected => while parsing case branch".to_string());
        };

        let line = if let Some(l) = self.parse_line(false)? {
            l
        } else {
            return Err("expected expression(s) after =>".to_string());
        };

        let mut case_branch = new_ast_node(TokenType::CaseBranch);
        case_branch.add_child(pattern);
        case_branch.add_child(fat_r_arrow);
        case_branch.add_child(line);

        Ok(Some(case_branch))
    }

    fn parse_if_else(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let if_keyword = if let Some(if_kwd) = self.parse_if_keyword()? {
            if_kwd
        } else {
            return Ok(None);
        };

        self.consume_blanks()?;

        let if_condition = if let Some(if_cond) = self.parse_expr()? {
            if_cond
        } else {
            return Err("expected expression as if condition".to_string());
        };

        let mut if_else = new_ast_node(TokenType::IfElse);
        if_else.add_child(if_keyword);
        if_else.add_child(if_condition);

        let start_indent = self.get_block(&mut if_else, TokenType::Line)?;

        if self.currentindent != start_indent {
            return Ok(Some(if_else));
        }

        if let Some(else_kwd) = self.parse_else_keyword()? {
            if_else.add_child(else_kwd);
        } else {
            return Ok(Some(if_else));
        };

        if let Some(if_else_recur) = self.parse_if_else()? {
            if_else.add_child(if_else_recur);

            return Ok(Some(if_else));
        }

        self.get_block(&mut if_else, TokenType::Line)?;

        Ok(Some(if_else))
    }

    fn parse_try(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let mut try = new_ast_node(TokenType::Try);

        if let Some(try_kwd) = self.parse_try_keyword()? {
            self.consume_blanks()?;

            try.add_child(try_kwd);
        } else {
            return Ok(None);
        }

        let start_indent = self.get_block(&mut try, TokenType::Line)?;

        if self.currentindent != start_indent {
            return Err(
                "try must have corresponsing catch on same indent level"
                    .to_string()
            );
        }

        let catch_keyword =
            if let Some(catch_kwd) = self.parse_catch_keyword()? {
                catch_kwd
            } else {
                return Err("try must have corresponding catch".to_string());
            };

        if let Some(exception_ident) = self.parse_ident()? {
            try.add_child(catch_keyword);
            try.add_child(exception_ident);

            self.get_block(&mut try, TokenType::Line)?;

            Ok(Some(try))
        } else {
            Err("catch must name the caught exception".to_string())
        }
    }

    fn parse_while(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let while_keyword =
            if let Some(while_kwd) = self.parse_while_keyword()? {
                while_kwd
            } else {
                return Ok(None);
            };

        self.consume_blanks()?;

        if let Some(while_condition) = self.parse_expr()? {
            let mut while_ = new_ast_node(TokenType::While);
            while_.add_child(while_keyword);
            while_.add_child(while_condition);

            self.get_block(&mut while_, TokenType::Line)?;

            Ok(Some(while_))
        } else {
            Err("expected expression as while condition".to_string())
        }
    }

    fn parse_for(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let for_keyword = if let Some(for_kwd) = self.parse_for_keyword()? {
            for_kwd
        } else {
            return Ok(None);
        };

        self.consume_blanks()?;

        let for_pattern = if let Some(for_pat) = self.parse_pattern()? {
            for_pat
        } else {
            return Err(
                "expected pattern as first part of for header".to_string()
            );
        };

        self.consume_blanks()?;

        let in_keyword = if let Some(in_kwd) = self.parse_in_keyword()? {
            in_kwd
        } else {
            return Err("missing in keyword of for loop".to_string());
        };

        let iterated = if let Some(itrd) = self.parse_expr()? {
            itrd
        } else {
            return Err("for must iterate over an expression".to_string());
        };

        let mut for_ = new_ast_node(TokenType::For);
        for_.add_child(for_keyword);
        for_.add_child(for_pattern);
        for_.add_child(in_keyword);
        for_.add_child(iterated);

        self.get_block(&mut for_, TokenType::Line)?;

        Ok(Some(for_))
    }

    fn parse_lambda(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let backslash = if let Some(bkslsh) = self.parse_backslash()? {
            bkslsh
        } else {
            return Ok(None);
        };

        let first_param = if let Some(fst_param) = self.parse_param()? {
            fst_param
        } else {
            return Err("lambda expression requires 1+ args".to_string());
        };

        let mut lambda = new_ast_node(TokenType::Lambda);
        lambda.add_child(backslash);
        lambda.add_child(first_param);

        self.consume_blanks()?;

        while let Some(comma) = self.parse_comma()? {
            if let Some(param) = self.parse_param()? {
                lambda.add_child(comma);
                lambda.add_child(param);

                self.consume_blanks()?;
            } else {
                break;
            }
        }

        let r_arrow = if let Some(r_arr) = self.parse_r_arrow()? {
            r_arr
        } else {
            return Err("lambda expression requires ->".to_string());
        };

        if let Some(expr) = self.parse_expr()? {
            lambda.add_child(r_arrow);
            lambda.add_child(expr);

            Ok(Some(lambda))
        } else {
            Err("lambda body must be expression".to_string())
        }
    }

    fn parse_tuple_lit(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let l_paren = if let Some(l_prn) = self.parse_l_paren()? {
            l_prn
        } else {
            return Ok(None);
        };

        let mut tuple_lit = new_ast_node(TokenType::TupleLit);
        tuple_lit.add_child(l_paren);

        self.consume_blanks()?;

        if let Some(first_expr) = self.parse_expr()? {
            tuple_lit.add_child(first_expr);

            if let Some(first_comma) = self.parse_comma()? {
                tuple_lit.add_child(first_comma);
            } else {
                return Err(
                    "expected comma after first tuple element".to_string()
                );
            }

            if let Some(second_expr) = self.parse_expr()? {
                tuple_lit.add_child(second_expr);
            } else {
                return Err(
                    "expected 0 or at least 2 elements in tuple".to_string()
                );
            }

            self.consume_blanks()?;

            while let Some(comma) = self.parse_comma()? {
                if let Some(expr) = self.parse_expr()? {
                    tuple_lit.add_child(comma);
                    tuple_lit.add_child(expr);

                    self.consume_blanks()?;
                } else {
                    break;
                }
            }
        }

        if let Some(r_paren) = self.parse_r_paren()? {
            tuple_lit.add_child(r_paren);

            Ok(Some(tuple_lit))
        } else {
            Err("expected right paren to terminate tuple".to_string())
        }
    }

    fn parse_list_lit(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let mut list_lit = new_ast_node(TokenType::ListLit);

        if let Some(l_sq_bracket) = self.parse_l_sq_bracket()? {
            list_lit.add_child(l_sq_bracket);
        } else {
            return Ok(None);
        }

        if let Some(first_expr) = self.parse_expr()? {
            list_lit.add_child(first_expr);

            self.consume_blanks()?;

            while let Some(comma) = self.parse_comma()? {
                if let Some(expr) = self.parse_expr()? {
                    list_lit.add_child(comma);
                    list_lit.add_child(expr);

                    self.consume_blanks()?;
                } else {
                    break;
                }
            }
        }

        if let Some(r_sq_bracket) = self.parse_r_sq_bracket()? {
            list_lit.add_child(r_sq_bracket);

            Ok(Some(list_lit))
        } else {
            Err("left square bracket in list literal requires ]".to_string())
        }
    }

    fn parse_list_comp(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let l_sq_bracket =
            if let Some(l_sq_bckt) = self.parse_l_sq_bracket()? {
                l_sq_bckt
            } else {
                return Ok(None);
            };

        let expr = if let Some(xpr) = self.parse_expr()? {
            xpr
        } else {
            return Err(
                "expected expression on left-hand side of list comprehension"
                    .to_string()
            );
        };

        let bar_ = if let Some(br) = self.parse_bar()? {
            br
        } else {
            return Err("expected | for list comprehension".to_string());
        };

        let mut list_comp = new_ast_node(TokenType::ListComp);
        list_comp.add_child(l_sq_bracket);
        list_comp.add_child(expr);
        list_comp.add_child(bar_);

        let mut gen_or_cond = true;
        if let Some(first_generator) = self.parse_generator()? {
            list_comp.add_child(first_generator);
        } else {
            if let Some(first_condition) = self.parse_expr()? {
                list_comp.add_child(first_condition);
            } else {
                gen_or_cond = false;
            }
        }

        if gen_or_cond {
            self.consume_blanks()?;

            while let Some(comma) = self.parse_comma()? {
                if let Some(generator) = self.parse_generator()? {
                    list_comp.add_child(comma);
                    list_comp.add_child(generator);
                } else if let Some(condition) = self.parse_expr()? {
                    list_comp.add_child(comma);
                    list_comp.add_child(condition);
                } else {
                    break;
                }

                self.consume_blanks()?;
            }
        }

        if let Some(r_sq_bracket) = self.parse_r_sq_bracket()? {
            list_comp.add_child(r_sq_bracket);

            Ok(Some(list_comp))
        } else {
            Err("expected ] to terminate list comprehension".to_string())
        }
    }

    fn parse_dict_lit(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let mut dict_lit = new_ast_node(TokenType::DictLit);

        if let Some(l_curly_bracket) = self.parse_l_curly_bracket()? {
            dict_lit.add_child(l_curly_bracket);
        } else {
            return Ok(None);
        }

        if let Some(first_entry) = self.parse_dict_entry()? {
            self.consume_blanks()?;

            dict_lit.add_child(first_entry);

            while let Some(comma) = self.parse_comma()? {
                if let Some(entry) = self.parse_dict_entry()? {
                    dict_lit.add_child(comma);
                    dict_lit.add_child(entry);

                    self.consume_blanks()?;
                } else {
                    break;
                }
            }
        }

        if let Some(r_curly_bracket) = self.parse_r_curly_bracket()? {
            dict_lit.add_child(r_curly_bracket);

            Ok(Some(dict_lit))
        } else {
            Err("left curly bracket in dict literal requires }".to_string())
        }
    }

    fn parse_dict_comp(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let l_curly_bracket =
            if let Some(l_curly_bckt) = self.parse_l_curly_bracket()? {
                l_curly_bckt
            } else {
                return Ok(None);
            };

        let dict_entry = if let Some(dict_ent) = self.parse_dict_entry()? {
            dict_ent
        } else {
            return Err(
                "expected entry on left-hand side of dict comprehension"
                    .to_string()
            );
        };

        let bar_ = if let Some(br) = self.parse_bar()? {
            br
        } else {
            return Err("expected | for dict comprehension".to_string());
        };

        let mut dict_comp = new_ast_node(TokenType::DictComp);
        dict_comp.add_child(l_curly_bracket);
        dict_comp.add_child(dict_entry);
        dict_comp.add_child(bar_);

        let mut gen_or_cond = true;
        if let Some(first_generator) = self.parse_generator()? {
            dict_comp.add_child(first_generator);
        } else if let Some(first_condition) = self.parse_expr()? {
            dict_comp.add_child(first_condition);
        } else {
            gen_or_cond = false;
        }

        if gen_or_cond {
            self.consume_blanks()?;

            while let Some(comma) = self.parse_comma()? {
                if let Some(generator) = self.parse_generator()? {
                    dict_comp.add_child(comma);
                    dict_comp.add_child(generator);
                } else if let Some(condition) = self.parse_expr()? {
                    dict_comp.add_child(comma);
                    dict_comp.add_child(condition);
                } else {
                    break;
                }

                self.consume_blanks()?;
            }
        }

        if let Some(r_curly_bracket) = self.parse_r_curly_bracket()? {
            dict_comp.add_child(r_curly_bracket);

            Ok(Some(dict_comp))
        } else {
            Err("expected } to terminate dict comprehension".to_string())
        }
    }

    fn parse_set_lit(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let mut set_lit = new_ast_node(TokenType::SetLit);

        if let Some(l_curly_bracket) = self.parse_l_curly_bracket()? {
            set_lit.add_child(l_curly_bracket);
        } else {
            return Ok(None);
        }

        if let Some(first_expr) = self.parse_expr()? {
            self.consume_blanks()?;

            set_lit.add_child(first_expr);

            while let Some(comma) = self.parse_comma()? {
                if let Some(expr) = self.parse_expr()? {
                    set_lit.add_child(comma);
                    set_lit.add_child(expr);

                    self.consume_blanks()?;
                } else {
                    break;
                }
            }
        }

        if let Some(r_curly_bracket) = self.parse_r_curly_bracket()? {
            set_lit.add_child(r_curly_bracket);

            Ok(Some(set_lit))
        } else {
            Err("left curly bracket in set literal requires }".to_string())
        }
    }

    fn parse_set_comp(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let l_curly_bracket =
            if let Some(l_curly_bckt) = self.parse_l_curly_bracket()? {
                l_curly_bckt
            } else {
                return Ok(None);
            };

        let expr = if let Some(xpr) = self.parse_expr()? {
            xpr
        } else {
            return Err(
                "expected expression on left-hand side of set comprehension"
                    .to_string()
            );
        };

        let bar_ = if let Some(br) = self.parse_bar()? {
            br
        } else {
            return Err("expected | for set comprehension".to_string());
        };

        let mut set_comp = new_ast_node(TokenType::SetComp);
        set_comp.add_child(l_curly_bracket);
        set_comp.add_child(expr);
        set_comp.add_child(bar_);

        let mut gen_or_cond = true;
        if let Some(first_generator) = self.parse_generator()? {
            set_comp.add_child(first_generator);
        } else if let Some(first_condition) = self.parse_expr()? {
            set_comp.add_child(first_condition);
        } else {
            gen_or_cond = false;
        }

        if gen_or_cond {
            self.consume_blanks()?;

            while let Some(comma) = self.parse_comma()? {
                if let Some(generator) = self.parse_generator()? {
                    set_comp.add_child(comma);
                    set_comp.add_child(generator);
                } else if let Some(condition) = self.parse_expr()? {
                    set_comp.add_child(comma);
                    set_comp.add_child(condition);
                } else {
                    break;
                }

                self.consume_blanks()?;
            }
        }

        if let Some(r_curly_bracket) = self.parse_r_curly_bracket()? {
            set_comp.add_child(r_curly_bracket);

            Ok(Some(set_comp))
        } else {
            Err("expected } to terminate set comprehension".to_string())
        }
    }

    fn parse_qual_ident(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        if let Some(member_ident) = self.parse_member_ident()? {
            let mut qual_ident = new_ast_node(TokenType::QualIdent);
            qual_ident.add_child(member_ident);

            return Ok(Some(qual_ident));
        }

        if let Some(scoped_ident) = self.parse_scoped_ident()? {
            let mut qual_ident = new_ast_node(TokenType::QualIdent);
            qual_ident.add_child(scoped_ident);

            return Ok(Some(qual_ident));
        }

        if let Some(ident) = self.parse_ident()? {
            let mut qual_ident = new_ast_node(TokenType::QualIdent);
            qual_ident.add_child(ident);

            return Ok(Some(qual_ident));
        }

        Ok(None)
    }

    fn parse_namespaced_ident(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        if let Some(scoped_ident) = self.parse_scoped_ident()? {
            let mut namespaced_ident = new_ast_node(
                TokenType::NamespacedIdent
            );
            namespaced_ident.add_child(scoped_ident);

            return Ok(Some(namespaced_ident));
        }

        if let Some(ident) = self.parse_ident()? {
            let mut namespaced_ident = new_ast_node(
                TokenType::NamespacedIdent
            );
            namespaced_ident.add_child(ident);

            return Ok(Some(namespaced_ident));
        }

        Ok(None)
    }

    fn parse_ident(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        if self.ch != '_' && !self.ch.is_alphabetic() {
            return Ok(None);
        }

        let mut id = String::with_capacity(16);

        if self.ch == '_' {
            id.push('_');
            self.advance()?;

            if self.ch != '_' && !self.ch.is_alphanumeric() {
                self.charhistory.push_front(self.ch);
                self.ch = '_';

                return Ok(None);
            }
        }

        while self.ch == '_' || self.ch.is_alphanumeric() {
            id.push(self.ch);

            if self.advance()? {
                break;
            }
        }

        Ok(Some(new_ast_leaf(TokenType::Ident, id)))
    }

    fn parse_member_ident(&mut self) -> Result<Option<AST>, String> {
        let first_ident = if let Some(fst_ident) = self.parse_ident()? {
            fst_ident
        } else {
            return Ok(None);
        };

        if let Some(dot) = self.parse_dot()? {
            if let Some(second_ident) = self.parse_ident()? {
                let mut member_ident = new_ast_node(TokenType::MemberIdent);
                member_ident.add_child(first_ident);
                member_ident.add_child(dot);
                member_ident.add_child(second_ident);

                Ok(Some(member_ident))
            } else {
                Err("expected identifier after dot operator".to_string())
            }
        } else {
            let mut first_ident_lex = first_ident.val().lexeme.clone();

            self.charhistory.push_front(self.ch);

            while first_ident_lex.len() > 1 {
                if let Some(first_ident_lex_pop) = first_ident_lex.pop() {
                    self.charhistory.push_front(first_ident_lex_pop);
                }
            }

            if let Some(c) = first_ident_lex.pop() {
                self.ch = c;
            }

            Ok(None)
        }
    }

    fn parse_scoped_ident(&mut self) -> Result<Option<AST>, String> {
        let first_ident = if let Some(fst_ident) = self.parse_ident()? {
            fst_ident
        } else {
            return Ok(None);
        };

        if let Some(double_colon) = self.parse_double_colon()? {
            if let Some(second_ident) = self.parse_ident()? {
                let mut scoped_ident = new_ast_node(TokenType::ScopedIdent);
                scoped_ident.add_child(first_ident);
                scoped_ident.add_child(double_colon);
                scoped_ident.add_child(second_ident);

                Ok(Some(scoped_ident))
            } else {
                Err("expected identifier after dot operator".to_string())
            }
        } else {
            let mut first_ident_lex = first_ident.val().lexeme.clone();

            self.charhistory.push_front(self.ch);

            while first_ident_lex.len() > 1 {
                if let Some(first_ident_lex_pop) = first_ident_lex.pop() {
                    self.charhistory.push_front(first_ident_lex_pop);
                }
            }

            if let Some(c) = first_ident_lex.pop() {
                self.ch = c;
            }

            Ok(None)
        }
    }

    fn parse_type_ident(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        if let Some(namespaced_ident) = self.parse_namespaced_ident()? {
            let mut type_ident = new_ast_node(TokenType::TypeIdent);
            type_ident.add_child(namespaced_ident);

            Ok(Some(type_ident))
        } else if let Some(l_paren) = self.parse_l_paren()? {
            let mut type_ident = new_ast_node(TokenType::TypeIdent);
            type_ident.add_child(l_paren);

            if let Some(first_ident) = self.parse_type_ident()? {
                self.consume_blanks()?;

                let first_comma = if let Some(fst_cma) = self.parse_comma()? {
                    fst_cma
                } else {
                    return Err(
                        "expected comma after first type tuple element"
                            .to_string()
                    );
                };

                let second_ident =
                    if let Some(snd_ident) = self.parse_type_ident()? {
                        snd_ident
                    } else {
                        return Err(
                            "expected 0 or at least 2 elements in type tuple"
                                .to_string()
                        );
                    };

                type_ident.add_child(first_ident);
                type_ident.add_child(first_comma);
                type_ident.add_child(second_ident);

                self.consume_blanks()?;

                while let Some(comma) = self.parse_comma()? {
                    if let Some(ident) = self.parse_type_ident()? {
                        type_ident.add_child(comma);
                        type_ident.add_child(ident);

                        self.consume_blanks()?;
                    } else {
                        break;
                    }
                }
            }

            if let Some(r_paren) = self.parse_r_paren()? {
                type_ident.add_child(r_paren);
            } else {
                return Err(
                    "expected right paren to terminate type tuple".to_string()
                );
            }

            Ok(Some(type_ident))
        } else if let Some(l_sq_bracket) = self.parse_l_sq_bracket()? {
            let ident = if let Some(id) = self.parse_type_ident()? {
                id
            } else {
                return Err("expected type identifier after [".to_string());
            };

            let r_sq_bracket =
                if let Some(r_sq_bckt) = self.parse_r_sq_bracket()? {
                    r_sq_bckt
                } else {
                    return Err("expected closing ] of list type".to_string());
                };

            let mut type_ident = new_ast_node(TokenType::TypeIdent);
            type_ident.add_child(l_sq_bracket);
            type_ident.add_child(ident);
            type_ident.add_child(r_sq_bracket);

            Ok(Some(type_ident))
        } else if let Some(l_curly_bracket) = self.parse_l_curly_bracket()? {
            let ident = if let Some(id) = self.parse_type_ident()? {
                id
            } else {
                return Err("expected type identifier after {".to_string());
            };

            self.consume_blanks()?;

            let mut type_ident = new_ast_node(TokenType::TypeIdent);
            type_ident.add_child(l_curly_bracket);
            type_ident.add_child(ident);

            if let Some(comma) = self.parse_comma()? {
                if let Some(second_ident) = self.parse_type_ident()? {
                    type_ident.add_child(comma);
                    type_ident.add_child(second_ident);
                } else {
                    return Err("expected type identifier after ,".to_string());
                }
            }

            if let Some(r_curly_bracket) = self.parse_r_curly_bracket()? {
                type_ident.add_child(r_curly_bracket);

                Ok(Some(type_ident))
            } else {
                Err("expected closing } of dict/set type".to_string())
            }
        } else {
            Ok(None)
        }
    }

    fn parse_op(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let mut op = String::with_capacity(4);

        while let Some(op_char) = self.expect_char_op()? {
            op.push(op_char);
        }

        if op.is_empty() {
            Ok(None)
        } else if is_reserved_op(&op) {
            Err(format!("the operator {} is reserved", op))
        } else {
            Ok(Some(new_ast_leaf(TokenType::Op, op)))
        }
    }

    fn parse_num_lit(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let mut minus = None;

        if self.expect_op("-")? {
            minus = Some(new_ast_leaf(TokenType::Minus, "-"));

            self.consume_blanks()?;
        }

        if self.expect_keyword("NaN")? {
            let mut num_lit = new_ast_node(TokenType::NumLit);
            let mut real_lit = new_ast_node(TokenType::RealLit);

            if let Some(m) = minus {
                real_lit.add_child(m);
            }

            real_lit.add_child(new_ast_leaf(TokenType::NanKeyword, "NaN"));
            num_lit.add_child(real_lit);

            return Ok(Some(num_lit));
        }

        if self.expect_keyword("Infinity")? {
            let mut num_lit = new_ast_node(TokenType::NumLit);
            let mut real_lit = new_ast_node(TokenType::RealLit);

            if let Some(m) = minus {
                real_lit.add_child(m);
            }

            real_lit.add_child(
                new_ast_leaf(TokenType::InfinityKeyword, "Infinity")
            );
            num_lit.add_child(real_lit);

            return Ok(Some(num_lit));
        }

        if !self.ch.is_digit(10) {
            if minus.is_some() {
                //self.charhistory.push_front(' ');
                self.charhistory.push_front(self.ch);
                self.ch = '-';
            }

            return Ok(None);
        }

        let mut s = String::with_capacity(10);

        while self.ch.is_digit(10) {
            s.push(self.ch);

            if self.advance()? {
                break;
            }
        }

        if self.ch != '.' {
            let mut num_lit = new_ast_node(TokenType::NumLit);
            let mut int_lit = new_ast_node(TokenType::IntLit);

            if let Some(m) = minus {
                int_lit.add_child(m);
            }

            int_lit.add_child(new_ast_leaf(TokenType::AbsInt, s));
            num_lit.add_child(int_lit);

            return Ok(Some(num_lit));
        }

        s.push(self.ch);
        self.advance()?;

        if !self.ch.is_digit(10) {
            return Err(
                "expected at least one digit after decimal point".to_string()
            );
        }

        while self.ch.is_digit(10) {
            s.push(self.ch);

            if self.advance()? {
                break;
            }
        }

        let mut num_lit = new_ast_node(TokenType::NumLit);
        let mut real_lit = new_ast_node(TokenType::RealLit);

        if let Some(m) = minus {
            real_lit.add_child(m);
        }

        real_lit.add_child(new_ast_leaf(TokenType::AbsReal, s));
        num_lit.add_child(real_lit);

        Ok(Some(num_lit))
    }

    fn parse_chr_lit(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let init_single_quote =
            if let Some(s_qt) = self.parse_single_quote()? {
                s_qt
            } else {
                return Ok(None);
            };

        let the_char = if let Some(ch_ch) = self.parse_chr_chr()? {
            ch_ch
        } else {
            return Err("unexpected ' or EOF".to_string());
        };

        let end_single_quote = if let Some(s_qt) = self.parse_single_quote()? {
            s_qt
        } else {
            return Err(format!("expected ', got: {}", self.ch));
        };

        let mut chr_lit = new_ast_node(TokenType::ChrLit);
        chr_lit.add_child(init_single_quote);
        chr_lit.add_child(the_char);
        chr_lit.add_child(end_single_quote);

        Ok(Some(chr_lit))
    }

    fn parse_str_lit(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let mut str_lit = new_ast_node(TokenType::StrLit);

        if let Some(init_double_quote) = self.parse_double_quote()? {
            str_lit.add_child(init_double_quote);
        } else {
            return Ok(None);
        }

        while let Some(str_chr) = self.parse_str_chr()? {
            str_lit.add_child(str_chr);
        }

        if let Some(end_double_quote) = self.parse_double_quote()? {
            str_lit.add_child(end_double_quote);

            Ok(Some(str_lit))
        } else {
            Err(format!("expected \", got: {}", self.ch))
        }
    }

    fn parse_infixed(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let first_backtick = if let Some(bcktck) = self.parse_backtick()? {
            bcktck
        } else {
            return Ok(None);
        };

        let ident = if let Some(id) = self.parse_qual_ident()? {
            id
        } else {
            return Err("expected identifier after `".to_string());
        };

        let second_backtick = if let Some(bcktck) = self.parse_backtick()? {
            bcktck
        } else {
            return Err("expected closing `".to_string());
        };

        let mut infixed = new_ast_node(TokenType::Infixed);
        infixed.add_child(first_backtick);
        infixed.add_child(ident);
        infixed.add_child(second_backtick);

        Ok(Some(infixed))
    }

    fn parse_pattern(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let mut pattern = new_ast_node(TokenType::Pattern);

        if let Some(ident) = self.parse_ident()? {
            pattern.add_child(ident);

            Ok(Some(pattern))
        } else if let Some(chr_lit) = self.parse_chr_lit()? {
            pattern.add_child(chr_lit);

            Ok(Some(pattern))
        } else if let Some(str_lit) = self.parse_str_lit()? {
            pattern.add_child(str_lit);

            Ok(Some(pattern))
        } else if let Some(num_lit) = self.parse_num_lit()? {
            pattern.add_child(num_lit);

            Ok(Some(pattern))
        } else if let Some(underscore) = self.parse_underscore()? {
            pattern.add_child(underscore);

            Ok(Some(pattern))
        } else if let Some(l_paren) = self.parse_l_paren()? {
            pattern.add_child(l_paren);

            if let Some(first_pattern) = self.parse_pattern()? {
                let first_comma = if let Some(cma) = self.parse_comma()? {
                    cma
                } else {
                    return Err(
                        "expected comma after first element of pattern tuple"
                            .to_string()
                    );
                };

                let second_pattern = if let Some(pat) = self.parse_pattern()? {
                    pat
                } else {
                    return Err(
                        "expected 0 or at least 2 elements in pattern tuple"
                            .to_string()
                    );
                };

                pattern.add_child(first_pattern);
                pattern.add_child(first_comma);
                pattern.add_child(second_pattern);

                self.consume_blanks()?;

                while let Some(comma) = self.parse_comma()? {
                    if let Some(unit) = self.parse_pattern()? {
                        pattern.add_child(comma);
                        pattern.add_child(unit);

                        self.consume_blanks()?;
                    } else {
                        break;
                    }
                }
            }

            if let Some(r_paren) = self.parse_r_paren()? {
                pattern.add_child(r_paren);

                Ok(Some(pattern))
            } else {
                Err("left paren in pattern requires )".to_string())
            }
        } else if let Some(l_sq_bracket) = self.parse_l_sq_bracket()? {
            pattern.add_child(l_sq_bracket);

            if let Some(first_pattern) = self.parse_pattern()? {
                pattern.add_child(first_pattern);

                self.consume_blanks()?;

                while let Some(comma) = self.parse_comma()? {
                    if let Some(unit) = self.parse_pattern()? {
                        pattern.add_child(comma);
                        pattern.add_child(unit);

                        self.consume_blanks()?;
                    } else {
                        break;
                    }
                }
            }

            if let Some(r_sq_bracket) = self.parse_r_sq_bracket()? {
                pattern.add_child(r_sq_bracket);

                Ok(Some(pattern))
            } else {
                Err("left square bracket in pattern requires ]".to_string())
            }
        } else if let Some(l_curly_bracket) = self.parse_l_curly_bracket()? {
            pattern.add_child(l_curly_bracket);

            if let Some(first_key) = self.parse_pattern()? {
                self.consume_blanks()?;

                if let Some(first_equals) = self.parse_equals()? {
                    let first_val =
                        if let Some(fst_val) = self.parse_pattern()? {
                            fst_val
                        } else {
                            return Err(
                                "expected value pattern after \
                                 first = of dict pattern".to_string(),
                            );
                        };

                    pattern.add_child(first_key);
                    pattern.add_child(first_equals);
                    pattern.add_child(first_val);

                    self.consume_blanks()?;

                    while let Some(comma) = self.parse_comma()? {
                        if let Some(key) = self.parse_pattern()? {
                            self.consume_blanks()?;

                            let equals =
                                if let Some(eq) = self.parse_equals()? {
                                    eq
                                } else {
                                    return Err(
                                        "expected = after key of dict pattern"
                                            .to_string()
                                    );
                                };

                            let val = if let Some(v) = self.parse_pattern()? {
                                v
                            } else {
                                return Err(
                                    "expected value pattern after = \
                                     of dict pattern".to_string(),
                                );
                            };

                            pattern.add_child(comma);
                            pattern.add_child(key);
                            pattern.add_child(equals);
                            pattern.add_child(val);

                            self.consume_blanks()?;
                        } else {
                            break;
                        }
                    }
                } else {
                    pattern.add_child(first_key);

                    self.consume_blanks()?;

                    while let Some(comma) = self.parse_comma()? {
                        if let Some(unit) = self.parse_pattern()? {
                            pattern.add_child(comma);
                            pattern.add_child(unit);

                            self.consume_blanks()?;
                        } else {
                            break;
                        }
                    }
                }
            }

            if let Some(r_curly_bracket) = self.parse_r_curly_bracket()? {
                pattern.add_child(r_curly_bracket);

                Ok(Some(pattern))
            } else {
                Err("left curly bracket in pattern requires }".to_string())
            }
        } else {
            Ok(None)
        }
    }

    fn parse_chr_chr(&mut self) -> Result<Option<AST>, String> {
        if let Some(char_) = self.expect_char_not_chr_ctrl()? {
            Ok(Some(new_ast_leaf(TokenType::ChrChr, char_.to_string())))
        } else if !self.expect_char('\\')? {
            Ok(None)
        } else if let Some(esc_char) = self.expect_char_esc()? {
            let mut escaped = String::with_capacity(2);
            escaped.push('\\');
            escaped.push(esc_char);

            Ok(Some(new_ast_leaf(TokenType::ChrChr, escaped)))
        } else {
            Ok(None)
        }
    }

    fn parse_str_chr(&mut self) -> Result<Option<AST>, String> {
        if let Some(char_) = self.expect_char_not_str_ctrl()? {
            Ok(Some(new_ast_leaf(TokenType::StrChr, char_.to_string())))
        } else if !self.expect_char('\\')? {
            Ok(None)
        } else if let Some(esc_char) = self.expect_char_esc()? {
            let mut escaped = String::with_capacity(2);
            escaped.push('\\');
            escaped.push(esc_char);

            Ok(Some(new_ast_leaf(TokenType::StrChr, escaped)))
        } else {
            Ok(None)
        }
    }

    fn parse_param(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        if let Some(l_paren) = self.parse_l_paren()? {
            let pattern = if let Some(pat) = self.parse_pattern()? {
                pat
            } else {
                return Ok(None);
            };

            self.consume_blanks()?;

            let colon = if let Some(cln) = self.parse_colon()? {
                cln
            } else {
                return Ok(None);
            };

            let type_ident = if let Some(ty_id) = self.parse_type_ident()? {
                ty_id
            } else {
                return Err("expected type".to_string());
            };

            let r_paren = if let Some(r_prn) = self.parse_r_paren()? {
                r_prn
            } else {
                return Err("expected ) after type".to_string());
            };

            let mut param = new_ast_node(TokenType::Param);
            param.add_child(l_paren);
            param.add_child(pattern);
            param.add_child(colon);
            param.add_child(type_ident);
            param.add_child(r_paren);

            Ok(Some(param))
        } else if let Some(pattern) = self.parse_pattern()? {
            let mut param = new_ast_node(TokenType::Param);
            param.add_child(pattern);

            Ok(Some(param))
        } else {
            Ok(None)
        }
    }

    fn parse_generator(&mut self) -> Result<Option<AST>, String> {
        let pattern = if let Some(pat) = self.parse_pattern()? {
            pat
        } else {
            return Ok(None);
        };

        if let Some(l_arrow) = self.parse_l_arrow()? {
            if let Some(expr) = self.parse_expr()? {
                let mut generator = new_ast_node(TokenType::Generator);
                generator.add_child(pattern);
                generator.add_child(l_arrow);
                generator.add_child(expr);

                Ok(Some(generator))
            } else {
                Err("expected expression after <-".to_string())
            }
        } else {
            self.charhistory.push_front(self.ch);
            self.charhistory.push_front(' ');

            let mut consumed_pattern = str_repr(&pattern);

            while consumed_pattern.len() > 1 {
                if let Some(consumed_pattern_pop) = consumed_pattern.pop() {
                    self.charhistory.push_front(consumed_pattern_pop);
                }
            }

            if let Some(last_consumed_ch) = consumed_pattern.pop() {
                self.ch = last_consumed_ch;
            }

            Ok(None)
        }
    }

    fn parse_dict_entry(&mut self) -> Result<Option<AST>, String> {
        self.consume_blanks()?;

        let key = if let Some(ky) = self.parse_expr()? {
            ky
        } else {
            return Ok(None);
        };

        self.consume_blanks()?;

        let equals = if let Some(eq) = self.parse_equals()? {
            eq
        } else {
            return Ok(None);
        };

        let val = if let Some(vl) = self.parse_expr()? {
            vl
        } else {
            return Err(
                "expected expression to be assigned to dict key".to_string()
            );
        };

        let mut dict_entry = new_ast_node(TokenType::DictEntry);
        dict_entry.add_child(key);
        dict_entry.add_child(equals);
        dict_entry.add_child(val);

        Ok(Some(dict_entry))
    }

    fn parse_equals(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_char('=')? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::Equals, "=")))
        }
    }

    fn parse_single_quote(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_char('\'')? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::SingleQuote, "'")))
        }
    }

    fn parse_double_quote(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_char('"')? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::DoubleQuote, "\"")))
        }
    }

    fn parse_fn_keyword(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_keyword("fn")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::FnKeyword, "fn")))
        }
    }

    fn parse_case_keyword(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_keyword("case")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::CaseKeyword, "case")))
        }
    }

    fn parse_if_keyword(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_keyword("if")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::IfKeyword, "if")))
        }
    }

    fn parse_else_keyword(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_keyword("else")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::ElseKeyword, "else")))
        }
    }

    fn parse_try_keyword(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_keyword("try")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::TryKeyword, "try")))
        }
    }

    fn parse_catch_keyword(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_keyword("catch")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::CatchKeyword, "catch")))
        }
    }

    fn parse_while_keyword(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_keyword("while")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::WhileKeyword, "while")))
        }
    }

    fn parse_for_keyword(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_keyword("for")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::ForKeyword, "for")))
        }
    }

    fn parse_in_keyword(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_keyword("in")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::InKeyword, "in")))
        }
    }

    fn parse_var_keyword(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_keyword("var")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::VarKeyword, "var")))
        }
    }

    fn parse_module_keyword(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_keyword("module")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::ModuleKeyword, "module")))
        }
    }

    fn parse_exposing_keyword(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_keyword("exposing")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::ExposingKeyword, "exposing")))
        }
    }

    fn parse_hiding_keyword(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_keyword("hiding")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::HidingKeyword, "hiding")))
        }
    }

    fn parse_import_keyword(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_keyword("import")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::ImportKeyword, "import")))
        }
    }

    fn parse_as_keyword(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_keyword("as")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::AsKeyword, "as")))
        }
    }

    fn parse_return_keyword(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_keyword("return")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::ReturnKeyword, "return")))
        }
    }

    fn consume_line_comment_op(&mut self) -> Result<bool, String> {
        self.expect_op("--")
    }

    fn parse_dot(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_op(".")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::Dot, ".")))
        }
    }

    fn parse_comma(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_char(',')? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::Comma, ",")))
        }
    }

    fn parse_colon(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_op(":")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::Colon, ":")))
        }
    }

    fn parse_double_colon(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_op("::")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::DoubleColon, "::")))
        }
    }

    fn parse_underscore(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_keyword("_")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::Underscore, "_")))
        }
    }

    fn parse_l_arrow(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_op("<-")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::LArrow, "<-")))
        }
    }

    fn parse_r_arrow(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_op("->")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::RArrow, "->")))
        }
    }

    fn parse_fat_r_arrow(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_op("=>")? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::FatRArrow, "=>")))
        }
    }

    fn parse_l_paren(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_char('(')? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::LParen, "(")))
        }
    }

    fn parse_r_paren(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_char(')')? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::RParen, ")")))
        }
    }

    fn parse_l_sq_bracket(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_char('[')? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::LSqBracket, "[")))
        }
    }

    fn parse_r_sq_bracket(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_char(']')? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::RSqBracket, "]")))
        }
    }

    fn parse_l_curly_bracket(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_char('{')? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::LCurlyBracket, "{")))
        }
    }

    fn parse_r_curly_bracket(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_char('}')? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::RCurlyBracket, "}")))
        }
    }

    fn parse_backslash(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_char('\\')? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::Backslash, "\\")))
        }
    }

    fn parse_bar(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_char('|')? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::Bar, "|")))
        }
    }

    fn parse_backtick(&mut self) -> Result<Option<AST>, String> {
        if !self.expect_char('`')? {
            Ok(None)
        } else {
            Ok(Some(new_ast_leaf(TokenType::Backtick, "`")))
        }
    }

    /// Returns `true` when the EOF is reached and `self.charhistory` is
    /// consumed, otherwise returns `false`.
    #[inline]
    fn advance(&mut self) -> Result<bool, String> {
        if let Some(first_history) = self.charhistory.pop_front() {
            self.ch = first_history;

            Ok(self.charhistory.is_empty() && self.eof)
        } else if let Some(temp_ch) = self.charstream.next() {
            self.ch = match temp_ch {
                Ok(c)  => c,
                Err(e) => return Err(e.description().to_string()),
            };

            Ok(false)
        } else {
            self.eof = true;

            Ok(true)
        }
    }

    #[inline]
    fn consume_blanks(&mut self) -> Result<bool, String> {
        if !is_blank(self.ch) {
            return Ok(false);
        }

        while let Some(first_history) = self.charhistory.pop_front() {
            self.ch = first_history;

            if !is_blank(self.ch) {
                return Ok(true);
            }
        }

        while let Some(temp_ch) = self.charstream.next() {
            self.ch = match temp_ch {
                Ok(c)  => c,
                Err(e) => return Err(e.description().to_string()),
            };

            if !is_blank(self.ch) {
                return Ok(true);
            }
        }

        self.eof = true;

        Ok(true)
    }

    fn expect_newline(&mut self) -> Result<bool, String> {
        self.consume_blanks()?;

        if !is_newline(self.ch) {
            return Ok(false);
        }

        while let Some(first_history) = self.charhistory.pop_front() {
            self.ch = first_history;

            if is_newline(self.ch) {
                self.currentindent.clear();
            } else if is_blank(self.ch) {
                self.currentindent.push(self.ch);
            } else {
                return Ok(true);
            }
        }

        while let Some(temp_ch) = self.charstream.next() {
            self.ch = match temp_ch {
                Ok(c)  => c,
                Err(e) => return Err(e.description().to_string()),
            };

            if is_newline(self.ch) {
                self.currentindent.clear();
            } else if is_blank(self.ch) {
                self.currentindent.push(self.ch);
            } else {
                return Ok(true);
            }
        }

        self.eof = true;

        if is_newline(self.ch) {
            self.currentindent.clear();
        }

        Ok(true)
    }

    fn expect_char(&mut self, c: char) -> Result<bool, String> {
        if self.ch != c {
            Ok(false)
        } else {
            self.advance()?;

            Ok(true)
        }
    }

    fn expect_char_not_chr_ctrl(&mut self) -> Result<Option<char>, String> {
        if self.ch == '\'' || self.ch == '\\' {
            Ok(None)
        } else {
            let tmp = self.ch;
            self.advance()?;

            Ok(Some(tmp))
        }
    }

    fn expect_char_not_str_ctrl(&mut self) -> Result<Option<char>, String> {
        if self.ch == '"' || self.ch == '\\' {
            Ok(None)
        } else {
            let tmp = self.ch;
            self.advance()?;

            Ok(Some(tmp))
        }
    }

    fn expect_char_esc(&mut self) -> Result<Option<char>, String> {
        if self.ch != '\'' &&
           self.ch != '"'  &&
           self.ch != 't'  &&
           self.ch != 'v'  &&
           self.ch != 'n'  &&
           self.ch != 'r'  &&
           self.ch != 'b'  &&
           self.ch != '0'
        {
            Ok(None)
        } else {
            let tmp = self.ch;
            self.advance()?;

            Ok(Some(tmp))
        }
    }

    fn expect_char_op(&mut self) -> Result<Option<char>, String> {
        if self.ch != '?'  &&
           self.ch != '<'  &&
           self.ch != '>'  &&
           self.ch != '='  &&
           self.ch != '%'  &&
           self.ch != '\\' &&
           self.ch != '~'  &&
           self.ch != '!'  &&
           self.ch != '@'  &&
           self.ch != '#'  &&
           self.ch != '$'  &&
           self.ch != '|'  &&
           self.ch != '&'  &&
           self.ch != '*'  &&
           self.ch != '/'  &&
           self.ch != '+'  &&
           self.ch != '^'  &&
           self.ch != '-'  &&
           self.ch != ':'  &&
           self.ch != ';'
        {
            Ok(None)
        } else {
            let tmp = self.ch;
            self.advance()?;

            Ok(Some(tmp))
        }
    }

    fn expect_keyword(&mut self, kwd: &str) -> Result<bool, String> {
        let mut kwd_iter = kwd.chars();

        if let Some(first_kwd_ch) = kwd_iter.next() {
            if self.ch != first_kwd_ch {
                return Ok(false);
            }
        } else {
            return Err("empty keyword".to_string());
        }

        let mut historic_stack = Vec::with_capacity(5);

        while let Some(&first_history) = self.charhistory.front() {
            if let Some(next_ch) = kwd_iter.next() {
                if first_history != next_ch {
                    while historic_stack.len() > 1 {
                        if let Some(historic_pop) = historic_stack.pop() {
                            self.charhistory.push_front(historic_pop);
                        }
                    }

                    if let Some(historic_back) = historic_stack.pop() {
                        self.ch = historic_back;
                    }

                    return Ok(false);
                }

                historic_stack.push(self.ch);

                if let Some(first_history) = self.charhistory.pop_front() {
                    self.ch = first_history;
                }
            } else {
                let not_keyword = if self.charhistory.is_empty() {
                    let temp_ch = self.ch;

                    if let Some(temp_ch) = self.charstream.next() {
                        self.ch = match temp_ch {
                            Ok(c)  => c,
                            Err(e) => return Err(e.description().to_string()),
                        };
                    } else {
                        self.eof = true;
                    }

                    let not_keyword_tmp =
                        self.ch == '_' || self.ch.is_alphanumeric();

                    self.charhistory.push_back(self.ch);
                    self.ch = temp_ch;

                    not_keyword_tmp
                } else {
                    first_history == '_' || first_history.is_alphanumeric()
                };

                if not_keyword {
                    while let Some(historic_back) = historic_stack.pop() {
                        self.charhistory.push_front(self.ch);
                        self.ch = historic_back;
                    }

                    return Ok(false);
                }

                self.advance()?;

                return Ok(true);
            }
        }

        self.charhistory.push_back(self.ch);
        let mut history_pushbacks = 1usize;

        while let Some(next_ch) = kwd_iter.next() {
            if let Some(Ok(temp_ch)) = self.charstream.next() {
                self.ch = temp_ch;

                if self.ch != next_ch {
                    while historic_stack.len() > 1 {
                        if let Some(historic_pop) = historic_stack.pop() {
                            self.charhistory.push_front(historic_pop);
                        }
                    }

                    if let Some(historic_back) = historic_stack.pop() {
                        self.ch = historic_back;
                    }

                    return Ok(false);
                }

                self.charhistory.push_back(self.ch);
                history_pushbacks += 1;
            } else {
                self.eof = true;

                break;
            }
        }

        if let Some(temp_ch) = self.charstream.next() {
            self.ch = match temp_ch {
                Ok(c)  => c,
                Err(e) => return Err(e.description().to_string()),
            };
        } else {
            self.eof = true;
        }

        if self.ch == '_' || self.ch.is_alphanumeric() {
            while historic_stack.len() > 1 {
                if let Some(historic_pop) = historic_stack.pop() {
                    self.charhistory.push_front(historic_pop);
                }
            }

            if let Some(historic_back) = historic_stack.pop() {
                self.ch = historic_back;
            }
        }

        for _ in 0..history_pushbacks {
            self.charhistory.pop_back();
        }

        Ok(kwd_iter.next().is_none())
    }

    fn expect_op(&mut self, op: &str) -> Result<bool, String> {
        let mut op_iter = op.chars();

        if let Some(first_char) = op_iter.next() {
            if self.ch != first_char {
                return Ok(false);
            }
        } else {
            return Err("empty operator".to_string());
        }

        let mut historic_stack = Vec::with_capacity(4);

        while let Some(&first_history) = self.charhistory.front() {
            if let Some(next_char) = op_iter.next() {
                if first_history != next_char {
                    while historic_stack.len() > 1 {
                        if let Some(historic_pop) = historic_stack.pop() {
                            self.charhistory.push_front(historic_pop);
                        }
                    }

                    if let Some(historic_back) = historic_stack.pop() {
                        self.ch = historic_back;
                    }

                    return Ok(false);
                }

                historic_stack.push(self.ch);

                if let Some(first_history) = self.charhistory.pop_front() {
                    self.ch = first_history;
                }
            } else {
                let not_op = if self.charhistory.is_empty() {
                    let temp_ch = self.ch;

                    if let Some(temp_ch) = self.charstream.next() {
                        self.ch = match temp_ch {
                            Ok(c)  => c,
                            Err(e) => return Err(e.description().to_string()),
                        };
                    } else {
                        self.eof = true;
                    }

                    let not_op_tmp = is_op_char(self.ch);

                    self.charhistory.push_back(self.ch);
                    self.ch = temp_ch;

                    not_op_tmp
                } else {
                    is_op_char(first_history)
                };

                if not_op {
                    while let Some(historic_back) = historic_stack.pop() {
                        self.charhistory.push_front(self.ch);
                        self.ch = historic_back;
                    }

                    return Ok(false);
                }

                self.advance()?;

                return Ok(true);
            }
        }

        self.charhistory.push_back(self.ch);
        let mut history_pushbacks = 1usize;

        while let Some(next_ch) = op_iter.next() {
            if let Some(Ok(temp_ch)) = self.charstream.next() {
                self.ch = temp_ch;

                if self.ch != next_ch {
                    while historic_stack.len() > 1 {
                        if let Some(historic_pop) = historic_stack.pop() {
                            self.charhistory.push_front(historic_pop);
                        }
                    }

                    if let Some(historic_back) = historic_stack.pop() {
                        self.ch = historic_back;
                    }

                    return Ok(false);
                }

                self.charhistory.push_back(self.ch);
                history_pushbacks += 1;
            } else {
                self.eof = true;

                break;
            }
        }

        if let Some(temp_ch) = self.charstream.next() {
            self.ch = match temp_ch {
                Ok(c)  => c,
                Err(e) => return Err(e.description().to_string()),
            };
        } else {
            self.eof = true;
        }

        if is_op_char(self.ch) {
            while historic_stack.len() > 1 {
                if let Some(historic_pop) = historic_stack.pop() {
                    self.charhistory.push_front(historic_pop);
                }
            }

            if let Some(historic_back) = historic_stack.pop() {
                self.ch = historic_back;
            }
        }

        for _ in 0..history_pushbacks {
            self.charhistory.pop_back();
        }

        Ok(op_iter.next().is_none())
    }

    fn get_block(
        &mut self,
        main_ast:       &mut AST,
        body_item_type: TokenType
    ) -> Result<String, String> {
        let start_indent = self.currentindent.clone();

        if !self.expect_newline()? {
            return Err("expected newline after header".to_string());
        }

        let block_indent = self.currentindent.clone();

        if start_indent.len() >= block_indent.len() ||
           !block_indent.starts_with(&start_indent)
        {
            return Err("improper indentation after header".to_string());
        }

        if let Some(first_item) = match body_item_type {
            TokenType::Line       => self.parse_line(false)?,
            TokenType::CaseBranch => self.parse_case_branch()?,
            _ => return Err("unhandled body item type".to_string()),
        } {
            main_ast.add_child(first_item);
        } else {
            return Err("expected at least one item in block".to_string());
        };

        if !self.expect_newline()? {
            return Err(
                "expected newline after first item of block".to_string()
            );
        }

        while self.currentindent == block_indent {
            if let Some(item) = match body_item_type {
                TokenType::Line       => self.parse_line(false)?,
                TokenType::CaseBranch => self.parse_case_branch()?,
                _ => return Err("unhandled body item type".to_string()),
            } {
                main_ast.add_child(item);

                if !self.expect_newline()? {
                    return Err(
                        "expected newline after block item".to_string()
                    );
                }
            } else {
                return Err("expected item in block".to_string());
            }
        }

        Ok(start_indent)
    }
}

#[inline(always)]
pub fn new_ast_node(token_type: TokenType) -> AST {
    AST::new(Token::new(token_type, String::new()))
}

#[inline(always)]
pub fn new_ast_leaf<S: Into<String>>(token_type: TokenType, s: S) -> AST {
    AST::new(Token::new(token_type, s.into()))
}

#[inline(always)]
pub fn str_repr(ast: &AST) -> String {
    if !ast.val().lexeme.is_empty() {
        ast.val().lexeme.clone()
    } else {
        let mut ret = String::with_capacity(6 * ast.children().len());

        for child_ast in ast.children() {
            ret += &str_repr(child_ast);

            let child_type = &child_ast.val().type_;

            if child_type != &TokenType::StrChr      &&
               child_type != &TokenType::ChrChr      &&
               child_type != &TokenType::DoubleQuote &&
               child_type != &TokenType::SingleQuote
            {
                ret.push(' ');
            }
        }

        ret
    }
}

pub fn log_depth_first(ast: &AST, cur_depth: usize) {
    for _ in 0..cur_depth {
        print!("  ");
    }

    let lex = &ast.val().lexeme;

    if lex.is_empty() {
        println!(" └─ {:?}", ast.val().type_);
    } else {
        println!(" └─ {:?} \"{}\"", ast.val().type_, lex);
    }

    for child_ast in ast.children() {
        log_depth_first(child_ast, cur_depth + 1);
    }
}

#[inline(always)]
pub fn is_newline(c: char) -> bool {
    c == '\n' || c == '\r'
}

#[inline(always)]
pub fn is_blank(c: char) -> bool {
    c == ' ' || c == '\t'
}

fn is_op_char(c: char) -> bool {
    c == '?'  ||
    c == '<'  ||
    c == '>'  ||
    c == '='  ||
    c == '%'  ||
    c == '\\' ||
    c == '~'  ||
    c == '!'  ||
    c == '@'  ||
    c == '#'  ||
    c == '$'  ||
    c == '|'  ||
    c == '&'  ||
    c == '*'  ||
    c == '/'  ||
    c == '+'  ||
    c == '^'  ||
    c == '-'  ||
    c == ':'  ||
    c == ';'
}

pub fn is_reserved_op(op_str: &str) -> bool {
    op_str == ":"  ||
    op_str == "->" ||
    op_str == "=>" ||
    op_str == "<-" ||
    op_str == "--" ||
    op_str == "|"  ||
    op_str == "\\" ||
    op_str == "="  ||
    op_str == "."  ||
    op_str == "::"
}
