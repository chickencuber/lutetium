#pragma once
#include "aids.h"
#include "lexer.h"

// not defined until later
typedef struct Scope Scope;
void free_scope(Scope*);

#define TYPE_KIND(X)                                                           \
    X(".NULL", TYPE_NULL)                                                      \
    X(".INFER", TYPE_INFER)                                                    \
    X(".POINTER", TYPE_POINTER)                                                \
    X(".FUNCTION", TYPE_FUNCTION)                                              \
    X("void", TYPE_VOID)                                                       \
    X("i8", TYPE_I8)                                                           \
    X("i16", TYPE_I16)                                                         \
    X("i32", TYPE_I32)                                                         \
    X("i64", TYPE_I64)                                                         \
    X("u8", TYPE_U8)                                                           \
    X("u16", TYPE_U16)                                                         \
    X("u32", TYPE_U32)                                                         \
    X("u64", TYPE_U64)                                                         \
    X("cstr", TYPE_CSTR)                                                       \
    X("bool", TYPE_BOOL)

#define ENUM(_, t) t,
typedef enum TypeKind { TYPE_KIND(ENUM) } TypeKind;
#undef ENUM

// TOKEN, op, postfix
#define UNOP_KIND(X)                                                           \
    X(TOKEN_NULL, UP_NULL, false)                                              \
    X(TOKEN_EXC, UP_LNOT, false)

#define ENUM(_, value, __) value,
typedef enum { UNOP_KIND(ENUM) } UnOpKind;
#undef ENUM

typedef struct Unop Unop;
struct Unop {
    UnOpKind kind;
    bool postfix;
};
#define TOKEN_MAP(token, op, postfix) [token] = (Unop){op, postfix},
static const Unop token_to_unop[TOKEN_COUNT] = {UNOP_KIND(TOKEN_MAP)};
#undef TOKEN_MAP

#define TOKEN_MAP(token, op, _) [op] = token,
static const TokenKind unop_to_token[] = {UNOP_KIND(TOKEN_MAP)};
#undef TOKEN_MAP

#define BINOP_KIND(X)                                                          \
    X(TOKEN_NULL, OP_NULL)                                                     \
    X(TOKEN_PLUS, OP_ADD)                                                      \
    X(TOKEN_SLASH, OP_DIV)                                                     \
    X(TOKEN_PER, OP_MOD)                                                       \
    X(TOKEN_MINUS, OP_SUB)                                                     \
    X(TOKEN_SHL, OP_SHL)                                                       \
    X(TOKEN_SHR, OP_SHR)                                                       \
    X(TOKEN_LT, OP_LT)                                                         \
    X(TOKEN_GT, OP_GT)                                                         \
    X(TOKEN_LE, OP_LE)                                                         \
    X(TOKEN_GE, OP_GE)                                                         \
    X(TOKEN_NE, OP_NE)                                                         \
    X(TOKEN_EQ, OP_EQ)                                                         \
    X(TOKEN_STAR, OP_MUL)                                                      \
    X(TOKEN_BAND, OP_BAND)                                                     \
    X(TOKEN_PIPE, OP_BOR)                                                      \
    X(TOKEN_CARET, OP_XOR)                                                     \
    X(TOKEN_LAND, OP_LAND)                                                     \
    X(TOKEN_OR, OP_LOR)                                                        \
    X(TOKEN_EQUALS, OP_ASSIGN)

#define ENUM(_, value) value,
typedef enum { BINOP_KIND(ENUM) } BinOpKind;
#undef ENUM

#define OPERATORS(...)                                                         \
    (BinOpKind[]) { __VA_ARGS__ __VA_OPT__(, ) OP_NULL }

#define PRECEDENCE_COUNT (int)(sizeof(PRECEDENCE) / sizeof(BinOpKind*))
// took this idea for operator precedence from https://github.com/bext-lang/b
static const BinOpKind* PRECEDENCE[] = {
    OPERATORS(OP_MUL, OP_MOD, OP_DIV),
    OPERATORS(OP_ADD, OP_SUB),
    OPERATORS(OP_SHL, OP_SHR),
    OPERATORS(OP_LT, OP_GT, OP_GE, OP_LE),
    OPERATORS(OP_NE, OP_EQ),
    OPERATORS(OP_BAND),
    OPERATORS(OP_XOR),
    OPERATORS(OP_BOR),
    OPERATORS(OP_LAND),
    OPERATORS(OP_LOR),
    OPERATORS(OP_ASSIGN),
};

#define TOKEN_MAP(token, op) [token] = op,
static const BinOpKind token_to_binop[TOKEN_COUNT] = {BINOP_KIND(TOKEN_MAP)};
#undef TOKEN_MAP

#define TOKEN_MAP(token, op) [op] = token,
static const TokenKind binop_to_token[] = {BINOP_KIND(TOKEN_MAP)};
#undef TOKEN_MAP

#define FN_CONV_KIND(X)                                                        \
    X("lut", CONV_LUT)                                                         \
    X("C", CONV_C)

#define ENUM(_, value) value,
typedef enum { FN_CONV_KIND(ENUM) } FnConv;
#undef ENUM

typedef struct Type Type;
typedef struct Parameter Parameter;
struct Parameter {
    String name;
    Type* type;
};
typedef DA(Parameter) ParameterList;
struct Type {
    Allocator* alloc;
    TypeKind kind;
    union {
        struct {
            Type* child;
        } pointer;
        struct {
            Type* return_type;
            ParameterList params;
            bool va_args;
            FnConv conv;
        } function;
    } data;
};

#define CONV "%s"
#define FMTCONV(p) FN_CONV_NAMES[p]

#define ENUM(k, _) k,
static const char* TYPEKIND[] = {TYPE_KIND(ENUM)};
#undef ENUM

typedef enum ASTKind {
    AST_FUNCTION,
    AST_FUNCTION_DECL,
    AST_RETURN,
    AST_INT_LITERAL,
    AST_STR_LITERAL,
    AST_BOOL_LIT,
    AST_EXPR_STMT,
    AST_BLOCK,
    AST_IDENT,
    AST_FUNCTION_CALL,
    AST_VARIABLE_DECLARATION,
    AST_IF,
    AST_WHILE,
    AST_BINOP,
    AST_UNOP,
} ASTKind;

#define ENUM(name, _) name,
static const char* FN_CONV_NAMES[] = {FN_CONV_KIND(ENUM)};
#undef ENUM

typedef struct AST AST;
typedef DA(AST*) ASTList;
struct AST {
    Allocator* alloc;
    ASTKind kind;
    size_t line, col;
    Type* type; // filled during semantic analysis
    union {
        struct {
            AST* operand;
            UnOpKind op;
            bool postfix;
        } unop;
        struct {
            AST* lhs;
            AST* rhs;
            BinOpKind op;
        } binop;
        struct {
            String name;
            AST* val; // nulled out will be uninitialized
            Type* type;
        } var_decl;
        struct {
            Scope* scope; // needed for param names and type checking for that
            String name;
            AST* body;
            Type* return_type;
            ParameterList params;
        } function;
        struct {
            AST* cond;
            AST* then;
            AST* el;
        } if_stmt;
        struct {
            AST* cond;
            AST* body;
        } while_loop;
        struct {
            String value;
        } str_lit;

        struct {
            Scope* scope; // needed for param names and type checking for that
            String name;
            Type* return_type;
            ParameterList params; // optional name this time
            String symbol;
            FnConv conv;
            bool va_args;
        } function_decl;

        struct {
            AST* value;
        } return_stmt;
        struct {
            String name;
            ASTList args;
        } function_call;
        struct {
            AST* value;
        } expr_stmt;

        struct {
            u64 value;
        } int_literal;

        struct {
            bool value;
        } bool_lit;

        struct {
            String name;
        } ident;

        struct {
            Scope* scope;
            ASTList statements;
            bool toplevel;
        } block;
    } data;
};

// this will collect the tokens
AST* ast_parse_toplevel(Allocator*, Lexer*);

void type_free(Type*);
void type_display(Type*);
void ast_display(AST*);
void ast_free(AST*);
