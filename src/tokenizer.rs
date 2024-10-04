use json::{object, object::Object, JsonValue};
use regex::Regex;

#[derive(Clone, PartialEq, Eq)]
pub enum Token {
    MathOperator(char),
    BinaryOperator(String),
    OpenParen,
    ClosedParen,
    OpenBrace,
    ClosedBrace,
    Ident(String),
    VarDeclaration(bool),
    FunctionDeclaration,
    String(String),
    Char(String),
    Number(String),
    Dot,
    EOF,
}

impl Token {
    pub fn to_str(&self) -> &str {
        match self {
            Self::MathOperator(_) => "Math",
            Self::OpenParen => "(",
            Self::ClosedParen => ")",
            Self::Ident(_) => "Ident",
            Self::VarDeclaration(_) => "Var",
            Self::FunctionDeclaration => "Function",
            Self::String(_) => "String",
            Self::Char(_) => "Char",
            Self::Number(_) => "Number",
            Self::BinaryOperator(_) => "Binary",
            Self::OpenBrace => "{",
            Self::ClosedBrace => "}",
            Self::EOF => "EOF",
            Self::Dot => ".",
        }
    }
}

fn keyword(keyword: String) -> Token {
    let key = keyword.as_str();
    let function = Regex::new(r"f(?:un(?:c(?:tion)?)?|n)").unwrap();
    match key {
        key if function.is_match(key) => Token::FunctionDeclaration,
        "let" => Token::VarDeclaration(false),
        "const" => Token::VarDeclaration(true),
        _ => Token::Ident(keyword),
    }
}

fn parse_all_symbols(chars: &Vec<char>, i: &mut usize, symbols: &Object) -> Option<String> {
    let c: char;
    if let Some(v) = chars.get(*i) {
        c = *v;
    } else {
        return None;
    }
    let get: &JsonValue;
    if let Some(v) = symbols.get(c.to_string().as_str()) {
        get = v;
    } else if let Some(v) = symbols.get("default") {
        *i -= 1;
        get = v;
    } else {
        return None;
    }

    if let JsonValue::Object(o) = get {
        *i += 1;
        let result = parse_all_symbols(chars, i, o);
        if let Some(s) = result {
            return Some(s);
        } else {
            *i -= 1;
            if let Some(v) = symbols.get("default") {
                if let JsonValue::Object(o) = v {
                    *i += 1;
                    let result = parse_all_symbols(chars, i, o);
                    if let Some(v) = result {
                        *i -= 1;
                        return Some(v);
                    } else {
                        *i -= 2;
                        return None;
                    }
                } else if let JsonValue::String(s) = v {
                    *i -= 1;
                    return Some(s.to_string());
                } else if let JsonValue::Short(s) = v {
                    *i -= 1;
                    return Some(s.to_string());
                } else {
                    *i -= 1;
                    return None;
                }
            } else {
                *i -= 1;
                return None;
            }
        }
    } else if let JsonValue::String(s) = get {
        return Some(s.to_string());
    } else if let JsonValue::Short(s) = get {
        return Some(s.to_string());
    } else {
        return None;
    }
}

fn parse_symbol(chars: &Vec<char>, i: &mut usize, symbols: &Object) -> String {
    if let Some(v) = parse_all_symbols(chars, i, symbols) {
        return v;
    } else {
        //panic!("unknown symbol");
        return "unknown".to_string();
    }
}

pub fn tokenize(code: String) -> Vec<Token> {
    let mut tokens: Vec<Token> = Vec::new();
    let mut i = 0;
    let chars: Vec<char> = code.chars().collect();

    let temp = object! {
        //other
        "(": "(",
        ")": ")",
        "{": "{",
        "}": "}",
        ".": ".",
        //math
        "+": "+",
        "-": "-",
        "/": "/",
        "*": "*",
        "%": "%",
        //binary
        "~": "~",
        "^": "^",
        "&": "&",
        "|": "|",
        ">": {
            ">": ">>",
        },
        "<": {
            "<": "<<",
        },
        //string
        "\"": "\"",
        "'": "'",
    };

    let symbols = match temp {
        JsonValue::Object(o) => o,
        _ => {
            panic!("unknown error occurred")
        }
    };

    while i < chars.len() {
        let mut c = chars.get(i).unwrap();
        if c.is_whitespace() {
            //do nothing
        } else if c.is_numeric() {
            let mut num = String::from("");
            let mut dec = false;
            while c.is_numeric() || *c == '.' {
                if *c == '.' {
                    if dec {
                        panic!("unexpected character");
                    } else {
                        dec = true;
                    }
                }
                num.push(*c);
                i += 1;
                c = chars.get(i).unwrap();
            }
            i -= 1;
            tokens.push(Token::Number(num));
        } else if c.is_alphabetic() || *c == '_' {
            let mut str = String::from("");
            while c.is_alphabetic() || c.is_numeric() || *c == '_' {
                str.push(*c);
                i += 1;
                c = chars.get(i).unwrap();
            }
            i -= 1;
            tokens.push(keyword(str));
        } else if c.is_ascii_punctuation() {
            tokens.push(match parse_symbol(&chars, &mut i, &symbols).as_str() {
                //other
                "(" => Token::OpenParen,
                ")" => Token::ClosedParen,
                "{" => Token::OpenBrace,
                "}" => Token::ClosedBrace,
                //math
                "+" => Token::MathOperator('+'),
                "-" => Token::MathOperator('-'),
                "/" => Token::MathOperator('/'),
                "*" => Token::MathOperator('*'),
                "%" => Token::MathOperator('%'),
                //binary
                "~" => Token::BinaryOperator("~".to_string()),
                "^" => Token::BinaryOperator("^".to_string()),
                "&" => Token::BinaryOperator("&".to_string()),
                "|" => Token::BinaryOperator("|".to_string()),
                ">>" => Token::BinaryOperator(">>".to_string()),
                "<<" => Token::BinaryOperator("<<".to_string()),
                //string
                "\"" => {
                    let mut s = String::from("");
                    i += 1;
                    loop {
                        let c = chars.get(i).unwrap();
                        if *c == '\\' {
                            s.push('\\');
                            i+=1;
                            s.push(*chars.get(i).unwrap());
                        } else if *c == '"' {
                            i+=1;
                            break;
                        } else {
                            s.push(*c);
                        }
                        i+=1;
                    }
                    i-=1;
                    Token::String(s)
                },
                "'" => {
                    i+=1;
                    let c = chars.get(i).unwrap();
                    let mut ret = String::from("");
                    if *c ==  '\\' {
                        ret.push('\\');
                        i+=1;
                        ret.push(*chars.get(i).unwrap());
                    } else if *c == '\'' {
                        panic!("unexpected token");
                    } else {
                        ret.push(*c);
                    }
                    i+=1;
                    if *chars.get(i).unwrap() != '\'' {
                        panic!("expected \"'\"");
                    }
                    Token::Char(ret)
                }
                "." => {
                    Token::Dot
                }
                _ => {
                    panic!("unknown symbol")
                }
            });
        }
        i += 1;
    }
    tokens.push(Token::EOF);
    return tokens;
}
