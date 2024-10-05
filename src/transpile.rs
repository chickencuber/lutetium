use crate::ast::{AstType, BinaryOp, FunctionDeclaration, Program, UnaryOp};

pub fn transpile(program: AstType) -> String {
   return eval(program); 
}

pub fn eval(ast: AstType) -> String {
    return match ast {
        AstType::Program(v) => eval_program(*v),
        AstType::Char(v) => v,
        AstType::Number(v) => v,
        AstType::UnaryOp(v) => eval_unary_op(*v),
        AstType::BinaryOp(v) => eval_binary_op(*v),
        AstType::Ident(v) => v,
        AstType::FunctionDeclaration(v) => eval_function_declare(*v),
    }
}

fn eval_function_declare(func: FunctionDeclaration) -> String {
    return format!(
r#"
fn {} () {{
    {}
}}
"#, func.name, eval_body(func.body));
}

fn eval_binary_op(op: BinaryOp) -> String {
    return format!("({}{}{})", eval(op.left), op.op, eval(op.right));
}

fn eval_unary_op(op: UnaryOp) -> String {
    return format!("({}{})", op.op, eval(op.value));
}

fn eval_program(program: Program) -> String {
    return eval_body(program.body);
}

fn eval_body(vec: Vec<AstType>) -> String {
    let mut str = String::new();
    for i in vec {
        str.push_str(&eval(i));
        str.push('\n');
    }
    return str;
}

