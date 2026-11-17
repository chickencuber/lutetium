#pragma once
#include "ast.h"
#include "aids.h"

typedef struct Symbol Symbol;
struct Symbol {
    Type* type;
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
