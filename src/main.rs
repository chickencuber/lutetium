use std::{env, fs, path::PathBuf};
mod tokenizer;
use tokenizer::tokenize;
mod ast;
use ast::Parser;
mod transpile;
use transpile::transpile;
mod syntax_checker;
use syntax_checker::check_syntax;

use regex::Regex;

fn replace_last(text: &mut String, pattern: &str, replacement: &str) {
    let re = Regex::new(pattern).unwrap();

    // Find the last match
    let mut last_match = None;
    for mat in re.find_iter(text) {
        last_match = Some(mat);
    }

    // If there's a match, replace the last one
    if let Some(mat) = last_match {
        let (start, end) = (mat.start(), mat.end());
        text.replace_range(start..end, replacement);
    }
}


fn main() {
    let args: Vec<String> = env::args().collect();
    let mut dir = env::current_dir().unwrap();
    if let Some(file) = args.get(1) {
        dir.push(file);
        if let Some(path) = dir.to_str() {
            use_file(path);
        } else {
            println!("an unknown error occurred");
        }
    } else {
        println!("please provide an argument");
    }
}

fn use_file(file_path: &str) {
    if !file_path.ends_with(".lut") {
        panic!("file must end in '.lut'")
    }
    if let Ok(file) = fs::read_to_string(file_path) {
        let mut parser = Parser::new(tokenize(file));
        let ast = parser.get_ast();
        let mut new_path = PathBuf::from(file_path);
        let __name = new_path.file_name().expect("error");
        let _name = __name.to_str().unwrap(); 
        let mut name = _name.to_string();
        replace_last(&mut name, ".lut", ".rs");
        new_path.pop();
        new_path.push("build");
        if !new_path.exists() {
            fs::create_dir(&new_path).expect("error");
        }
        new_path.push(name);
        let _ = fs::write(new_path, transpile(ast));
    } else {
        println!("file doesn't exist or there was an error")
    }
}
