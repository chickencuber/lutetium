#pragma once
#include "aids.h"
#include "lexer.h"

#define TYPE_KIND(X)                                                           \
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

typedef struct Type Type;
struct Type {
    TypeKind kind;
    size_t depth;
};
#define TYPE "%s%.*s"
#define FMTTYPE(p) TYPEKIND[(p)->kind], (int)(p)->depth, "****************"


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
} ASTKind;

typedef struct Parameter Parameter;
struct Parameter {
    String name;
    Type type;
};

#define FN_CONV_KIND(X) \
    X("lut", CONV_LUT)  \
    X("C", CONV_C)

#define ENUM(_, value) value,
typedef enum {
    FN_CONV_KIND(ENUM)
} FnConv;
#undef ENUM

#define ENUM(name, _) name,
static const char* FN_CONV_NAMES[] = {
    FN_CONV_KIND(ENUM)
};
#undef ENUM

typedef struct AST AST;
typedef DA(AST*) ASTList;
typedef DA(Parameter) ParameterList;
struct AST {
    Allocator* alloc;
    ASTKind kind;
    size_t line, col;
    union {
        struct {
            String name;
            AST* body;
            Type return_type;
            ParameterList params;
        } function;
        struct {
            String value;
        } str_lit;

        struct {
            String name;
            Type return_type;
            ParameterList params; //optional name this time
            FnConv conv;
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
            ASTList statements;
            bool toplevel;
        } block;
    } data;
};

// this will collect the tokens
AST* ast_parse_toplevel(Allocator*, Lexer*);

void ast_display(AST*);
void ast_free(AST*);
