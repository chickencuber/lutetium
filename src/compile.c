#include "compile.h"
#include "aids.h"
#include "analysis.h"

#include <libgccjit.h>
#include <stdio.h>

/* ------------------------------------------------------------------------- */
/* Compiler                                                                  */
/* ------------------------------------------------------------------------- */

typedef struct Compiler Compiler;

struct Compiler {
    gcc_jit_context* ctx;
    Allocator* alloc;
};

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static char* compile_string(Compiler* c, String* string) {
    return from_string(string, c->alloc);
}

static Pos ast_pos(AST* ast) {
    return (Pos){
        .line = ast->line,
        .col = ast->col,
    };
}

/* ------------------------------------------------------------------------- */
/* Types                                                                     */
/* ------------------------------------------------------------------------- */

static gcc_jit_type* compile_type(Compiler* c, Type* type) {
    assert(type != NULL, "cannot compile a NULL type\n");

    switch (type->kind) {
    case TYPE_VOID:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_VOID);

    case TYPE_I8:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_INT8_T);
    case TYPE_I16:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_INT16_T);
    case TYPE_I32:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_INT32_T);
    case TYPE_I64:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_INT64_T);

    case TYPE_U8:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_UINT8_T);
    case TYPE_U16:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_UINT16_T);
    case TYPE_U32:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_UINT32_T);
    case TYPE_U64:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_UINT64_T);

    case TYPE_BOOL:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_BOOL);

    case TYPE_CSTR:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_CONST_CHAR_PTR);

    case TYPE_POINTER:
        return gcc_jit_type_get_pointer(
            compile_type(c, type->data.pointer.child));

    case TYPE_FUNCTION: {
        ParameterList* params = &type->data.function.params;
        gcc_jit_type** gcc_params = NULL;

        if (params->len > 0) {
            gcc_params = ALLOC(c->alloc, sizeof(gcc_jit_type*) * params->len);

            for (size_t i = 0; i < params->len; i++) {
                gcc_params[i] = compile_type(c, params->data[i].type);
            }
        }

        gcc_jit_type* result = gcc_jit_context_new_function_ptr_type(
            c->ctx, NULL, compile_type(c, type->data.function.return_type),
            (int)params->len, gcc_params, type->data.function.va_args);

        if (gcc_params != NULL)
            FREE(c->alloc, gcc_params);

        return result;
    }

    default:
        assert(false, "unsupported type in compile_type\n");
        return NULL;
    }
}

/* ------------------------------------------------------------------------- */
/* Functions                                                                 */
/* ------------------------------------------------------------------------- */

static gcc_jit_function* compile_function(
    Compiler* c,
    AST* ast,
    bool is_definition
) {
    String name;
    Type* return_type;
    ParameterList* params;
    Scope* scope;
    bool va_args = false;

    if (ast->kind == AST_FUNCTION) {
        name = ast->data.function.name;
        return_type = ast->data.function.return_type;
        params = &ast->data.function.params;
        scope = ast->data.function.scope;
    } else {
        name = ast->data.function_decl.name;
        return_type = ast->data.function_decl.return_type;
        params = &ast->data.function_decl.params;
        scope = ast->data.function_decl.scope;
        va_args = ast->data.function_decl.va_args;
    }

    assert(scope != NULL, "function has no scope\n");

    // Allow top-level file-scope declarations to have a NULL parent safely
    Scope* lookup_scope = (scope->parent != NULL) ? scope->parent : scope;

    Symbol* symbol = get_symbol(
        lookup_scope,
        name,
        ast_pos(ast)
    );

    assert(symbol != NULL, "function symbol not found\n");

    /* Already registered with libgccjit. */
    if (symbol->compiler_data != NULL)
        return symbol->compiler_data;

    gcc_jit_param** gcc_params = NULL;

    if (params->len > 0) {
        gcc_params = ALLOC(
            c->alloc,
            sizeof(gcc_jit_param*) * params->len
        );
    }

    for (size_t i = 0; i < params->len; i++) {
        Parameter* param = &params->data[i];

        char generated_name[32];
        char* allocated_name = NULL;
        const char* param_name;

        if (param->name.data == NULL) {
            snprintf(generated_name, sizeof(generated_name),
                     "arg%zu", i);
            param_name = generated_name;
        } else {
            allocated_name = compile_string(c, &param->name);
            param_name = allocated_name;
        }

        gcc_params[i] = gcc_jit_context_new_param(
            c->ctx,
            NULL,
            compile_type(c, param->type),
            param_name
        );

        if (allocated_name != NULL)
            FREE(c->alloc, allocated_name);
    }

    char* function_name = compile_string(c, &name);

    // Correctly creates IMPORTED (U) for declarations and EXPORTED (T) for definitions
    gcc_jit_function* function = gcc_jit_context_new_function(
        c->ctx,
        NULL,
        is_definition
            ? GCC_JIT_FUNCTION_EXPORTED
            : GCC_JIT_FUNCTION_IMPORTED,
        compile_type(c, return_type),
        function_name,
        (int)params->len,
        gcc_params,
        va_args
    );

    FREE(c->alloc, function_name);

    if (gcc_params != NULL)
        FREE(c->alloc, gcc_params);

    assert(function != NULL, "failed to create JIT function\n");

    symbol->compiler_data = function;

    return function;
}

/* ------------------------------------------------------------------------- */
/* Expressions                                                               */
/* ------------------------------------------------------------------------- */

static gcc_jit_rvalue* compile_expression(Compiler* c, AST* ast, Scope* scope) {
    assert(ast != NULL, "cannot compile NULL expression\n");

    switch (ast->kind) {
    case AST_INT_LITERAL:
        return gcc_jit_context_new_rvalue_from_long(
            c->ctx, compile_type(c, ast->type),
            (long)ast->data.int_literal.value);

    case AST_BOOL_LIT:
        return gcc_jit_context_new_rvalue_from_int(
            c->ctx, compile_type(c, ast->type), ast->data.bool_lit.value);

    case AST_STR_LITERAL: {
        char* value = compile_string(c, &ast->data.str_lit.value);

        gcc_jit_rvalue* result =
            gcc_jit_context_new_string_literal(c->ctx, value);

        FREE(c->alloc, value);
        return result;
    }

    case AST_IDENT: {
        Symbol* symbol = get_symbol(scope, ast->data.ident.name, ast_pos(ast));

        gcc_jit_lvalue* lvalue = symbol->compiler_data;

        assert(lvalue != NULL, "identifier has no compiled value\n");

        return gcc_jit_lvalue_as_rvalue(lvalue);
    }

    case AST_FUNCTION_CALL: {
        Symbol* symbol =
            get_symbol(scope, ast->data.function_call.name, ast_pos(ast));

        gcc_jit_function* function = symbol->compiler_data;

        assert(function != NULL, "function was not compiled\n");

        ASTList* args = &ast->data.function_call.args;
        gcc_jit_rvalue** gcc_args = NULL;

        if (args->len > 0) {
            gcc_args = ALLOC(c->alloc, sizeof(gcc_jit_rvalue*) * args->len);

            for (size_t i = 0; i < args->len; i++) {
                gcc_args[i] = compile_expression(c, args->data[i], scope);
            }
        }

        gcc_jit_rvalue* result = gcc_jit_context_new_call(
            c->ctx, NULL, function, (int)args->len, gcc_args);

        if (gcc_args != NULL)
            FREE(c->alloc, gcc_args);

        return result;
    }

    default:
        assert(false, "unsupported expression in compile_expression\n");
        return NULL;
    }
}

/* ------------------------------------------------------------------------- */
/* Globals                                                                   */
/* ------------------------------------------------------------------------- */

static void compile_global(Compiler* c, AST* ast, Scope* scope) {
    Type* type = ast->data.var_decl.type;
    String name = ast->data.var_decl.name;

    assert(type != NULL, "global variable has no type\n");

    char* cstr_name = compile_string(c, &name);

    gcc_jit_lvalue* global =
        gcc_jit_context_new_global(c->ctx, NULL, GCC_JIT_GLOBAL_EXPORTED,
                                   compile_type(c, type), cstr_name);

    FREE(c->alloc, cstr_name);

    assert(global != NULL, "failed to create global variable\n");

    Symbol* symbol = get_symbol(scope, name, ast_pos(ast));
    symbol->compiler_data = global;
}

static void initialize_global(Compiler* c, AST* ast, Scope* scope) {
    if (ast->data.var_decl.val == NULL)
        return;

    Symbol* symbol = get_symbol(scope, ast->data.var_decl.name, ast_pos(ast));

    gcc_jit_lvalue* global = symbol->compiler_data;

    assert(global != NULL, "global variable was not compiled\n");

    gcc_jit_global_set_initializer_rvalue(
        global, compile_expression(c, ast->data.var_decl.val, scope));
}

/* ------------------------------------------------------------------------- */
/* Statements                                                                */
/* ------------------------------------------------------------------------- */

static void compile_block(Compiler* c, gcc_jit_function* function,
                          gcc_jit_block* block, AST* ast);

static bool statement_ends_with_return(AST* ast) {
    if (ast->kind == AST_RETURN)
        return true;

    if (ast->kind != AST_BLOCK || ast->data.block.statements.len == 0)
        return false;

    AST* last =
        ast->data.block.statements.data[ast->data.block.statements.len - 1];

    return statement_ends_with_return(last);
}

static void compile_statement(Compiler* c, gcc_jit_function* function,
                             gcc_jit_block* block, AST* ast, Scope* scope) {
    switch (ast->kind) {
    case AST_BLOCK:
        compile_block(c, function, block, ast);
        break;

    case AST_FUNCTION:
    case AST_FUNCTION_DECL:
        break;

    case AST_RETURN:
        if (ast->data.return_stmt.value == NULL) {
            gcc_jit_block_end_with_void_return(block, NULL);
        } else {
            gcc_jit_block_end_with_return(
                block, NULL,
                compile_expression(c, ast->data.return_stmt.value, scope));
        }
        break;

    case AST_VARIABLE_DECLARATION: {
        Type* type = ast->data.var_decl.type;

        assert(type != NULL, "variable declaration has no type\n");

        char* name = compile_string(c, &ast->data.var_decl.name);

        gcc_jit_lvalue* local = gcc_jit_function_new_local(
            function, NULL, compile_type(c, type), name);

        FREE(c->alloc, name);

        assert(local != NULL, "failed to create local variable\n");

        Symbol* symbol =
            get_symbol(scope, ast->data.var_decl.name, ast_pos(ast));

        symbol->compiler_data = local;

        if (ast->data.var_decl.val != NULL) {
            gcc_jit_block_add_assignment(
                block, NULL, local,
                compile_expression(c, ast->data.var_decl.val, scope));
        }

        break;
    }

    case AST_EXPR_STMT: {
        gcc_jit_rvalue* value =
            compile_expression(c, ast->data.expr_stmt.value, scope);

        gcc_jit_block_add_eval(block, NULL, value);
        break;
    }

    default:
        assert(false, "unsupported statement in compile_statement\n");
        break;
    }
}

static void compile_block(Compiler* c, gcc_jit_function* function,
                          gcc_jit_block* block, AST* ast) {
    Scope* scope = ast->data.block.scope;

    da_foreach(statement, &ast->data.block.statements) {
        compile_statement(c, function, block, *statement, scope);
    }
}

/* ------------------------------------------------------------------------- */
/* Function bodies                                                           */
/* ------------------------------------------------------------------------- */

static void compile_function_body(Compiler* c, AST* ast) {
    // Pass true here since this is compiling an actual function definition body
    gcc_jit_function* function = compile_function(c, ast, true);
    gcc_jit_block* block = gcc_jit_function_new_block(function, NULL);

    assert(block != NULL, "failed to create function block\n");

    Scope* scope = ast->data.function.scope;
    ParameterList* params = &ast->data.function.params;

    for (size_t i = 0; i < params->len; i++) {
        Parameter* param = &params->data[i];

        Symbol* symbol = get_symbol(scope, param->name, ast_pos(ast));

        symbol->compiler_data = gcc_jit_param_as_lvalue(
            gcc_jit_function_get_param(function, (int)i));
    }

    AST* body = ast->data.function.body;

    if (body->kind == AST_BLOCK) {
        compile_block(c, function, block, body);
    } else {
        compile_statement(c, function, block, body, scope);
    }

    if (ast->data.function.return_type->kind == TYPE_VOID &&
        !statement_ends_with_return(body)) {
        gcc_jit_block_end_with_void_return(block, NULL);
    }
}

/* ------------------------------------------------------------------------- */
/* AST                                                                       */
/* ------------------------------------------------------------------------- */

static void compile_ast(Compiler* c, AST* ast) {
    assert(ast->kind == AST_BLOCK && ast->data.block.toplevel,
           "expected top level block\n");

    Scope* scope = ast->data.block.scope;

    /* Create all globals first. */
    da_foreach(statement, &ast->data.block.statements) {
        AST* node = *statement;

        if (node->kind == AST_VARIABLE_DECLARATION)
            compile_global(c, node, scope);
    }

    /* Register all functions and declarations before compiling bodies. */
    da_foreach(statement, &ast->data.block.statements) {
        AST* node = *statement;

        if (node->kind == AST_FUNCTION) {
            compile_function(c, node, true);  // Definition (EXPORTED / T)
        } else if (node->kind == AST_FUNCTION_DECL) {
            compile_function(c, node, false); // Declaration (IMPORTED / U)
        }
    }

    /* Initialize globals after all symbols exist. */
    da_foreach(statement, &ast->data.block.statements) {
        AST* node = *statement;

        if (node->kind == AST_VARIABLE_DECLARATION)
            initialize_global(c, node, scope);
    }

    /* Compile function bodies. */
    da_foreach(statement, &ast->data.block.statements) {
        AST* node = *statement;

        if (node->kind == AST_FUNCTION)
            compile_function_body(c, node);
    }
}

/* ------------------------------------------------------------------------- */
/* Public API                                                                */
/* ------------------------------------------------------------------------- */

void compile_to_file(AST* ast, const char* path) {
    Compiler c = {
        .ctx = gcc_jit_context_acquire(),
        .alloc = ast->alloc,
    };

    assert(c.ctx != NULL, "failed to acquire libgccjit context\n");

    compile_ast(&c, ast);

    const char* error = gcc_jit_context_get_first_error(c.ctx);

    if (error != NULL) {
        fprintf(stderr, "libgccjit: %s\n", error);
        gcc_jit_context_release(c.ctx);
        return;
    }

    gcc_jit_context_compile_to_file(
        c.ctx,
        GCC_JIT_OUTPUT_KIND_OBJECT_FILE,
        path
    );

    error = gcc_jit_context_get_first_error(c.ctx);

    if (error != NULL)
        fprintf(stderr, "libgccjit: %s\n", error);

    gcc_jit_context_release(c.ctx);
}
