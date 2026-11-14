#include "compile.h"
#include "ast.h"

#include <libgccjit.h>

typedef enum {
    SYMBOL_VARIABLE,
    SYMBOL_PARAM,
    SYMBOL_FUNCTION,
} SymbolKind;

typedef struct Symbol Symbol;
struct Symbol {
    SymbolKind kind;
    Type type; // currently doesn't mean anything for functions, will add a type
               // for functions in the future
    union {
        gcc_jit_lvalue* variable;
        gcc_jit_param* param;
        gcc_jit_function* function;
    } data;
};
typedef HM(String, Symbol) SymbolList;
typedef struct Scope Scope;
typedef DA(Scope*) ScopeList;
struct Scope {
    Allocator* alloc;
    Scope* parent;
    SymbolList symbols;
    ScopeList scopes;
};

Symbol* add_symbol(Scope* scope, String name, SymbolKind kind, Type type);

Symbol* get_symbol(Scope* scope, String name);

Scope* pushScope(Scope* scope);

// ============================================================
// Compiler state
// ============================================================

struct Compiler {
    Allocator* alloc;
    Scope* scope;

    gcc_jit_context* ctx;
    gcc_jit_result* result;

    const char* error;
};

// ============================================================
// Error handling
// ============================================================

static void compiler_set_error(Compiler* compiler, const char* error) {
    if (!compiler->error)
        compiler->error = error;
}

static void compiler_set_jit_error(Compiler* compiler) {
    const char* error = gcc_jit_context_get_first_error(compiler->ctx);

    compiler_set_error(
        compiler, error ? error : "libgccjit encountered an unknown error");
}

// ============================================================
// Type compilation
// ============================================================

static gcc_jit_type* compile_type(Compiler* compiler, Type type) {
    if (type.kind == TYPE_VOID)
        return gcc_jit_context_get_type(compiler->ctx, GCC_JIT_TYPE_VOID);
    if (type.kind == TYPE_I8)
        return gcc_jit_context_get_type(compiler->ctx, GCC_JIT_TYPE_INT8_T);
    if (type.kind == TYPE_I16)
        return gcc_jit_context_get_type(compiler->ctx, GCC_JIT_TYPE_INT16_T);
    if (type.kind == TYPE_I32)
        return gcc_jit_context_get_type(compiler->ctx, GCC_JIT_TYPE_INT32_T);
    if (type.kind == TYPE_I64)
        return gcc_jit_context_get_type(compiler->ctx, GCC_JIT_TYPE_INT64_T);
    if (type.kind == TYPE_U8)
        return gcc_jit_context_get_type(compiler->ctx, GCC_JIT_TYPE_UINT8_T);
    if (type.kind == TYPE_U16)
        return gcc_jit_context_get_type(compiler->ctx, GCC_JIT_TYPE_UINT16_T);
    if (type.kind == TYPE_U32)
        return gcc_jit_context_get_type(compiler->ctx, GCC_JIT_TYPE_UINT32_T);
    if (type.kind == TYPE_U64)
        return gcc_jit_context_get_type(compiler->ctx, GCC_JIT_TYPE_UINT64_T);
    if (type.kind == TYPE_BOOL)
        return gcc_jit_context_get_type(compiler->ctx, GCC_JIT_TYPE_BOOL);
    if (type.kind == TYPE_CSTR)
        return gcc_jit_context_get_type(compiler->ctx,
                                        GCC_JIT_TYPE_CONST_CHAR_PTR);

    compiler_set_error(compiler, "unknown type");
    return NULL;
}

// ============================================================
// Expression compilation
// ============================================================

static gcc_jit_rvalue* compile_expression(Compiler* compiler, AST* ast,
                                          Scope* scope) {
    if (!ast) {
        compiler_set_error(compiler, "expected expression");
        return NULL;
    }

    switch (ast->kind) {
    case AST_INT_LITERAL: {
        gcc_jit_type* type =
            gcc_jit_context_get_type(compiler->ctx, GCC_JIT_TYPE_INT);

        return gcc_jit_context_new_rvalue_from_int(
            compiler->ctx, type, (int)ast->data.int_literal.value);
    }
    case AST_IDENT: {
        Symbol* symbol = get_symbol(scope, ast->data.ident.name);

        if (!symbol) {
            compiler_set_error(compiler, "undefined identifier");
            return NULL;
        }

        switch (symbol->kind) {
        case SYMBOL_PARAM:
            return gcc_jit_param_as_rvalue(symbol->data.param);

        case SYMBOL_VARIABLE:
            return gcc_jit_lvalue_as_rvalue(symbol->data.variable);
        default:
            unreachable();
        }

        return NULL;
    }
    case AST_FUNCTION_CALL: {
        Symbol* symbol = get_symbol(scope, ast->data.function_call.name);

        if (!symbol) {
            compiler_set_error(compiler, "undefined function");
            return NULL;
        }

        if (symbol->kind != SYMBOL_FUNCTION) {
            compiler_set_error(compiler, "called object is not a function");
            return NULL;
        }

        ASTList* args = &ast->data.function_call.args;

        gcc_jit_rvalue** jit_args = NULL;

        if (args->len > 0) {
            jit_args = ALLOC(compiler->alloc, sizeof(*jit_args) * args->len);

            da_foreach_enum(i, arg, args) {
                jit_args[i] = compile_expression(compiler, *arg, scope);

                if (!jit_args[i]) {
                    FREE(compiler->alloc, jit_args);
                    return NULL;
                }
            }
        }

        gcc_jit_rvalue* call =
            gcc_jit_context_new_call(compiler->ctx, NULL, symbol->data.function,
                                     (int)args->len, jit_args);

        FREE(compiler->alloc, jit_args);

        if (!call)
            compiler_set_jit_error(compiler);

        return call;
    }
    case AST_BOOL_LIT: {
        gcc_jit_type* type =
            gcc_jit_context_get_type(compiler->ctx, GCC_JIT_TYPE_BOOL);
        return gcc_jit_context_new_rvalue_from_int(
            compiler->ctx, type, (int)ast->data.bool_lit.value);
    }
    case AST_STR_LITERAL: {
        char* value = from_string(&ast->data.str_lit.value, compiler->alloc);
        gcc_jit_rvalue* result =
            gcc_jit_context_new_string_literal(compiler->ctx, value);
        FREE(compiler->alloc, value);
        return result;
    }

    default:
        compiler_set_error(compiler, "unsupported expression");
        return NULL;
    }
}

// ============================================================
// Statement compilation
// ============================================================

// Returns true if the statement terminates its block.
static bool compile_statement(Compiler* compiler, gcc_jit_block* block,
                              AST* ast, Scope* scope);

static bool compile_block(Compiler* compiler, gcc_jit_block* block, AST* ast,
                          Scope* scope) {
    da_foreach(stmt, &ast->data.block.statements) {
        bool terminated = compile_statement(compiler, block, *stmt, scope);

        if (compiler->error || terminated)
            return terminated;
    }

    return false;
}

static bool compile_expr_stmt(Compiler* compiler, gcc_jit_block* block,
                              AST* ast, Scope* scope) {
    gcc_jit_rvalue* value =
        compile_expression(compiler, ast->data.expr_stmt.value, scope);

    if (!value)
        return false;

    gcc_jit_block_add_eval(block, NULL, value);
    return false;
}

static bool compile_return(Compiler* compiler, gcc_jit_block* block, AST* ast,
                           Scope* scope) {
    AST* value_ast = ast->data.return_stmt.value;

    // A return without a value.
    if (value_ast == NULL) {
        gcc_jit_block_end_with_void_return(block, NULL);
        return true;
    }

    gcc_jit_rvalue* value = compile_expression(compiler, value_ast, scope);

    if (!value)
        return false;

    gcc_jit_block_end_with_return(block, NULL, value);
    return true;
}

static bool compile_statement(Compiler* compiler, gcc_jit_block* block,
                              AST* ast, Scope* scope) {
    switch (ast->kind) {
    case AST_RETURN:
        return compile_return(compiler, block, ast, scope);
    case AST_BLOCK:
        return compile_block(compiler, block, ast, scope);
    case AST_EXPR_STMT:
        return compile_expr_stmt(compiler, block, ast, scope);

    default:
        compiler_set_error(compiler, "unsupported statement");
        return false;
    }
}

static gcc_jit_param* compile_parameter(Compiler* compiler, Parameter* param,
                                        size_t index) {
    gcc_jit_type* type = compile_type(compiler, param->type);

    if (!type)
        return NULL;

    char generated_name[32];
    char* name = NULL;

    if (param->name.data) {
        name = from_string(&param->name, compiler->alloc);
    } else {
        snprintf(generated_name, sizeof(generated_name), "arg%zu", index);
        name = generated_name;
    }

    gcc_jit_param* jit_param =
        gcc_jit_context_new_param(compiler->ctx, NULL, type, name);

    if (param->name.data)
        FREE(compiler->alloc, name);

    if (!jit_param)
        compiler_set_jit_error(compiler);

    return jit_param;
}

// ============================================================
// Declaration compilation
// ============================================================

static void compile_extern_function(Compiler* compiler, AST* ast,
                                    Scope* scope) {
    Type return_type = ast->data.function_decl.return_type;
    ParameterList* params = &ast->data.function_decl.params;

    gcc_jit_type* jit_return_type = compile_type(compiler, return_type);
    if (!jit_return_type)
        return;

    gcc_jit_param** jit_params = NULL;

    if (params->len > 0) {
        jit_params = ALLOC(compiler->alloc, sizeof(*jit_params) * params->len);

        da_foreach_enum(i, param, params) {
            jit_params[i] = compile_parameter(compiler, param, i);

            if (!jit_params[i]) {
                FREE(compiler->alloc, jit_params);
                return;
            }
        }
    }

    char* name = from_string(&ast->data.function_decl.name, compiler->alloc);

    gcc_jit_function* function = gcc_jit_context_new_function(
        compiler->ctx, NULL, GCC_JIT_FUNCTION_IMPORTED, jit_return_type, name,
        (int)params->len, jit_params, 0);

    FREE(compiler->alloc, name);
    FREE(compiler->alloc, jit_params);

    if (!function) {
        compiler_set_jit_error(compiler);
        return;
    }

    Symbol* symbol = add_symbol(scope, ast->data.function_decl.name,
                                SYMBOL_FUNCTION, return_type);

    symbol->data.function = function;
}

static void compile_function(Compiler* compiler, AST* ast, Scope* scope) {
    gcc_jit_type* return_type =
        compile_type(compiler, ast->data.function.return_type);

    if (!return_type)
        return;

    ParameterList* params = &ast->data.function.params;

    gcc_jit_param** jit_params = NULL;

    if (params->len > 0) {
        jit_params =
            ALLOC(compiler->alloc, sizeof(gcc_jit_param*) * params->len);

        for (size_t i = 0; i < params->len; i++) {
            jit_params[i] = compile_parameter(compiler, &params->data[i], i);

            if (!jit_params[i]) {
                FREE(compiler->alloc, jit_params);
                return;
            }
        }
    }

    char* name = from_string(&ast->data.function.name, compiler->alloc);
    // TASK(20261113-162351-368-n6-6e930705): add a type for functions
    Symbol* symbol =
        add_symbol(scope, ast->data.function.name, SYMBOL_FUNCTION, (Type){});

    gcc_jit_function* function = gcc_jit_context_new_function(
        compiler->ctx, NULL, GCC_JIT_FUNCTION_EXPORTED, return_type, name,
        (int)params->len, jit_params, 0);
    symbol->data.function = function;

    FREE(compiler->alloc, name);

    if (!function) {
        compiler_set_jit_error(compiler);
        return;
    }

    gcc_jit_block* entry = gcc_jit_function_new_block(function, "entry");

    if (!entry) {
        compiler_set_jit_error(compiler);
        return;
    }

    Scope* newScope = pushScope(scope);

    da_foreach_enum(i, param, params) {
        Symbol* paramSymbol =
            add_symbol(newScope, param->name, SYMBOL_PARAM, param->type);

        paramSymbol->data.param = jit_params[i];
    }

    FREE(compiler->alloc, jit_params);
    compile_statement(compiler, entry, ast->data.function.body, newScope);
}

static void compile_declaration(Compiler* compiler, AST* ast, Scope* scope) {
    switch (ast->kind) {
    case AST_FUNCTION:
        compile_function(compiler, ast, scope);
        break;
    case AST_FUNCTION_DECL:
        compile_extern_function(compiler, ast, scope);
        break;

    default:
        compiler_set_error(compiler, "unsupported top-level declaration");
        break;
    }
}

// ============================================================
// Public API
// ============================================================

Scope* createScope(Allocator* alloc) {
    SymbolList symbols = {0};
    ScopeList scopes = {0};
    hm_init(&symbols, alloc);
    da_init(&scopes, alloc);
    Scope* p = ALLOC(alloc, sizeof(Scope));
    *p = (Scope){
        .symbols = symbols,
        .scopes = scopes,
        .alloc = alloc,
        .parent = NULL,
    };
    return p;
}

// name does get cloned
Symbol* add_symbol(Scope* scope, String name, SymbolKind kind, Type type) {
    String key = string_clone(&name, scope->alloc);
    hm_set(&scope->symbols, key,
           ((Symbol){
               .kind = kind,
               .type = type,
           }));
    return hm_get(&scope->symbols, key);
}

// name does not get owned
// returns null if it doesn't exist
Symbol* get_symbol(Scope* scope, String name) {
    if (scope == NULL)
        return NULL;
    Symbol* has = hm_get(&scope->symbols, name);
    if (has == NULL)
        return get_symbol(scope->parent, name);
    return has;
}

void scope_free(Scope* scope) {
    if (scope == NULL) {
        return;
    }
    da_foreach(it, &scope->scopes) { scope_free(*it); }
    da_free(&scope->scopes);
    hm_free_call(&scope->symbols, bucks, { da_free(&bucks->key); });
    FREE(scope->alloc, scope);
}

Scope* pushScope(Scope* scope) {
    Scope* s = createScope(scope->alloc);
    s->parent = scope;
    da_append(&scope->scopes, s);
    return s;
}

Compiler* compiler_create(Allocator* alloc) {
    Compiler* compiler = CALLOC(alloc, 1, sizeof(Compiler));

    compiler->alloc = alloc;
    compiler->ctx = gcc_jit_context_acquire();
    compiler->scope = createScope(alloc);

    if (!compiler->ctx) {
        FREE(alloc, compiler);
        return NULL;
    }

    return compiler;
}

void compiler_free(Compiler* compiler) {
    if (!compiler)
        return;

    if (compiler->result)
        gcc_jit_result_release(compiler->result);

    if (compiler->ctx)
        gcc_jit_context_release(compiler->ctx);

    scope_free(compiler->scope);
    FREE(compiler->alloc, compiler);
}

bool compiler_compile(Compiler* compiler, AST* ast) {
    compiler->error = NULL;

    if (compiler->result) {
        gcc_jit_result_release(compiler->result);
        compiler->result = NULL;
    }

    if (!ast || ast->kind != AST_BLOCK) {
        compiler_set_error(compiler, "expected top-level block");
        return false;
    }

    // Compile declarations.
    da_foreach(node, &ast->data.block.statements) {
        compile_declaration(compiler, *node, compiler->scope);

        if (compiler->error)
            return false;
    }

    // Generate machine code.
    compiler->result = gcc_jit_context_compile(compiler->ctx);

    if (!compiler->result) {
        compiler_set_jit_error(compiler);
        return false;
    }

    return true;
}

const char* compiler_get_error(Compiler* compiler) {
    if (compiler->error)
        return compiler->error;

    return gcc_jit_context_get_first_error(compiler->ctx);
}

void* compiler_get_function(Compiler* compiler, const char* name) {
    if (!compiler->result)
        return NULL;

    return gcc_jit_result_get_code(compiler->result, name);
}
