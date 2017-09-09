use std::collections::VecDeque;
use std::convert::AsRef;
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

    pub fn parse(&mut self) -> Option<AST> {
        let mut last_ch = '\0'; // Dummy value.

        let mut hit_eof = true;
        while let Some(Ok(temp_ch)) = self.charstream.next() {
            self.ch = temp_ch;

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
            panic!("source must not start with leading whitespace");
        }

        let mut main_ast = new_ast_node(TokenType::Root);
        let prog = self.parse_prog();

        if prog.is_none() {
            return None;
        }

        main_ast.add_child(prog.unwrap());

        Some(main_ast)
    }

    fn parse_prog(&mut self) -> Option<AST> {
        let mut prog = new_ast_node(TokenType::Prog);

        let module_decl = self.parse_mod_decl();

        if module_decl.is_some() {
            prog.add_child(module_decl.unwrap());
        }

        while !self.eof || !self.charhistory.is_empty() {
            let import = self.parse_import();

            if import.is_none() {
                break;
            }

            prog.add_child(import.unwrap());
        }

        while !self.eof || !self.charhistory.is_empty() {
            if let Some(line) = self.parse_line(true) {
                prog.add_child(line);
            } else {
                break;
            }
        }

        Some(prog)
    }

    fn parse_mod_decl(&mut self) -> Option<AST> {
        let module_keyword = self.parse_module_keyword();

        if module_keyword.is_none() {
            return None;
        }

        let mut mod_decl = new_ast_node(TokenType::ModDecl);
        mod_decl.add_child(module_keyword.unwrap());

        let mod_name = self.parse_ident();

        if mod_name.is_none() {
            panic!("expected name of module to be plain identifier");
        }

        mod_decl.add_child(mod_name.unwrap());

        self.consume_blanks();

        let exposing_keyword = self.parse_exposing_keyword();
        let hiding_keyword = if exposing_keyword.is_none() {
            self.parse_hiding_keyword()
        } else {
            None
        };

        if exposing_keyword.is_some() || hiding_keyword.is_some() {
            if exposing_keyword.is_some() {
                mod_decl.add_child(exposing_keyword.unwrap());
            } else {
                mod_decl.add_child(hiding_keyword.unwrap());
            }

            let first_ident = self.parse_ident();

            if first_ident.is_none() {
                panic!("expected at least one item in module export/hide list");
            }

            mod_decl.add_child(first_ident.unwrap());

            self.consume_blanks();

            while let Some(comma) = self.parse_comma() {
                let ident = self.parse_ident();

                if ident.is_none() {
                    break;
                }

                mod_decl.add_child(comma);
                mod_decl.add_child(ident.unwrap());

                self.consume_blanks();
            }
        }

        if !self.expect_newline() {
            panic!("expected newline after module declaration");
        }

        Some(mod_decl)
    }

    fn parse_import(&mut self) -> Option<AST> {
        let import_keyword = self.parse_import_keyword();

        if import_keyword.is_none() {
            return None;
        }

        let mut import = new_ast_node(TokenType::Import);
        import.add_child(import_keyword.unwrap());

        let mod_name = self.parse_ident();

        if mod_name.is_none() {
            panic!("expected module name after import keyword");
        }

        import.add_child(mod_name.unwrap());

        self.consume_blanks();
        let as_keyword = self.parse_as_keyword();

        if as_keyword.is_some() {
            import.add_child(as_keyword.unwrap());

            let qual_name = self.parse_ident();

            if qual_name.is_none() {
                panic!("expected namespace alias after as keyword");
            }

            import.add_child(qual_name.unwrap());
        } else {
            let hiding_keyword = self.parse_hiding_keyword();

            if hiding_keyword.is_some() {
                import.add_child(hiding_keyword.unwrap());
            }

            self.consume_blanks();

            let l_paren = self.parse_l_paren();

            if l_paren.is_none() {
                panic!("expected left paren to start import list");
            }

            import.add_child(l_paren.unwrap());

            let first_import_item = self.parse_ident();

            if first_import_item.is_none() {
                panic!("expected at least one import item in import list");
            }

            import.add_child(first_import_item.unwrap());

            self.consume_blanks();

            while let Some(comma) = self.parse_comma() {
                let import_item = self.parse_ident();

                if import_item.is_none() {
                    break;
                }

                import.add_child(comma);
                import.add_child(import_item.unwrap());

                self.consume_blanks();
            }

            self.consume_blanks();

            let r_paren = self.parse_r_paren();

            if r_paren.is_none() {
                panic!("expected right paren to terminate import list");
            }

            import.add_child(r_paren.unwrap());
        }

        if !self.expect_newline() {
            panic!("expected newline after import statement");
        }

        Some(import)
    }

    fn parse_line(&mut self, consume_newline: bool) -> Option<AST> {
        self.consume_blanks();

        let mut line = new_ast_node(TokenType::Line);

        let expr = self.parse_expr();

        if expr.is_some() {
            line.add_child(expr.unwrap());
        }

        self.consume_line_comment(consume_newline);

        if consume_newline {
            self.expect_newline();
        }

        Some(line)
    }

    fn consume_line_comment(&mut self, consume_newline: bool) -> bool {
        self.consume_blanks();

        if !self.consume_line_comment_op() {
            return false;
        }

        if is_newline(self.ch) {
            if consume_newline {
                self.expect_newline();
            }

            return true;
        }

        while let Some(&front_ch) = self.charhistory.front() {
            self.ch = front_ch;
            self.charhistory.pop_front();

            if is_newline(self.ch) {
                if consume_newline {
                    self.expect_newline();
                }

                return true;
            }
        }

        while let Some(Ok(temp_ch)) = self.charstream.next() {
            self.ch = temp_ch;

            if is_newline(self.ch) {
                if consume_newline {
                    self.expect_newline();
                }

                return true;
            }
        }

        self.eof = true;

        true
    }

    fn parse_expr(&mut self) -> Option<AST> {
        self.consume_blanks();

        let first_subexpr = match self.parse_subexpr() {
            Some(subexpr) => subexpr,
            _             => return None,
        };

        let mut expr = new_ast_node(TokenType::Expr);
        expr.add_child(first_subexpr);

        while let Some(subexpr) = self.parse_subexpr() {
            expr.add_child(subexpr);
        }

        Some(expr)
    }

    fn parse_subexpr(&mut self) -> Option<AST> {
        self.consume_blanks();

        let mut subexpr = new_ast_node(TokenType::Subexpr);

        if let Some(var) = self.parse_var() {
            subexpr.add_child(var);
        } else if let Some(assign) = self.parse_assign() {
            subexpr.add_child(assign);
        } else if let Some(fn_decl) = self.parse_fn_decl() {
            subexpr.add_child(fn_decl);
        } else if let Some(parened) = self.parse_parened() {
            subexpr.add_child(parened);
        } else if let Some(return_) = self.parse_return() {
            subexpr.add_child(return_);
        } else if let Some(case) = self.parse_case() {
            subexpr.add_child(case);
        } else if let Some(if_else) = self.parse_if_else() {
            subexpr.add_child(if_else);
        } else if let Some(try) = self.parse_try() {
            subexpr.add_child(try);
        } else if let Some(while_) = self.parse_while() {
            subexpr.add_child(while_);
        } else if let Some(for_) = self.parse_for() {
            subexpr.add_child(for_);
        } else if let Some(lambda) = self.parse_lambda() {
            subexpr.add_child(lambda);
        } else if let Some(tuple_lit) = self.parse_tuple_lit() {
            subexpr.add_child(tuple_lit);
        } else if let Some(list_lit) = self.parse_list_lit() {
            subexpr.add_child(list_lit);
        } else if let Some(list_comp) = self.parse_list_comp() {
            subexpr.add_child(list_comp);
        } else if let Some(dict_lit) = self.parse_dict_lit() {
            subexpr.add_child(dict_lit);
        } else if let Some(dict_comp) = self.parse_dict_comp() {
            subexpr.add_child(dict_comp);
        } else if let Some(set_lit) = self.parse_set_lit() {
            subexpr.add_child(set_lit);
        } else if let Some(set_comp) = self.parse_set_comp() {
            subexpr.add_child(set_comp);
        } else if let Some(qual_ident) = self.parse_qual_ident() {
            subexpr.add_child(qual_ident);
        } else if let Some(infixed) = self.parse_infixed() {
            subexpr.add_child(infixed);
        } else if let Some(num_lit) = self.parse_num_lit() {
            subexpr.add_child(num_lit);
        } else if let Some(chr_lit) = self.parse_chr_lit() {
            subexpr.add_child(chr_lit);
        } else if let Some(str_lit) = self.parse_str_lit() {
            subexpr.add_child(str_lit);
        } else if let Some(op) = self.parse_op() {
            subexpr.add_child(op);
        } else {
            return None;
        }

        Some(subexpr)
    }

    fn parse_var(&mut self) -> Option<AST> {
        let var_keyword = self.parse_var_keyword();

        if var_keyword.is_none() {
            return None;
        }

        let pattern = self.parse_pattern();

        if pattern.is_none() {
            panic!("left-hand side of var assignment must be a pattern");
        }

        self.consume_blanks();

        let mut var = new_ast_node(TokenType::Var);
        var.add_child(var_keyword.unwrap());
        var.add_child(pattern.unwrap());

        if let Some(colon) = self.parse_colon() {
            let type_ = self.parse_qual_ident();

            if type_.is_none() {
                panic!("type of var binding must be a valid identifier");
            }

            var.add_child(colon);
            var.add_child(type_.unwrap());
        }

        let equals = self.parse_equals();

        if equals.is_none() {
            panic!("var assignment must use =");
        }

        let expr = self.parse_expr();

        if expr.is_none() {
            panic!("right-hand side of var assignment must be a valid expression");
        }

        var.add_child(equals.unwrap());
        var.add_child(expr.unwrap());

        Some(var)
    }

    fn parse_assign(&mut self) -> Option<AST> {
        let pattern = match self.parse_pattern() {
            Some(pattern) => pattern,
            _             => return None,
        };

        self.consume_blanks();

        let mut assign = new_ast_node(TokenType::Assign);
        assign.add_child(pattern.clone());

        if let Some(colon) = self.parse_colon() {
            let type_ = self.parse_type_ident().expect(
                "type of binding must be a valid identifier"
            );

            assign.add_child(colon);
            assign.add_child(type_);
        }

        self.consume_blanks();

        let equals = self.parse_equals();

        if equals.is_none() {
            self.charhistory.push_front(self.ch);
            self.charhistory.push_front(' ');

            let mut consumed_pattern = str_repr(&pattern);

            while consumed_pattern.len() > 1 {
                self.charhistory.push_front(consumed_pattern.pop().unwrap());
            }

            if let Some(c) = consumed_pattern.pop() {
                self.ch = c;
            }

            return None;
        }

        let expr = self.parse_expr();

        if expr.is_none() {
            panic!("right-hand side of assignment must be a valid expression");
        }

        assign.add_child(equals.unwrap());
        assign.add_child(expr.unwrap());

        Some(assign)
    }

    fn parse_fn_decl(&mut self) -> Option<AST> {
        self.consume_blanks();

        let fn_keyword = match self.parse_fn_keyword() {
            Some(kwd) => kwd,
            _         => return None,
        };

        self.consume_blanks();

        let fn_name = self.parse_ident().expect("expected function name");

        self.consume_blanks();

        let mut fn_decl = new_ast_node(TokenType::FnDecl);
        fn_decl.add_child(fn_keyword);
        fn_decl.add_child(fn_name);

        while let Some(fn_param) = self.parse_param() {
            fn_decl.add_child(fn_param);
        }

        self.consume_blanks();

        if let Some(r_arrow) = self.parse_r_arrow() {
            fn_decl.add_child(r_arrow);

            let ret_type =
                self.parse_qual_ident().expect("expected type after arrow");

            fn_decl.add_child(ret_type);
        }

        self.get_block(&mut fn_decl, TokenType::Line);

        Some(fn_decl)
    }

    fn parse_parened(&mut self) -> Option<AST> {
        self.consume_blanks();

        let l_paren = self.parse_l_paren();

        if l_paren.is_none() {
            return None;
        }

        let expr = self.parse_expr();

        if expr.is_none() {
            panic!("expected expression within parens");
        }

        let r_paren = self.parse_r_paren();

        if r_paren.is_none() {
            panic!("expected closing paren");
        }

        let mut parened = new_ast_node(TokenType::Parened);
        parened.add_child(l_paren.unwrap());
        parened.add_child(expr.unwrap());
        parened.add_child(r_paren.unwrap());

        Some(parened)
    }

    fn parse_return(&mut self) -> Option<AST> {
        self.consume_blanks();

        let return_keyword = self.parse_return_keyword();

        if return_keyword.is_none() {
            return None;
        }

        let expr = self.parse_expr();

        if expr.is_none() {
            panic!("expected expression to return");
        }

        let mut return_ = new_ast_node(TokenType::Return);
        return_.add_child(return_keyword.unwrap());
        return_.add_child(expr.unwrap());

        Some(return_)
    }

    fn parse_case(&mut self) -> Option<AST> {
        self.consume_blanks();

        let case_keyword = self.parse_case_keyword();

        if case_keyword.is_none() {
            return None;
        }

        self.consume_blanks();

        let subject_expr = self.parse_expr();

        if subject_expr.is_none() {
            panic!("expected subject expression for case");
        }

        let mut case = new_ast_node(TokenType::Case);
        case.add_child(case_keyword.unwrap());
        case.add_child(subject_expr.unwrap());

        self.get_block(&mut case, TokenType::CaseBranch);

        Some(case)
    }

    fn parse_case_branch(&mut self) -> Option<AST> {
        self.consume_blanks();

        let pattern = self.parse_pattern();

        if pattern.is_none() {
            return None;
        }

        let fat_r_arrow = self.parse_fat_r_arrow();

        if fat_r_arrow.is_none() {
            panic!("expected => while parsing case branch");
        }

        let line = self.parse_line(false);

        if line.is_none() {
            panic!("expected expression(s) after =>");
        }

        let mut case_branch = new_ast_node(TokenType::CaseBranch);
        case_branch.add_child(pattern.unwrap());
        case_branch.add_child(fat_r_arrow.unwrap());
        case_branch.add_child(line.unwrap());

        Some(case_branch)
    }

    fn parse_if_else(&mut self) -> Option<AST> {
        self.consume_blanks();

        let if_keyword = self.parse_if_keyword();

        if if_keyword.is_none() {
            return None;
        }

        self.consume_blanks();

        let if_condition = self.parse_expr();

        if if_condition.is_none() {
            panic!("expected expression as if condition");
        }

        let mut if_else = new_ast_node(TokenType::IfElse);
        if_else.add_child(if_keyword.unwrap());
        if_else.add_child(if_condition.unwrap());

        let start_indent = self.get_block(&mut if_else, TokenType::Line);

        if self.currentindent != start_indent {
            return Some(if_else);
        }

        let else_keyword = self.parse_else_keyword();

        if else_keyword.is_none() {
            return Some(if_else);
        }

        if_else.add_child(else_keyword.unwrap());

        if let Some(if_else_recur) = self.parse_if_else() {
            if_else.add_child(if_else_recur);

            return Some(if_else);
        }

        self.get_block(&mut if_else, TokenType::Line);

        Some(if_else)
    }

    fn parse_try(&mut self) -> Option<AST> {
        self.consume_blanks();

        let try_keyword = self.parse_try_keyword();

        if try_keyword.is_none() {
            return None;
        }

        self.consume_blanks();

        let mut try = new_ast_node(TokenType::Try);
        try.add_child(try_keyword.unwrap());

        let start_indent = self.get_block(&mut try, TokenType::Line);

        if self.currentindent != start_indent {
            panic!("try must have corresponsing catch on same indent level");
        }

        let catch_keyword = self.parse_catch_keyword();

        if catch_keyword.is_none() {
            panic!("try must have corresponding catch");
        }

        let exception_ident = self.parse_ident();

        if exception_ident.is_none() {
            panic!("catch must name the caught exception");
        }

        try.add_child(catch_keyword.unwrap());
        try.add_child(exception_ident.unwrap());

        self.get_block(&mut try, TokenType::Line);

        Some(try)
    }

    fn parse_while(&mut self) -> Option<AST> {
        self.consume_blanks();

        let while_keyword = self.parse_while_keyword();

        if while_keyword.is_none() {
            return None;
        }

        self.consume_blanks();

        let while_condition = self.parse_expr();

        if while_condition.is_none() {
            panic!("expected expression as while condition");
        }

        let mut while_ = new_ast_node(TokenType::While);
        while_.add_child(while_keyword.unwrap());
        while_.add_child(while_condition.unwrap());

        self.get_block(&mut while_, TokenType::Line);

        Some(while_)
    }

    fn parse_for(&mut self) -> Option<AST> {
        self.consume_blanks();

        let for_keyword = self.parse_for_keyword();

        if for_keyword.is_none() {
            return None;
        }

        self.consume_blanks();

        let for_pattern = self.parse_pattern();

        if for_pattern.is_none() {
            panic!("expected pattern as first part of for header");
        }

        self.consume_blanks();

        let in_keyword = self.parse_in_keyword();

        if in_keyword.is_none() {
            panic!("missing in keyword of for loop");
        }

        let iterated = self.parse_expr();

        if iterated.is_none() {
            panic!("for must iterate over an expression");
        }

        let mut for_ = new_ast_node(TokenType::For);
        for_.add_child(for_keyword.unwrap());
        for_.add_child(for_pattern.unwrap());
        for_.add_child(in_keyword.unwrap());
        for_.add_child(iterated.unwrap());

        self.get_block(&mut for_, TokenType::Line);

        Some(for_)
    }

    fn parse_lambda(&mut self) -> Option<AST> {
        self.consume_blanks();

        let backslash = self.parse_backslash();

        if backslash.is_none() {
            return None;
        }

        let first_param = self.parse_param();

        if first_param.is_none() {
            panic!("lambda expression requires 1+ args");
        }

        let mut lambda = new_ast_node(TokenType::Lambda);
        lambda.add_child(backslash.unwrap());
        lambda.add_child(first_param.unwrap());

        self.consume_blanks();

        while let Some(comma) = self.parse_comma() {
            let param = self.parse_param();

            if param.is_none() {
                break;
            }

            lambda.add_child(comma);
            lambda.add_child(param.unwrap());

            self.consume_blanks();
        }

        let arrow = self.parse_r_arrow();

        if arrow.is_none() {
            panic!("lambda expression requires ->");
        }

        let expr = self.parse_expr();

        if expr.is_none() {
            panic!("lambda body must be expression");
        }

        lambda.add_child(arrow.unwrap());
        lambda.add_child(expr.unwrap());

        Some(lambda)
    }

    fn parse_tuple_lit(&mut self) -> Option<AST> {
        self.consume_blanks();

        let l_paren = self.parse_l_paren();

        if l_paren.is_none() {
            return None;
        }

        let first_expr = self.parse_expr();

        let mut tuple_lit = new_ast_node(TokenType::TupleLit);
        tuple_lit.add_child(l_paren.unwrap());

        self.consume_blanks();

        if first_expr.is_some() {
            let first_comma = self.parse_comma();

            if first_comma.is_none() {
                panic!("expected comma after first tuple element");
            }

            let second_expr = self.parse_expr();

            if second_expr.is_none() {
                panic!("expected 0 or at least 2 elements in tuple");
            }

            tuple_lit.add_child(first_expr.unwrap());
            tuple_lit.add_child(first_comma.unwrap());
            tuple_lit.add_child(second_expr.unwrap());

            self.consume_blanks();

            while let Some(comma) = self.parse_comma() {
                let expr = self.parse_expr();

                if expr.is_none() {
                    break;
                }

                tuple_lit.add_child(comma);
                tuple_lit.add_child(expr.unwrap());

                self.consume_blanks();
            }
        }

        let r_paren = self.parse_r_paren();

        if r_paren.is_none() {
            panic!("expected right paren to terminate tuple");
        }

        tuple_lit.add_child(r_paren.unwrap());

        Some(tuple_lit)
    }

    fn parse_list_lit(&mut self) -> Option<AST> {
        self.consume_blanks();

        let l_sq_bracket = self.parse_l_sq_bracket();

        if l_sq_bracket.is_none() {
            return None;
        }

        let first_expr = self.parse_expr();

        let mut list_lit = new_ast_node(TokenType::ListLit);
        list_lit.add_child(l_sq_bracket.unwrap());

        if first_expr.is_some() {
            list_lit.add_child(first_expr.unwrap());

            self.consume_blanks();

            while let Some(comma) = self.parse_comma() {
                let expr = self.parse_expr();

                if expr.is_none() {
                    break;
                }

                list_lit.add_child(comma);
                list_lit.add_child(expr.unwrap());

                self.consume_blanks();
            }
        }

        let r_sq_bracket = self.parse_r_sq_bracket();

        if r_sq_bracket.is_none() {
            panic!("left square bracket in list literal requires ]");
        }

        list_lit.add_child(r_sq_bracket.unwrap());

        Some(list_lit)
    }

    fn parse_list_comp(&mut self) -> Option<AST> {
        self.consume_blanks();

        let l_sq_bracket = self.parse_l_sq_bracket();

        if l_sq_bracket.is_none() {
            return None;
        }

        let expr = self.parse_expr().expect("expected expression on left-hand side of list comprehension");

        let bar_ = self.parse_bar().expect("expected | for list comprehension");

        let mut list_comp = new_ast_node(TokenType::ListComp);
        list_comp.add_child(l_sq_bracket.unwrap());
        list_comp.add_child(expr);
        list_comp.add_child(bar_);

        let first_generator = self.parse_generator();
        let first_condition = if first_generator.is_none() {
            self.parse_expr()
        } else {
            None
        };

        if first_generator.is_some() || first_condition.is_some() {
            if first_generator.is_some() {
                list_comp.add_child(first_generator.unwrap());
            } else {
                list_comp.add_child(first_condition.unwrap());
            }

            self.consume_blanks();

            while let Some(_) = self.parse_comma() {
                if let Some(generator) = self.parse_generator() {
                    list_comp.add_child(generator);
                } else if let Some(condition) = self.parse_expr() {
                    list_comp.add_child(condition);
                } else {
                    break;
                }

                self.consume_blanks();
            }
        }

        let r_sq_bracket = self.parse_r_sq_bracket();

        if r_sq_bracket.is_none() {
            panic!("expected ] to terminate list comprehension");
        }

        list_comp.add_child(r_sq_bracket.unwrap());

        Some(list_comp)
    }

    fn parse_dict_lit(&mut self) -> Option<AST> {
        self.consume_blanks();

        let l_curly_bracket = self.parse_l_curly_bracket();

        if l_curly_bracket.is_none() {
            return None;
        }

        let first_entry = self.parse_dict_entry();

        let mut dict_lit = new_ast_node(TokenType::DictLit);
        dict_lit.add_child(l_curly_bracket.unwrap());

        if first_entry.is_some() {
            self.consume_blanks();

            while let Some(comma) = self.parse_comma() {
                let entry = self.parse_dict_entry();

                if entry.is_none() {
                    break;
                }

                dict_lit.add_child(comma);
                dict_lit.add_child(entry.unwrap());

                self.consume_blanks();
            }
        }

        let r_curly_bracket = self.parse_r_curly_bracket();

        if r_curly_bracket.is_none() {
            panic!("left curly bracket in dict literal requires }");
        }

        dict_lit.add_child(r_curly_bracket.unwrap());

        Some(dict_lit)
    }

    fn parse_dict_comp(&mut self) -> Option<AST> {
        self.consume_blanks();

        let l_curly_bracket = self.parse_l_curly_bracket();

        if l_curly_bracket.is_none() {
            return None;
        }

        let entry = self.parse_dict_entry().expect("expected entry on left-hand side of dict comprehension");

        let bar_ = self.parse_bar().expect("expected | for dict comprehension");

        let mut dict_comp = new_ast_node(TokenType::DictComp);
        dict_comp.add_child(l_curly_bracket.unwrap());
        dict_comp.add_child(entry);
        dict_comp.add_child(bar_);

        let first_generator = self.parse_generator();
        let first_condition = if first_generator.is_none() {
            self.parse_expr()
        } else {
            None
        };

        if first_generator.is_some() || first_condition.is_some() {
            if first_generator.is_some() {
                dict_comp.add_child(first_generator.unwrap());
            } else {
                dict_comp.add_child(first_condition.unwrap());
            }

            self.consume_blanks();

            while let Some(_) = self.parse_comma() {
                if let Some(generator) = self.parse_generator() {
                    dict_comp.add_child(generator);
                }
                else if let Some(condition) = self.parse_expr() {
                    dict_comp.add_child(condition);
                } else {
                    break;
                }

                self.consume_blanks();
            }
        }

        let r_curly_bracket = self.parse_r_curly_bracket();

        if r_curly_bracket.is_none() {
            panic!("expected } to terminate dict comprehension");
        }

        dict_comp.add_child(r_curly_bracket.unwrap());

        Some(dict_comp)
    }

    fn parse_set_lit(&mut self) -> Option<AST> {
        self.consume_blanks();

        let l_curly_bracket = self.parse_l_curly_bracket();

        if l_curly_bracket.is_none() {
            return None;
        }

        let first_expr = self.parse_expr();

        let mut set_lit = new_ast_node(TokenType::SetLit);
        set_lit.add_child(l_curly_bracket.unwrap());

        if first_expr.is_some() {
            self.consume_blanks();

            while let Some(comma) = self.parse_comma() {
                let expr = self.parse_expr();

                if expr.is_none() {
                    break;
                }

                set_lit.add_child(comma);
                set_lit.add_child(expr.unwrap());

                self.consume_blanks();
            }
        }

        let r_curly_bracket = self.parse_r_curly_bracket();

        if r_curly_bracket.is_none() {
            panic!("left curly bracket in set literal requires }");
        }

        set_lit.add_child(r_curly_bracket.unwrap());

        Some(set_lit)
    }

    fn parse_set_comp(&mut self) -> Option<AST> {
        self.consume_blanks();

        let l_curly_bracket = self.parse_l_curly_bracket();

        if l_curly_bracket.is_none() {
            return None;
        }

        let expr = self.parse_expr().expect("expected expression on left-hand side of set comprehension");

        let bar_ = self.parse_bar().expect("expected | for set comprehension");

        let mut set_comp = new_ast_node(TokenType::SetComp);
        set_comp.add_child(l_curly_bracket.unwrap());
        set_comp.add_child(expr);
        set_comp.add_child(bar_);

        let first_generator = self.parse_generator();
        let first_condition = if first_generator.is_none() {
            self.parse_expr()
        } else {
            None
        };

        if first_generator.is_some() || first_condition.is_some() {
            if first_generator.is_some() {
                set_comp.add_child(first_generator.unwrap());
            } else {
                set_comp.add_child(first_condition.unwrap());
            }

            self.consume_blanks();

            while let Some(_) = self.parse_comma() {
                if let Some(generator) = self.parse_generator() {
                    set_comp.add_child(generator);
                } else if let Some(condition) = self.parse_expr() {
                    set_comp.add_child(condition);
                } else {
                    break;
                }

                self.consume_blanks();
            }
        }

        let r_curly_bracket = self.parse_r_curly_bracket();

        if r_curly_bracket.is_none() {
            panic!("expected } to terminate set comprehension");
        }

        set_comp.add_child(r_curly_bracket.unwrap());

        Some(set_comp)
    }

    fn parse_qual_ident(&mut self) -> Option<AST> {
        self.consume_blanks();

        if let Some(member_ident) = self.parse_member_ident() {
            let mut qual_ident = new_ast_node(TokenType::QualIdent);
            qual_ident.add_child(member_ident);

            return Some(qual_ident);
        }

        if let Some(scoped_ident) = self.parse_scoped_ident() {
            let mut qual_ident = new_ast_node(TokenType::QualIdent);
            qual_ident.add_child(scoped_ident);

            return Some(qual_ident);
        }

        if let Some(ident) = self.parse_ident() {
            let mut qual_ident = new_ast_node(TokenType::QualIdent);
            qual_ident.add_child(ident);

            return Some(qual_ident);
        }

        None
    }

    fn parse_namespaced_ident(&mut self) -> Option<AST> {
        self.consume_blanks();

        if let Some(scoped_ident) = self.parse_scoped_ident() {
            let mut namespaced_ident = new_ast_node(TokenType::NamespacedIdent);
            namespaced_ident.add_child(scoped_ident);

            return Some(namespaced_ident);
        }

        if let Some(ident) = self.parse_ident() {
            let mut namespaced_ident = new_ast_node(TokenType::NamespacedIdent);
            namespaced_ident.add_child(ident);

            return Some(namespaced_ident);
        }

        None
    }

    fn parse_ident(&mut self) -> Option<AST> {
        self.consume_blanks();

        if self.ch != '_' && !self.ch.is_alphabetic() {
            return None;
        }

        let mut id = String::with_capacity(16);

        if self.ch == '_' {
            id.push('_');
            self.advance();

            if self.ch != '_' && !self.ch.is_alphanumeric() {
                self.charhistory.push_front(self.ch);
                self.ch = '_';

                return None;
            }
        }

        while self.ch == '_' || self.ch.is_alphanumeric() {
            id.push(self.ch);

            if self.advance() {
                break;
            }
        }

        Some(new_ast_leaf(TokenType::Ident, id))
    }

    fn parse_member_ident(&mut self) -> Option<AST> {
        let first_ident = match self.parse_ident() {
            Some(first_ident) => first_ident,
            _                 => return None,
        };

        let dot = self.parse_dot();

        if dot.is_none() {
            let mut first_ident_lex = first_ident.val().lexeme.clone();

            self.charhistory.push_front(self.ch);

            while first_ident_lex.len() > 1 {
                self.charhistory.push_front(first_ident_lex.pop().unwrap());
            }

            if let Some(c) = first_ident_lex.pop() {
                self.ch = c;
            }

            return None;
        }

        let second_ident = self.parse_ident();

        if second_ident.is_none() {
            panic!("expected identifier after dot operator");
        }

        let mut member_ident = new_ast_node(TokenType::MemberIdent);
        member_ident.add_child(first_ident);
        member_ident.add_child(dot.unwrap());
        member_ident.add_child(second_ident.unwrap());

        Some(member_ident)
    }

    fn parse_scoped_ident(&mut self) -> Option<AST> {
        let first_ident = match self.parse_ident() {
            Some(first_ident) => first_ident,
            _                 => return None,
        };

        let double_colon = self.parse_double_colon();

        if double_colon.is_none() {
            let mut first_ident_lex = first_ident.val().lexeme.clone();

            self.charhistory.push_front(self.ch);

            while first_ident_lex.len() > 1 {
                self.charhistory.push_front(first_ident_lex.pop().unwrap());
            }

            if let Some(c) = first_ident_lex.pop() {
                self.ch = c;
            }

            return None;
        }

        let second_ident = self.parse_ident();

        if second_ident.is_none() {
            panic!("expected identifier after dot operator");
        }

        let mut scoped_ident = new_ast_node(TokenType::ScopedIdent);
        scoped_ident.add_child(first_ident);
        scoped_ident.add_child(double_colon.unwrap());
        scoped_ident.add_child(second_ident.unwrap());

        Some(scoped_ident)
    }

    fn parse_type_ident(&mut self) -> Option<AST> {
        self.consume_blanks();

        if let Some(namespaced_ident) = self.parse_namespaced_ident() {
            let mut type_ident = new_ast_node(TokenType::TypeIdent);
            type_ident.add_child(namespaced_ident);

            return Some(type_ident);
        }

        if let Some(l_paren) = self.parse_l_paren() {
            let first_ident = self.parse_type_ident();

            let mut type_ident = new_ast_node(TokenType::TypeIdent);
            type_ident.add_child(l_paren);

            self.consume_blanks();

            if first_ident.is_some() {
                let first_comma = self.parse_comma();

                if first_comma.is_none() {
                    panic!("expected comma after first type tuple element");
                }

                let second_ident = self.parse_type_ident();

                if second_ident.is_none() {
                    panic!("expected 0 or at least 2 elements in type tuple");
                }

                type_ident.add_child(first_ident.unwrap());
                type_ident.add_child(first_comma.unwrap());
                type_ident.add_child(second_ident.unwrap());

                self.consume_blanks();

                while let Some(comma) = self.parse_comma() {
                    let ident = self.parse_type_ident();

                    if ident.is_none() {
                        break;
                    }

                    type_ident.add_child(comma);
                    type_ident.add_child(ident.unwrap());

                    self.consume_blanks();
                }
            }

            let r_paren = self.parse_r_paren();

            if r_paren.is_none() {
                panic!("expected right paren to terminate type tuple");
            }

            type_ident.add_child(r_paren.unwrap());

            return Some(type_ident);
        }

        if let Some(l_sq_bracket) = self.parse_l_sq_bracket() {
            let ident = self.parse_type_ident();

            if ident.is_none() {
                panic!("expected type identifier after [");
            }

            let r_sq_bracket = self.parse_r_sq_bracket();

            if r_sq_bracket.is_none() {
                panic!("expected closing ] of list type");
            }

            let mut type_ident = new_ast_node(TokenType::TypeIdent);
            type_ident.add_child(l_sq_bracket);
            type_ident.add_child(ident.unwrap());
            type_ident.add_child(r_sq_bracket.unwrap());

            return Some(type_ident);
        }

        if let Some(l_curly_bracket) = self.parse_l_curly_bracket() {
            let ident = self.parse_type_ident();

            if ident.is_none() {
                panic!("expected type identifier after {");
            }

            self.consume_blanks();

            let mut type_ident = new_ast_node(TokenType::TypeIdent);
            type_ident.add_child(l_curly_bracket);
            type_ident.add_child(ident.unwrap());

            let comma = self.parse_comma();

            if comma.is_some() {
                let second_ident = self.parse_type_ident();

                if second_ident.is_none() {
                    panic!("expected type identifier after ,");
                }

                type_ident.add_child(comma.unwrap());
                type_ident.add_child(second_ident.unwrap());
            }

            let r_curly_bracket = self.parse_r_curly_bracket();

            if r_curly_bracket.is_none() {
                panic!("expected closing } of dict/set type");
            }

            type_ident.add_child(r_curly_bracket.unwrap());

            Some(type_ident)
        } else {
            None
        }
    }

    fn parse_op(&mut self) -> Option<AST> {
        self.consume_blanks();

        let mut op = String::with_capacity(4);

        while let Some(op_char) = self.expect_char_op() {
            op.push(op_char);
        }

        if op.is_empty() {
            return None;
        }

        if is_reserved_op(&op) {
            panic!("the operator {} is reserved", op);
        }

        Some(new_ast_leaf(TokenType::Op, op))
    }

    fn parse_num_lit(&mut self) -> Option<AST> {
        self.consume_blanks();

        let mut minus = None;

        if self.expect_op("-") {
            minus = Some(new_ast_leaf(TokenType::Minus, "-"));

            self.consume_blanks();
        }

        if self.expect_keyword("NaN") {
            let mut num_lit = new_ast_node(TokenType::NumLit);
            let mut real_lit = new_ast_node(TokenType::RealLit);

            if minus.is_some() {
                real_lit.add_child(minus.unwrap());
            }

            real_lit.add_child(new_ast_leaf(TokenType::NanKeyword, "NaN"));
            num_lit.add_child(real_lit);

            return Some(num_lit);
        }

        if self.expect_keyword("Infinity") {
            let mut num_lit = new_ast_node(TokenType::NumLit);
            let mut real_lit = new_ast_node(TokenType::RealLit);

            if minus.is_some() {
                real_lit.add_child(minus.unwrap());
            }

            real_lit.add_child(new_ast_leaf(TokenType::InfinityKeyword, "Infinity"));
            num_lit.add_child(real_lit);

            return Some(num_lit);
        }

        if !self.ch.is_digit(10) {
            if minus.is_some() {
                //self.charhistory.push_front(' ');
                self.charhistory.push_front(self.ch);
                self.ch = '-';
            }

            return None;
        }

        let mut s = String::with_capacity(10);

        while self.ch.is_digit(10) {
            s.push(self.ch);

            if self.advance() {
                break;
            }
        }

        if self.ch != '.' {
            let mut num_lit = new_ast_node(TokenType::NumLit);
            let mut int_lit = new_ast_node(TokenType::IntLit);

            if minus.is_some() {
                int_lit.add_child(minus.unwrap());
            }

            int_lit.add_child(new_ast_leaf(TokenType::AbsInt, s));
            num_lit.add_child(int_lit);

            return Some(num_lit);
        }

        s.push(self.ch);
        self.advance();

        if !self.ch.is_digit(10) {
            panic!("expected at least one digit after decimal point");
        }

        while self.ch.is_digit(10) {
            s.push(self.ch);

            if self.advance() {
                break;
            }
        }

        let mut num_lit = new_ast_node(TokenType::NumLit);
        let mut real_lit = new_ast_node(TokenType::RealLit);

        if minus.is_some() {
            real_lit.add_child(minus.unwrap());
        }

        real_lit.add_child(new_ast_leaf(TokenType::AbsReal, s));
        num_lit.add_child(real_lit);

        Some(num_lit)
    }

    fn parse_chr_lit(&mut self) -> Option<AST> {
        self.consume_blanks();

        let init_single_quote = self.parse_single_quote();

        if init_single_quote.is_none() {
            return None;
        }

        let the_char = self.parse_chr_chr();

        if the_char.is_none() {
            panic!("unexpected ' or EOF");
        }

        let end_single_quote = self.parse_single_quote();

        if end_single_quote.is_none() {
            panic!("expected ', got: {}", self.ch);
        }

        let mut chr_lit = new_ast_node(TokenType::ChrLit);
        chr_lit.add_child(init_single_quote.unwrap());
        chr_lit.add_child(the_char.unwrap());
        chr_lit.add_child(end_single_quote.unwrap());

        Some(chr_lit)
    }

    fn parse_str_lit(&mut self) -> Option<AST> {
        self.consume_blanks();

        let mut str_lit = new_ast_node(TokenType::StrLit);

        let init_double_quote = self.parse_double_quote();

        if init_double_quote.is_none() {
            return None;
        }

        str_lit.add_child(init_double_quote.unwrap());

        while let Some(str_chr) = self.parse_str_chr() {
            str_lit.add_child(str_chr);
        }

        let end_double_quote = match self.parse_double_quote() {
            Some(double_quote) => double_quote,
            _                  => panic!("expected \", got: {}", self.ch),
        };

        str_lit.add_child(end_double_quote);

        Some(str_lit)
    }

    fn parse_infixed(&mut self) -> Option<AST> {
        self.consume_blanks();

        let first_backtick = self.parse_backtick();

        if first_backtick.is_none() {
            return None;
        }

        let ident = self.parse_qual_ident();

        if ident.is_none() {
            panic!("expected identifier after `");
        }

        let second_backtick = self.parse_backtick();

        if second_backtick.is_none() {
            panic!("expected closing `");
        }

        let mut infixed = new_ast_node(TokenType::Infixed);
        infixed.add_child(first_backtick.unwrap());
        infixed.add_child(ident.unwrap());
        infixed.add_child(second_backtick.unwrap());

        Some(infixed)
    }

    fn parse_pattern(&mut self) -> Option<AST> {
        self.consume_blanks();

        let mut pattern = new_ast_node(TokenType::Pattern);

        if let Some(ident) = self.parse_ident() {
            pattern.add_child(ident);

            return Some(pattern);
        }

        if let Some(chr_lit) = self.parse_chr_lit() {
            pattern.add_child(chr_lit);

            return Some(pattern);
        }

        if let Some(str_lit) = self.parse_str_lit() {
            pattern.add_child(str_lit);

            return Some(pattern);
        }

        if let Some(num_lit) = self.parse_num_lit() {
            pattern.add_child(num_lit);

            return Some(pattern);
        }

        if let Some(underscore) = self.parse_underscore() {
            pattern.add_child(underscore);

            return Some(pattern);
        }

        if let Some(l_paren) = self.parse_l_paren() {
            let first_pattern = self.parse_pattern();

            pattern.add_child(l_paren);

            if first_pattern.is_some() {
                let first_comma = self.parse_comma();

                if first_comma.is_none() {
                    panic!("expected comma after first element of pattern tuple");
                }

                let second_pattern = self.parse_pattern();

                if second_pattern.is_none() {
                    panic!("expected 0 or at least 2 elements in pattern tuple");
                }

                pattern.add_child(first_pattern.unwrap());
                pattern.add_child(first_comma.unwrap());
                pattern.add_child(second_pattern.unwrap());

                self.consume_blanks();

                while let Some(comma) = self.parse_comma() {
                    let unit = self.parse_pattern();

                    if unit.is_none() {
                        break;
                    }

                    pattern.add_child(comma);
                    pattern.add_child(unit.unwrap());

                    self.consume_blanks();
                }
            }

            let r_paren = self.parse_r_paren();

            if r_paren.is_none() {
                panic!("left paren in pattern requires )");
            }

            pattern.add_child(r_paren.unwrap());

            return Some(pattern);
        }

        if let Some(l_sq_bracket) = self.parse_l_sq_bracket() {
            let first_pattern = self.parse_pattern();

            pattern.add_child(l_sq_bracket);

            if first_pattern.is_some() {
                pattern.add_child(first_pattern.unwrap());

                self.consume_blanks();

                while let Some(comma) = self.parse_comma() {
                    let unit = self.parse_pattern();

                    if unit.is_none() {
                        break;
                    }

                    pattern.add_child(comma);
                    pattern.add_child(unit.unwrap());

                    self.consume_blanks();
                }
            }

            let r_sq_bracket = self.parse_r_sq_bracket();

            if r_sq_bracket.is_none() {
                panic!("left square bracket in pattern requires ]");
            }

            pattern.add_child(r_sq_bracket.unwrap());

            return Some(pattern);
        }

        if let Some(l_curly_bracket) = self.parse_l_curly_bracket() {
            let first_key = self.parse_pattern();

            pattern.add_child(l_curly_bracket);

            if first_key.is_some() {
                self.consume_blanks();

                let first_equals = self.parse_equals();

                if first_equals.is_none() {
                    pattern.add_child(first_key.unwrap());

                    self.consume_blanks();

                    while let Some(comma) = self.parse_comma() {
                        let unit = self.parse_pattern();

                        if unit.is_none() {
                            break;
                        }

                        pattern.add_child(comma);
                        pattern.add_child(unit.unwrap());

                        self.consume_blanks();
                    }
                } else {
                    let first_val = self.parse_pattern();

                    if first_val.is_none() {
                        panic!(
                            "expected value pattern after \
                            first = of dict pattern"
                        );
                    }

                    pattern.add_child(first_key.unwrap());
                    pattern.add_child(first_equals.unwrap());
                    pattern.add_child(first_val.unwrap());

                    self.consume_blanks();

                    while let Some(comma) = self.parse_comma() {
                        let key = self.parse_pattern();

                        if key.is_none() {
                            break;
                        }

                        self.consume_blanks();
                        let equals = self.parse_equals();

                        if equals.is_none() {
                            panic!("expected = after key of dict pattern");
                        }

                        let val = self.parse_pattern();

                        if val.is_none() {
                            panic!(
                                "expected value pattern after = \
                                of dict pattern"
                            );
                        }

                        pattern.add_child(comma);
                        pattern.add_child(key.unwrap());
                        pattern.add_child(equals.unwrap());
                        pattern.add_child(val.unwrap());

                        self.consume_blanks();
                    }
                }
            }

            let r_curly_bracket = self.parse_r_curly_bracket();

            if r_curly_bracket.is_none() {
                panic!("left curly bracket in pattern requires }");
            }

            pattern.add_child(r_curly_bracket.unwrap());

            Some(pattern)
        } else {
            None
        }
    }

    fn parse_chr_chr(&mut self) -> Option<AST> {
        if let Some(char_) = self.expect_char_not_chr_ctrl() {
            return Some(new_ast_leaf(TokenType::ChrChr, char_.to_string()));
        }

        if !self.expect_char('\\') {
            return None;
        }

        if let Some(esc_char) = self.expect_char_esc() {
            let mut escaped = String::with_capacity(2);
            escaped.push('\\');
            escaped.push(esc_char);

            Some(new_ast_leaf(TokenType::ChrChr, escaped))
        } else {
            None
        }
    }

    fn parse_str_chr(&mut self) -> Option<AST> {
        if let Some(char_) = self.expect_char_not_str_ctrl() {
            return Some(new_ast_leaf(TokenType::StrChr, char_.to_string()));
        }

        if !self.expect_char('\\') {
            return None;
        }

        if let Some(esc_char) = self.expect_char_esc() {
            let mut escaped = String::with_capacity(2);
            escaped.push('\\');
            escaped.push(esc_char);

            Some(new_ast_leaf(TokenType::StrChr, escaped))
        } else {
            None
        }
    }

    fn parse_param(&mut self) -> Option<AST> {
        self.consume_blanks();

        if self.ch == '(' {
            let l_paren = self.parse_l_paren();

            if l_paren.is_none() {
                panic!("should have successfully parsed left paren");
            }

            let pattern = self.parse_pattern();

            if pattern.is_none() {
                return None;
            }

            self.consume_blanks();
            let colon = self.parse_colon();

            if colon.is_none() {
                return None;
            }

            let type_ident = self.parse_type_ident();

            if type_ident.is_none() {
                panic!("expected type");
            }

            let r_paren = self.parse_r_paren();

            if r_paren.is_none() {
                panic!("expected ) after type");
            }

            let mut param = new_ast_node(TokenType::Param);
            param.add_child(l_paren.unwrap());
            param.add_child(pattern.unwrap());
            param.add_child(colon.unwrap());
            param.add_child(type_ident.unwrap());
            param.add_child(r_paren.unwrap());

            return Some(param);
        }

        let pattern = self.parse_pattern();

        if pattern.is_some() {
            let mut param = new_ast_node(TokenType::Param);
            param.add_child(pattern.unwrap());

            Some(param)
        } else {
            None
        }
    }

    fn parse_generator(&mut self) -> Option<AST> {
        let pattern = match self.parse_pattern() {
            Some(pattern) => pattern,
            _             => return None,
        };

        let l_arrow = self.parse_l_arrow();

        if l_arrow.is_none() {
            self.charhistory.push_front(self.ch);
            self.charhistory.push_front(' ');

            let mut consumed_pattern = str_repr(&pattern);

            while consumed_pattern.len() > 1 {
                self.charhistory.push_front(consumed_pattern.pop().unwrap());
            }

            if let Some(last_consumed_ch) = consumed_pattern.pop() {
                self.ch = last_consumed_ch;
            }

            return None;
        }

        let expr = self.parse_expr();

        if expr.is_none() {
            panic!("expected expression after <-");
        }

        let mut generator = new_ast_node(TokenType::Generator);
        generator.add_child(pattern);
        generator.add_child(l_arrow.unwrap());
        generator.add_child(expr.unwrap());

        Some(generator)
    }

    fn parse_dict_entry(&mut self) -> Option<AST> {
        self.consume_blanks();

        let key = self.parse_expr();

        if key.is_none() {
            return None;
        }

        self.consume_blanks();

        let equals = self.parse_equals();

        if equals.is_none() {
            return None;
        }

        let val = self.parse_expr();

        if val.is_none() {
            panic!("expected expression to be assigned to dict key");
        }

        let mut dict_entry = new_ast_node(TokenType::DictEntry);
        dict_entry.add_child(key.unwrap());
        dict_entry.add_child(equals.unwrap());
        dict_entry.add_child(val.unwrap());

        Some(dict_entry)
    }

    fn parse_equals(&mut self) -> Option<AST> {
        if !self.expect_char('=') {
            return None;
        }

        Some(new_ast_leaf(TokenType::Equals, "="))
    }

    fn parse_single_quote(&mut self) -> Option<AST> {
        if !self.expect_char('\'') {
            return None;
        }

        Some(new_ast_leaf(TokenType::SingleQuote, "'"))
    }

    fn parse_double_quote(&mut self) -> Option<AST> {
        if !self.expect_char('"') {
            return None;
        }

        Some(new_ast_leaf(TokenType::DoubleQuote, "\""))
    }

    fn parse_fn_keyword(&mut self) -> Option<AST> {
        if !self.expect_keyword("fn") {
            return None;
        }

        Some(new_ast_leaf(TokenType::FnKeyword, "fn"))
    }

    fn parse_case_keyword(&mut self) -> Option<AST> {
        if !self.expect_keyword("case") {
            return None;
        }

        Some(new_ast_leaf(TokenType::CaseKeyword, "case"))
    }

    fn parse_if_keyword(&mut self) -> Option<AST> {
        if !self.expect_keyword("if") {
            return None;
        }

        Some(new_ast_leaf(TokenType::IfKeyword, "if"))
    }

    fn parse_else_keyword(&mut self) -> Option<AST> {
        if !self.expect_keyword("else") {
            return None;
        }

        Some(new_ast_leaf(TokenType::ElseKeyword, "else"))
    }

    fn parse_try_keyword(&mut self) -> Option<AST> {
        if !self.expect_keyword("try") {
            return None;
        }

        Some(new_ast_leaf(TokenType::TryKeyword, "try"))
    }

    fn parse_catch_keyword(&mut self) -> Option<AST> {
        if !self.expect_keyword("catch") {
            return None;
        }

        Some(new_ast_leaf(TokenType::CatchKeyword, "catch"))
    }

    fn parse_while_keyword(&mut self) -> Option<AST> {
        if !self.expect_keyword("while") {
            return None;
        }

        Some(new_ast_leaf(TokenType::WhileKeyword, "while"))
    }

    fn parse_for_keyword(&mut self) -> Option<AST> {
        if !self.expect_keyword("for") {
            return None;
        }

        Some(new_ast_leaf(TokenType::ForKeyword, "for"))
    }

    fn parse_in_keyword(&mut self) -> Option<AST> {
        if !self.expect_keyword("in") {
            return None;
        }

        Some(new_ast_leaf(TokenType::InKeyword, "in"))
    }

    fn parse_var_keyword(&mut self) -> Option<AST> {
        if !self.expect_keyword("var") {
            return None;
        }

        Some(new_ast_leaf(TokenType::VarKeyword, "var"))
    }

    fn parse_module_keyword(&mut self) -> Option<AST> {
        if !self.expect_keyword("module") {
            return None;
        }

        Some(new_ast_leaf(TokenType::ModuleKeyword, "module"))
    }

    fn parse_exposing_keyword(&mut self) -> Option<AST> {
        if !self.expect_keyword("exposing") {
            return None;
        }

        Some(new_ast_leaf(TokenType::ExposingKeyword, "exposing"))
    }

    fn parse_hiding_keyword(&mut self) -> Option<AST> {
        if !self.expect_keyword("hiding") {
            return None;
        }

        Some(new_ast_leaf(TokenType::HidingKeyword, "hiding"))
    }

    fn parse_import_keyword(&mut self) -> Option<AST> {
        if !self.expect_keyword("import") {
            return None;
        }

        Some(new_ast_leaf(TokenType::ImportKeyword, "import"))
    }

    fn parse_as_keyword(&mut self) -> Option<AST> {
        if !self.expect_keyword("as") {
            return None;
        }

        Some(new_ast_leaf(TokenType::AsKeyword, "as"))
    }

    fn parse_return_keyword(&mut self) -> Option<AST> {
        if !self.expect_keyword("return") {
            return None;
        }

        Some(new_ast_leaf(TokenType::ReturnKeyword, "return"))
    }

    fn consume_line_comment_op(&mut self) -> bool {
        self.expect_op("--")
    }

    fn parse_dot(&mut self) -> Option<AST> {
        if !self.expect_op(".") {
            return None;
        }

        Some(new_ast_leaf(TokenType::Dot, "."))
    }

    fn parse_comma(&mut self) -> Option<AST> {
        if !self.expect_char(',') {
            return None;
        }

        Some(new_ast_leaf(TokenType::Comma, ","))
    }

    fn parse_colon(&mut self) -> Option<AST> {
        if !self.expect_op(":") {
            return None;
        }

        Some(new_ast_leaf(TokenType::Colon, ":"))
    }

    fn parse_double_colon(&mut self) -> Option<AST> {
        if !self.expect_op("::") {
            return None;
        }

        Some(new_ast_leaf(TokenType::DoubleColon, "::"))
    }

    fn parse_underscore(&mut self) -> Option<AST> {
        if !self.expect_keyword("_") {
            return None;
        }

        Some(new_ast_leaf(TokenType::Underscore, "_"))
    }

    fn parse_l_arrow(&mut self) -> Option<AST> {
        if !self.expect_op("<-") {
            return None;
        }

        Some(new_ast_leaf(TokenType::LArrow, "<-"))
    }

    fn parse_r_arrow(&mut self) -> Option<AST> {
        if !self.expect_op("->") {
            return None;
        }

        Some(new_ast_leaf(TokenType::RArrow, "->"))
    }

    fn parse_fat_r_arrow(&mut self) -> Option<AST> {
        if !self.expect_op("=>") {
            return None;
        }

        Some(new_ast_leaf(TokenType::FatRArrow, "=>"))
    }

    fn parse_l_paren(&mut self) -> Option<AST> {
        if !self.expect_char('(') {
            return None;
        }

        Some(new_ast_leaf(TokenType::LParen, "("))
    }

    fn parse_r_paren(&mut self) -> Option<AST> {
        if !self.expect_char(')') {
            return None;
        }

        Some(new_ast_leaf(TokenType::RParen, ")"))
    }

    fn parse_l_sq_bracket(&mut self) -> Option<AST> {
        if !self.expect_char('[') {
            return None;
        }

        Some(new_ast_leaf(TokenType::LSqBracket, "["))
    }

    fn parse_r_sq_bracket(&mut self) -> Option<AST> {
        if !self.expect_char(']') {
            return None;
        }

        Some(new_ast_leaf(TokenType::RSqBracket, "]"))
    }

    fn parse_l_curly_bracket(&mut self) -> Option<AST> {
        if !self.expect_char('{') {
            return None;
        }

        Some(new_ast_leaf(TokenType::LCurlyBracket, "{"))
    }

    fn parse_r_curly_bracket(&mut self) -> Option<AST> {
        if !self.expect_char('}') {
            return None;
        }

        Some(new_ast_leaf(TokenType::RCurlyBracket, "}"))
    }

    fn parse_backslash(&mut self) -> Option<AST> {
        if !self.expect_char('\\') {
            return None;
        }

        Some(new_ast_leaf(TokenType::Backslash, "\\"))
    }

    fn parse_bar(&mut self) -> Option<AST> {
        if !self.expect_char('|') {
            return None;
        }

        Some(new_ast_leaf(TokenType::Bar, "|"))
    }

    fn parse_backtick(&mut self) -> Option<AST> {
        if !self.expect_char('`') {
            return None;
        }

        Some(new_ast_leaf(TokenType::Backtick, "`"))
    }

    /// Returns `true` when the EOF is reached and `charhistory` is consumed,
    /// otherwise returns `false`.
    fn advance(&mut self) -> bool {
        if let Some(first_history) = self.charhistory.pop_front() {
            self.ch = first_history;

            self.charhistory.is_empty() && self.eof
        } else if let Some(Ok(temp_ch)) = self.charstream.next() {
            self.ch = temp_ch;

            false
        } else {
            self.eof = true;

            true
        }
    }

    fn consume_blanks(&mut self) -> bool {
        if !is_blank(self.ch) {
            return false;
        }

        while let Some(first_history) = self.charhistory.pop_front() {
            self.ch = first_history;

            if !is_blank(self.ch) {
                return true;
            }
        }

        while let Some(Ok(temp_ch)) = self.charstream.next() {
            self.ch = temp_ch;

            if !is_blank(self.ch) {
                return true;
            }
        }

        self.eof = true;

        true
    }

    fn expect_newline(&mut self) -> bool {
        self.consume_blanks();

        if !is_newline(self.ch) {
            return false;
        }

        while let Some(first_history) = self.charhistory.pop_front() {
            self.ch = first_history;

            if is_newline(self.ch) {
                self.currentindent.clear();
            } else if is_blank(self.ch) {
                self.currentindent.push(self.ch);
            } else {
                return true;
            }
        }

        while let Some(Ok(temp_ch)) = self.charstream.next() {
            self.ch = temp_ch;

            if is_newline(self.ch) {
                self.currentindent.clear();
            } else if is_blank(self.ch) {
                self.currentindent.push(self.ch);
            } else {
                return true;
            }
        }

        self.eof = true;

        if is_newline(self.ch) {
            self.currentindent.clear();
        }

        true
    }

    fn expect_char(&mut self, c: char) -> bool {
        if self.ch != c {
            return false;
        }

        self.advance();

        true
    }

    fn expect_char_not_chr_ctrl(&mut self) -> Option<char> {
        if self.ch == '\'' || self.ch == '\\' {
            return None;
        }

        let tmp = self.ch;
        self.advance();

        Some(tmp)
    }

    fn expect_char_not_str_ctrl(&mut self) -> Option<char> {
        if self.ch == '"' || self.ch == '\\' {
            return None;
        }

        let tmp = self.ch;
        self.advance();

        Some(tmp)
    }

    fn expect_char_esc(&mut self) -> Option<char> {
        if self.ch != '\'' &&
           self.ch != '"'  &&
           self.ch != 't'  &&
           self.ch != 'v'  &&
           self.ch != 'n'  &&
           self.ch != 'r'  &&
           self.ch != 'b'  &&
           self.ch != '0'
        {
            return None;
        }

        let tmp = self.ch;
        self.advance();

        Some(tmp)
    }

    fn expect_char_op(&mut self) -> Option<char> {
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
            return None;
        }

        let tmp = self.ch;
        self.advance();

        Some(tmp)
    }

    fn expect_keyword(&mut self, kwd: &str) -> bool {
        let mut kwd_iter = kwd.chars();

        if let Some(first_kwd_ch) = kwd_iter.next() {
            if self.ch != first_kwd_ch {
                return false;
            }
        } else {
            panic!("empty keyword");
        }

        let mut historic_stack = Vec::with_capacity(5);

        while let Some(&first_history) = self.charhistory.front() {
            if let Some(next_ch) = kwd_iter.next() {
                if first_history != next_ch {
                    while historic_stack.len() > 1 {
                        self.charhistory.push_front(historic_stack.pop().unwrap());
                    }

                    if let Some(historic_back) = historic_stack.pop() {
                        self.ch = historic_back;
                    }

                    return false;
                }

                historic_stack.push(self.ch);

                if let Some(first_history) = self.charhistory.pop_front() {
                    self.ch = first_history;
                }
            } else {
                let not_keyword = if self.charhistory.is_empty() {
                    let temp_ch = self.ch;

                    if let Some(Ok(temp_ch)) = self.charstream.next() {
                        self.ch = temp_ch;
                    } else {
                        self.eof = true;
                    }

                    let not_keyword_tmp = self.ch == '_' || self.ch.is_alphanumeric();

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

                    return false;
                }

                self.advance();

                return true;
            }
        }

        self.charhistory.push_back(self.ch);
        let mut history_pushbacks = 1usize;

        while let Some(next_ch) = kwd_iter.next() {
            if let Some(Ok(temp_ch)) = self.charstream.next() {
                self.ch = temp_ch;

                if self.ch != next_ch {
                    while historic_stack.len() > 1 {
                        self.charhistory.push_front(historic_stack.pop().unwrap());
                    }

                    if let Some(historic_back) = historic_stack.pop() {
                        self.ch = historic_back;
                    }

                    return false;
                }

                self.charhistory.push_back(self.ch);
                history_pushbacks += 1;
            } else {
                self.eof = true;

                break;
            }
        }

        if let Some(Ok(temp_ch)) = self.charstream.next() {
            self.ch = temp_ch;
        } else {
            self.eof = true;
        }

        if self.ch == '_' || self.ch.is_alphanumeric() {
            while historic_stack.len() > 1 {
                self.charhistory.push_front(historic_stack.pop().unwrap());
            }

            if let Some(historic_back) = historic_stack.pop() {
                self.ch = historic_back;
            }
        }

        for _ in 0..history_pushbacks {
            self.charhistory.pop_back();
        }

        kwd_iter.next().is_none()
    }

    fn expect_op(&mut self, op: &str) -> bool {
        let mut op_iter = op.chars();

        if let Some(first_char) = op_iter.next() {
            if self.ch != first_char {
                return false;
            }
        } else {
            panic!("empty operator");
        }

        let mut historic_stack = Vec::with_capacity(4);

        while let Some(&first_history) = self.charhistory.front() {
            if let Some(next_char) = op_iter.next() {
                if first_history != next_char {
                    while historic_stack.len() > 1 {
                        self.charhistory.push_front(historic_stack.pop().unwrap());
                    }

                    if let Some(historic_back) = historic_stack.pop() {
                        self.ch = historic_back;
                    }

                    return false;
                }

                historic_stack.push(self.ch);

                if let Some(first_history) = self.charhistory.pop_front() {
                    self.ch = first_history;
                }
            } else {
                let not_op = if self.charhistory.is_empty() {
                    let temp_ch = self.ch;

                    if let Some(Ok(temp_ch)) = self.charstream.next() {
                        self.ch = temp_ch;
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

                    return false;
                }

                self.advance();

                return true;
            }
        }

        self.charhistory.push_back(self.ch);
        let mut history_pushbacks = 1usize;

        while let Some(next_ch) = op_iter.next() {
            if let Some(Ok(temp_ch)) = self.charstream.next() {
                self.ch = temp_ch;

                if self.ch != next_ch {
                    while historic_stack.len() > 1 {
                        self.charhistory.push_front(historic_stack.pop().unwrap());
                    }

                    if let Some(historic_back) = historic_stack.pop() {
                        self.ch = historic_back;
                    }

                    return false;
                }

                self.charhistory.push_back(self.ch);
                history_pushbacks += 1;
            } else {
                self.eof = true;

                break;
            }
        }

        if let Some(Ok(temp_ch)) = self.charstream.next() {
            self.ch = temp_ch;
        } else {
            self.eof = true;
        }

        if is_op_char(self.ch) {
            while historic_stack.len() > 1 {
                self.charhistory.push_front(historic_stack.pop().unwrap());
            }

            if let Some(historic_back) = historic_stack.pop() {
                self.ch = historic_back;
            }
        }

        for _ in 0..history_pushbacks {
            self.charhistory.pop_back();
        }

        op_iter.next().is_none()
    }

    fn get_block(&mut self, main_ast: &mut AST, body_item_type: TokenType) -> String {
        let start_indent = self.currentindent.clone();

        if !self.expect_newline() {
            panic!("expected newline after header");
        }

        let block_indent = self.currentindent.clone();

        if start_indent.len() >= block_indent.len() ||
           !block_indent.starts_with(&start_indent)
        {
            panic!("improper indentation after header");
        }

        let first_item = match body_item_type {
            TokenType::Line       => self.parse_line(false),
            TokenType::CaseBranch => self.parse_case_branch(),
            _                     => panic!("unhandled body item type"),
        }.expect("expected at least one item in block");

        main_ast.add_child(first_item);

        if !self.expect_newline() {
            panic!("expected newline after first item of block");
        }

        while self.currentindent == block_indent {
            let item_opt = match body_item_type {
                TokenType::Line       => self.parse_line(false),
                TokenType::CaseBranch => self.parse_case_branch(),
                _                     => panic!("unhandled body item type"),
            };

            match item_opt {
                Some(item) => {
                    main_ast.add_child(item);

                    if !self.expect_newline() {
                        panic!("expected newline after block item");
                    }
                },
                _ => panic!("expected item in block"),
            }
        }

        start_indent
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

pub fn is_newline(c: char) -> bool {
    c == '\n' || c == '\r'
}

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
