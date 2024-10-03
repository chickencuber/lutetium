use std::i128::MIN;

use crate::tokenizer::Token;

pub struct Program {
    body: Vec<AstType>,
}

pub enum Number {
    UInt(String),
    Int(String),
    Float(String),
}

pub enum AstType {
    Program(Box<Program>),
    Number(Box<Number>),
    BinaryOp(Box<BinaryOp>),
    UnaryOp(Box<UnaryOp>),
}

pub struct Parser {
    tokens: Vec<Token>,
}

pub struct BinaryOp {
    left: AstType,
    op: String,
    right: AstType,
}

pub struct UnaryOp {
    op: String,
    value: AstType,
}

impl Parser {
    fn new(tokens: Vec<Token>) -> Self {
        return Self { tokens };
    }
    fn at(&self) -> Token {
        return self.tokens.get(0).unwrap_or(&Token::EOF).clone();
    }
    fn eat(&mut self) -> Token {
        return self.tokens.remove(0);
    }
    fn not_eof(&self) -> bool {
        return match self.at() {
            Token::EOF => false,
            _ => true,
        };
    }
    fn get_ast(&mut self) -> AstType {
        let mut program = Program { body: Vec::new() };
        while self.not_eof() {
            program.body.push(self.parse_stmt());
        }
        return AstType::Program(Box::new(program));
    }
    fn parse_stmt(&mut self) -> AstType {
        return self.parse_expr();
    }
    fn parse_expr(&mut self) -> AstType {
        return self.parse_add_binary_expr();
    }
    fn parse_add_binary_expr(&mut self) -> AstType {
        let left = self.parse_mult_binary_expr();
        return match self.at() {
            Token::MathOperator('+') => {
                let _ = self.eat();
                return AstType::BinaryOp(Box::new(BinaryOp {
                    left,
                    right: self.parse_mult_binary_expr(),
                    op: "+".to_string(),
                }));
            }
            Token::MathOperator('-') => {
                let _ = self.eat();
                return AstType::BinaryOp(Box::new(BinaryOp {
                    left,
                    right: self.parse_mult_binary_expr(),
                    op: "-".to_string(),
                }));
            }
            _ => left,
        };
    }
    fn parse_mult_binary_expr(&mut self) -> AstType {
        let left = self.parse_unary_expr();
        return match self.at() {
            Token::MathOperator('*') => {
                let _ = self.eat();
                return AstType::BinaryOp(Box::new(BinaryOp {
                    left,
                    right: self.parse_unary_expr(),
                    op: "*".to_string(),
                }));
            }
            Token::MathOperator('/') => {
                let _ = self.eat();
                return AstType::BinaryOp(Box::new(BinaryOp {
                    left,
                    right: self.parse_unary_expr(),
                    op: "/".to_string(),
                }));
            }
            Token::MathOperator('%') => {
                let _ = self.eat();
                return AstType::BinaryOp(Box::new(BinaryOp {
                    left,
                    right: self.parse_unary_expr(),
                    op: "%".to_string(),
                }));
            }
            _ => left,
        };
    }
    fn parse_unary_expr(&mut self) -> AstType {
        return match self.at() {
            Token::BinaryOperator(ref s) if s == "~" => {
                let _ = self.eat();
                return AstType::UnaryOp(Box::new(UnaryOp {op: "~".to_string(), value: self.parse_primary_expr()}));
            }
            Token::MathOperator('-') => {
                let _ = self.eat();
                return AstType::UnaryOp(Box::new(UnaryOp {op: "-".to_string(), value: self.parse_primary_expr()}));
            }
            _ => self.parse_primary_expr(),
        };
    }
    fn parse_primary_expr(&mut self) -> AstType {

    }
}
