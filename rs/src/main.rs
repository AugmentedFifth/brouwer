#![cfg_attr(feature="clippy", feature(plugin))]
#![cfg_attr(feature="clippy", plugin(clippy))]

#![deny(missing_docs)]

#![feature(collection_placement)]
#![feature(io)]
#![feature(placement_in_syntax)]

//! Parser (and bytecode compiler/interpreter) for the brouwer language.

mod parser;
mod token;
mod tree;

use parser::{Parser, log_depth_first};

use std::env;
use std::process;


fn main() {
    if let Some(filename) = env::args().nth(1) {
        let mut parser = match Parser::new(filename) {
            Ok(parser) => parser,
            Err(e) => {
                eprintln!("{}", e);

                process::exit(1);
            },
        };

        match parser.parse() {
            Ok(Some(ast)) => {
                log_depth_first(&ast, 0);
                println!();
            },
            Ok(_) => {
                eprintln!("Parse failed!");

                process::exit(2);
            },
            Err(e) => {
                eprintln!("Parser error:\n    {}", e);

                process::exit(1);
            },
        }
    } else {
        eprintln!("Please provide the source file.");

        process::exit(1);
    }
}
