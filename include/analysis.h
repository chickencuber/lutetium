#pragma once
#include "aids.h"
#include "ast.h"

typedef enum {
    SYMBOL_VALUE,
    SYMBOL_TYPE,
} SymbolKind;

typedef struct Symbol Symbol;
struct Symbol {
    SymbolKind kind;
    union {
        Type* type;
        ParameterList* params;
    } data;
    String mangled;
    void* compiler_data;
};
typedef HM(String, Symbol) Symbols;

struct Scope {
    Allocator* alloc;
    Scope* parent;
    Symbols symbols;
};

void analyze(AST*);
Symbol* get_symbol(Scope* scope, String name, Pos pos);
