use crate::tokenizer::Token;

#[derive(Debug, Clone)]
pub struct Program {
    pub body: Vec<AstType>,
}

#[derive(Debug, Clone)]
pub struct BinaryOp {
    pub left: AstType,
    pub op: String,
    pub right: AstType,
}

#[derive(Debug, Clone)]
pub struct UnaryOp {
    pub op: String,
    pub value: AstType,
}

#[derive(Debug, Clone)]
pub struct FunctionDeclaration {
    pub name: String,
    pub body: Vec<AstType>
}

#[derive(Debug, Clone)]
pub enum AstType {
    Program(Box<Program>),
    Number(String),
    BinaryOp(Box<BinaryOp>),
    UnaryOp(Box<UnaryOp>),
    Char(String),
    Ident(String),
    FunctionDeclaration(Box<FunctionDeclaration>),
}

pub struct Parser {
    ast: Option<AstType>,
    tokens: Vec<Token>,
}

impl Parser {
    pub fn new(tokens: Vec<Token>) -> Self {
        return Self { 
            tokens,
            ast: None,
        };
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
    pub fn get_ast(&mut self) -> AstType {
        if let Some(v) = &self.ast {
            return v.clone();
        }
        let mut program = Program { body: Vec::new() };
        while self.not_eof() {
            program.body.push(self.parse_stmt());
        }
        let r = AstType::Program(Box::new(program));
        self.ast = Some(r.clone());
        return r;
    }
    fn parse_stmt(&mut self) -> AstType {
        return match self.at() {
            Token::FunctionDeclaration => {
                self.eat();
                let name: String;
                if let Token::Ident(n) = self.eat() {
                    name = n;
                } else {
                    panic!("Ident expected");
                }
                if self.eat() != Token::OpenParen {
                    panic!("expected '('");
                }
                if self.eat() != Token::ClosedParen {
                    panic!("expected ')'");
                }
                if self.eat() != Token::OpenBrace {
                    panic!("expected '{{'");
                }
                let mut body:Vec<AstType> = Vec::new();
                loop {
                    if !self.not_eof() {
                        panic!("expected '}}'");
                    }
                    if self.at() == Token::ClosedBrace {
                        self.eat();
                        break;
                    }
                    body.push(self.parse_stmt());
                }
                return AstType::FunctionDeclaration(Box::new(FunctionDeclaration {
                    name,
                    body,
                }))
            }
            _ => self.parse_expr(),
        }
    }
    fn parse_expr(&mut self) -> AstType {
        return self.parse_add_binary_expr();
    }
    fn parse_add_binary_expr(&mut self) -> AstType {
        let left = self.parse_mult_binary_expr();
        return match self.at() {
            Token::MathOperator('+') => {
                self.eat();
                return AstType::BinaryOp(Box::new(BinaryOp {
                    left,
                    right: self.parse_mult_binary_expr(),
                    op: "+".to_string(),
                }));
            }
            Token::MathOperator('-') => {
                self.eat();
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
                self.eat();
                return AstType::BinaryOp(Box::new(BinaryOp {
                    left,
                    right: self.parse_unary_expr(),
                    op: "*".to_string(),
                }));
            }
            Token::MathOperator('/') => {
                self.eat();
                return AstType::BinaryOp(Box::new(BinaryOp {
                    left,
                    right: self.parse_unary_expr(),
                    op: "/".to_string(),
                }));
            }
            Token::MathOperator('%') => {
                self.eat();
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
                self.eat();
                return AstType::UnaryOp(Box::new(UnaryOp {op: "~".to_string(), value: self.parse_primary_expr()}));
            }
            Token::MathOperator('-') => {
                self.eat();
                return AstType::UnaryOp(Box::new(UnaryOp {op: "-".to_string(), value: self.parse_primary_expr()}));
            }
            _ => self.parse_primary_expr(),
        };
    }
    fn parse_primary_expr(&mut self) -> AstType {
        match self.at() {
            Token::Char(char) => {
                self.eat(); 
                return AstType::Char(char)
            }
            Token::Number(str) => {
                let _ = self.eat();
                return AstType::Number(str);
            }
            Token::OpenParen => {
                self.eat();
                let value = self.parse_expr();
                if self.eat() != Token::ClosedParen {
                   panic!("expected Closed Paren"); 
                }
                return value;
            }
            _ => panic!("unknown token"),
        }
    }
}
