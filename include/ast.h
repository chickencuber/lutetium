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
            String value;
        } str_lit;

        struct {
            Scope* scope; // needed for param names and type checking for that
            String name;
            Type* return_type;
            ParameterList params; // optional name this time
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
