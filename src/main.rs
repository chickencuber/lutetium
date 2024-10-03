use std::{env, fs};
mod tokenizer;
use tokenizer::tokenize;
mod ast;

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
    if let Ok(file) = fs::read_to_string(file_path) {
        for token in tokenize(file).iter() {
            println!("{}", token.to_str())
        }
    } else {
        println!("file doesn't exist or there was an error")
    }
}
