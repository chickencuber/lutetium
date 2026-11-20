//TASK(20261119-015130-098-n6-6c9de76f): change the backend to one that supports more targets
#include "compile.h"
#include "aids.h"
#include "analysis.h"
#include "ast.h"

#include <libgccjit.h>
#include <stdio.h>

/* ------------------------------------------------------------------------- */
/* Compiler                                                                  */
/* ------------------------------------------------------------------------- */

typedef struct StructInfo StructInfo;
struct StructInfo {
    String name;
    gcc_jit_struct* struct_type;
    gcc_jit_type* type;
    gcc_jit_field** fields;
    size_t field_count;
    ParameterList* ast_fields;
};
typedef DA(StructInfo) StructList;

typedef struct Compiler Compiler;
struct Compiler {
    gcc_jit_context* ctx;
    Allocator* alloc;
    StructList structs;
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


static StructInfo* get_struct(Compiler* c, String name) {
    da_foreach(info, &c->structs) {
        if (string_compare(info->name, name))
            return info;
    }

    assert(false, "unknown struct type\n");
    return NULL;
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
    case TYPE_I128:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_INT128_T);

    case TYPE_U8:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_UINT8_T);
    case TYPE_U16:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_UINT16_T);
    case TYPE_U32:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_UINT32_T);
    case TYPE_U64:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_UINT64_T);
    case TYPE_U128:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_UINT128_T);

    case TYPE_BOOL:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_BOOL);

    case TYPE_CSTR:
        return gcc_jit_context_get_type(c->ctx, GCC_JIT_TYPE_CONST_CHAR_PTR);

    case TYPE_POINTER:
        return gcc_jit_type_get_pointer(
            compile_type(c, type->data.pointer.child));

    case TYPE_USER_DEFINED:
        return get_struct(c, type->data.user_defined.name)->type;

    case TYPE_FUNCTION: {
        ParameterList* params = &type->data.function.params;
        gcc_jit_type** gcc_params = NULL;

        if (params->len > 0) {
            gcc_params = ALLOC(c->alloc, sizeof(gcc_jit_type*) * params->len);

            for (size_t i = 0; i < params->len; i++)
                gcc_params[i] = compile_type(c, params->data[i].type);
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
/* Structs                                                                   */
/* ------------------------------------------------------------------------- */

static void compile_structs(Compiler* c, AST* ast) {
    ASTList* statements = &ast->data.block.statements;


    /* First create opaque types so fields can refer to any declared struct. */
    da_foreach(statement, statements) {
        AST* node = *statement;

        if (node->kind != AST_STRUCT)
            continue;

        String name = node->data.struct_def.name;
        char* struct_name = compile_string(c, &name);

        gcc_jit_struct* struct_type = gcc_jit_context_new_opaque_struct(
            c->ctx, NULL, struct_name);

        FREE(c->alloc, struct_name);
        assert(struct_type != NULL, "failed to create opaque struct\n");


        StructInfo info = {
            .name = name,
            .struct_type = struct_type,
            .type = gcc_jit_struct_as_type(struct_type),
            .field_count = node->data.struct_def.fields.len,
            .ast_fields = &node->data.struct_def.fields,
        };


        if (info.field_count != 0)
            info.fields = ALLOC(c->alloc,
                               sizeof(gcc_jit_field*) * info.field_count);

        da_append(&c->structs, info);
    }


    /* Then define fields after all names have been registered. */
    da_foreach(info, &c->structs) {
        ParameterList* fields = info->ast_fields;

        for (size_t i = 0; i < fields->len; i++) {
            Parameter* field = &fields->data[i];
            char* name = compile_string(c, &field->name);

            info->fields[i] = gcc_jit_context_new_field(
                c->ctx, NULL, compile_type(c, field->type), name);

            FREE(c->alloc, name);
            assert(info->fields[i] != NULL, "failed to create struct field\n");
        }


        gcc_jit_struct_set_fields(info->struct_type, NULL,
                                  (int)info->field_count, info->fields);
    }
}


/* ------------------------------------------------------------------------- */
/* Functions                                                                 */
/* ------------------------------------------------------------------------- */


static gcc_jit_function* compile_function(Compiler* c, AST* ast,
                                          bool is_definition) {
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

    Scope* lookup_scope = (scope->parent != NULL) ? scope->parent : scope;

    Symbol* symbol = get_symbol(lookup_scope, name, ast_pos(ast));

    assert(symbol != NULL, "function symbol not found\n");

    if (symbol->compiler_data != NULL)
        return symbol->compiler_data;


    gcc_jit_param** gcc_params = NULL;

    if (params->len > 0)
        gcc_params = ALLOC(c->alloc, sizeof(gcc_jit_param*) * params->len);


    for (size_t i = 0; i < params->len; i++) {
        Parameter* param = &params->data[i];

        char generated_name[32];
        char* allocated_name = NULL;
        const char* param_name;

        Symbol* param_symbol = NULL;

        if (param->name.data != NULL)
            param_symbol = get_symbol(scope, param->name, ast_pos(ast));

        if (param_symbol != NULL) {
            allocated_name = compile_string(c, &param_symbol->mangled);
            param_name = allocated_name;
        } else {
            snprintf(generated_name, sizeof(generated_name), "arg%zu", i);
            param_name = generated_name;
        }

        gcc_params[i] = gcc_jit_context_new_param(
            c->ctx, NULL, compile_type(c, param->type), param_name);

        if (allocated_name != NULL)
            FREE(c->alloc, allocated_name);
    }


    char* function_name = compile_string(c, &symbol->mangled);


    gcc_jit_function* function = gcc_jit_context_new_function(
        c->ctx, NULL,
        is_definition ? GCC_JIT_FUNCTION_EXPORTED : GCC_JIT_FUNCTION_IMPORTED,
        compile_type(c, return_type), function_name, (int)params->len,
        gcc_params, va_args);

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

static gcc_jit_rvalue* compile_expression(Compiler* c, AST* ast, Scope* scope,
                                          gcc_jit_block* block);


static gcc_jit_lvalue* compile_lvalue(Compiler* c, AST* ast, Scope* scope) {
    UNUSED(c);
    assert(ast != NULL, "cannot compile NULL lvalue\n");


    switch (ast->kind) {
    case AST_IDENT: {
        Symbol* symbol = get_symbol(scope, ast->data.ident.name, ast_pos(ast));

        assert(symbol != NULL, "identifier symbol not found\n");

        gcc_jit_lvalue* lvalue = symbol->compiler_data;

        assert(lvalue != NULL, "identifier has no compiled lvalue\n");

        return lvalue;
    }
    case AST_UNOP:
        if (ast->data.unop.op == UP_DEREF) {
            gcc_jit_rvalue* pointer =
                compile_expression(c, ast->data.unop.operand, scope, NULL);

            return gcc_jit_rvalue_dereference(pointer, NULL);
        }
        assert(false, "unary expression is not an lvalue\n");
        return NULL;


    default:
        assert(false, "unsupported lvalue in compile_lvalue\n");
        return NULL;
    }
}


static gcc_jit_rvalue* compile_struct_initializer(
    Compiler* c, AST* ast, Scope* scope, gcc_jit_block* block) {
    StructInfo* info = get_struct(c, ast->data.struct_init.name);
    StructInitializerFields* init_fields = ast->data.struct_init.fields;

    if (init_fields == NULL || init_fields->len == 0) {
        return gcc_jit_context_new_struct_constructor(
            c->ctx, NULL, info->type, 0, NULL, NULL);
    }


    gcc_jit_rvalue** values = CALLOC(c->alloc, info->field_count,
                                       sizeof(gcc_jit_rvalue*));

    da_foreach(init_field, init_fields) {
        size_t field_index = info->field_count;

        for (size_t i = 0; i < info->ast_fields->len; i++) {
            Parameter* field = &info->ast_fields->data[i];

            if (string_compare(field->name, init_field->name)) {
                field_index = i;
                break;
            }
        }

        assert(field_index < info->field_count,
               "unknown struct initializer field\n");
        assert(values[field_index] == NULL,
               "duplicate struct initializer field\n");

        values[field_index] = compile_expression(
            c, init_field->value, scope, block);
    }


    gcc_jit_rvalue* result = gcc_jit_context_new_struct_constructor(
        c->ctx, NULL, info->type, info->field_count,
        info->fields, values);

    FREE(c->alloc, values);
    assert(result != NULL, "failed to create struct constructor\n");

    return result;
}


static gcc_jit_rvalue* compile_expression(Compiler* c, AST* ast, Scope* scope,
                                          gcc_jit_block* block) {
    assert(ast != NULL, "cannot compile NULL expression\n");


    switch (ast->kind) {
    case AST_STRUCT_INIT:
        return compile_struct_initializer(c, ast, scope, block);

    case AST_UNOP: {
        AST* operand = ast->data.unop.operand;

        switch (ast->data.unop.op) {
        case UP_REF: {
            gcc_jit_lvalue* lvalue = compile_lvalue(c, operand, scope);

            return gcc_jit_lvalue_get_address(lvalue, NULL);
        }

        case UP_DEREF: {
            gcc_jit_lvalue* lvalue = compile_lvalue(c, ast, scope);

            return gcc_jit_lvalue_as_rvalue(lvalue);
        }

        case UP_LNOT: {
            gcc_jit_rvalue* value =
                compile_expression(c, operand, scope, block);

            return gcc_jit_context_new_unary_op(
                c->ctx, NULL, GCC_JIT_UNARY_OP_LOGICAL_NEGATE,
                compile_type(c, ast->type), value);
        }

        case UP_NEG: {
            gcc_jit_rvalue* value =
                compile_expression(c, operand, scope, block);

            return gcc_jit_context_new_unary_op(
                c->ctx, NULL, GCC_JIT_UNARY_OP_MINUS,
                compile_type(c, ast->type), value);
        }

        case UP_POS:
            return compile_expression(c, operand, scope, block);

        default:
            assert(false, "unsupported unary operator in codegen\n");
            return NULL;
        }
    }
    case AST_BINOP: {
        AST* lhs = ast->data.binop.lhs;
        AST* rhs = ast->data.binop.rhs;

        switch (ast->data.binop.op) {
            /* Assignment */

        case OP_ASSIGN: {
            assert(block != NULL,
                   "assignment expression requires a basic block\n");

            gcc_jit_lvalue* left = compile_lvalue(c, lhs, scope);
            gcc_jit_rvalue* right = compile_expression(c, rhs, scope, block);

            gcc_jit_block_add_assignment(block, NULL, left, right);

            return gcc_jit_lvalue_as_rvalue(left);
        }

            /* Arithmetic */

        case OP_ADD:
            return gcc_jit_context_new_binary_op(
                c->ctx, NULL, GCC_JIT_BINARY_OP_PLUS,
                compile_type(c, ast->type),
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

        case OP_SUB:
            return gcc_jit_context_new_binary_op(
                c->ctx, NULL, GCC_JIT_BINARY_OP_MINUS,
                compile_type(c, ast->type),
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

        case OP_MUL:
            return gcc_jit_context_new_binary_op(
                c->ctx, NULL, GCC_JIT_BINARY_OP_MULT,
                compile_type(c, ast->type),
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

        case OP_DIV:
            return gcc_jit_context_new_binary_op(
                c->ctx, NULL, GCC_JIT_BINARY_OP_DIVIDE,
                compile_type(c, ast->type),
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

        case OP_MOD:
            return gcc_jit_context_new_binary_op(
                c->ctx, NULL, GCC_JIT_BINARY_OP_MODULO,
                compile_type(c, ast->type),
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

            /* Shifts */

        case OP_SHL:
            return gcc_jit_context_new_binary_op(
                c->ctx, NULL, GCC_JIT_BINARY_OP_LSHIFT,
                compile_type(c, ast->type),
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

        case OP_SHR:
            return gcc_jit_context_new_binary_op(
                c->ctx, NULL, GCC_JIT_BINARY_OP_RSHIFT,
                compile_type(c, ast->type),
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

            /* Bitwise */

        case OP_BAND:
            return gcc_jit_context_new_binary_op(
                c->ctx, NULL, GCC_JIT_BINARY_OP_BITWISE_AND,
                compile_type(c, ast->type),
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

        case OP_BOR:
            return gcc_jit_context_new_binary_op(
                c->ctx, NULL, GCC_JIT_BINARY_OP_BITWISE_OR,
                compile_type(c, ast->type),
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

        case OP_XOR:
            return gcc_jit_context_new_binary_op(
                c->ctx, NULL, GCC_JIT_BINARY_OP_BITWISE_XOR,
                compile_type(c, ast->type),
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

            /* Logical */

        case OP_LAND:
            return gcc_jit_context_new_binary_op(
                c->ctx, NULL, GCC_JIT_BINARY_OP_LOGICAL_AND,
                compile_type(c, ast->type),
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

        case OP_LOR:
            return gcc_jit_context_new_binary_op(
                c->ctx, NULL, GCC_JIT_BINARY_OP_LOGICAL_OR,
                compile_type(c, ast->type),
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

            /* Comparisons */

        case OP_LT:
            return gcc_jit_context_new_comparison(
                c->ctx, NULL, GCC_JIT_COMPARISON_LT,
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

        case OP_GT:
            return gcc_jit_context_new_comparison(
                c->ctx, NULL, GCC_JIT_COMPARISON_GT,
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

        case OP_LE:
            return gcc_jit_context_new_comparison(
                c->ctx, NULL, GCC_JIT_COMPARISON_LE,
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

        case OP_GE:
            return gcc_jit_context_new_comparison(
                c->ctx, NULL, GCC_JIT_COMPARISON_GE,
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

        case OP_NE:
            return gcc_jit_context_new_comparison(
                c->ctx, NULL, GCC_JIT_COMPARISON_NE,
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

        case OP_EQ:
            return gcc_jit_context_new_comparison(
                c->ctx, NULL, GCC_JIT_COMPARISON_EQ,
                compile_expression(c, lhs, scope, block),
                compile_expression(c, rhs, scope, block));

        case OP_NULL:
        default:
            assert(false, "unsupported binary operator in codegen\n");
            return NULL;
        }
    }


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
        gcc_jit_lvalue* lvalue = compile_lvalue(c, ast, scope);
        return gcc_jit_lvalue_as_rvalue(lvalue);
    }


    case AST_FUNCTION_CALL: {
        Symbol* symbol =
            get_symbol(scope, ast->data.function_call.name, ast_pos(ast));

        assert(symbol != NULL, "function symbol not found\n");

        gcc_jit_function* function = symbol->compiler_data;

        assert(function != NULL, "function was not compiled\n");


        ASTList* args = &ast->data.function_call.args;
        gcc_jit_rvalue** gcc_args = NULL;

        if (args->len > 0) {
            gcc_args = ALLOC(c->alloc, sizeof(gcc_jit_rvalue*) * args->len);

            for (size_t i = 0; i < args->len; i++) {
                gcc_args[i] =
                    compile_expression(c, args->data[i], scope, block);
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
/* Globals */
/* ------------------------------------------------------------------------- */

static void compile_global(Compiler* c, AST* ast, Scope* scope) {
    Type* type = ast->data.var_decl.type;
    String name = ast->data.var_decl.name;


    assert(type != NULL, "global variable has no type\n");

    Symbol* symbol = get_symbol(scope, name, ast_pos(ast));

    char* cstr_name = compile_string(c, &symbol->mangled);

    gcc_jit_lvalue* global =
        gcc_jit_context_new_global(c->ctx, NULL, GCC_JIT_GLOBAL_EXPORTED,
                                  compile_type(c, type), cstr_name);

    FREE(c->alloc, cstr_name);

    assert(global != NULL, "failed to create global variable\n");

    symbol->compiler_data = global;
}


static void initialize_global(Compiler* c, AST* ast, Scope* scope) {
    if (ast->data.var_decl.val == NULL)
        return;

    Symbol* symbol = get_symbol(scope, ast->data.var_decl.name, ast_pos(ast));

    gcc_jit_lvalue* global = symbol->compiler_data;

    assert(global != NULL, "global variable was not compiled\n");


    gcc_jit_global_set_initializer_rvalue(
        global, compile_expression(c, ast->data.var_decl.val, scope, NULL));
}


/* ------------------------------------------------------------------------- */
/* Statements                                                                */
/* ------------------------------------------------------------------------- */

static gcc_jit_block* compile_statement(Compiler* c, gcc_jit_function* function,
                                        gcc_jit_block* block, AST* ast,
                                        Scope* scope);

static gcc_jit_block* compile_block(Compiler* c, gcc_jit_function* function,
                                    gcc_jit_block* block, AST* ast);


static bool statement_ends_with_return(AST* ast) {
    if (ast->kind == AST_RETURN)
        return true;


    if (ast->kind == AST_IF) {
        return ast->data.if_stmt.el != NULL &&
               statement_ends_with_return(ast->data.if_stmt.then) &&
               statement_ends_with_return(ast->data.if_stmt.el);
    }


    if (ast->kind == AST_BLOCK) {
        da_foreach(statement, &ast->data.block.statements) {
            if (statement_ends_with_return(*statement))
                return true;
        }
    }


    return false;
}


static gcc_jit_block* compile_statement(Compiler* c, gcc_jit_function* function,
                                        gcc_jit_block* block, AST* ast,
                                        Scope* scope) {
    switch (ast->kind) {
    case AST_BLOCK:
        return compile_block(c, function, block, ast);


    case AST_IF: {
        gcc_jit_rvalue* condition =
            compile_expression(c, ast->data.if_stmt.cond, scope, block);

        AST* then_stmt = ast->data.if_stmt.then;
        AST* else_stmt = ast->data.if_stmt.el;

        bool then_returns = statement_ends_with_return(then_stmt);
        bool else_returns =
            else_stmt != NULL && statement_ends_with_return(else_stmt);

        gcc_jit_block* then_block = gcc_jit_function_new_block(function, NULL);
        gcc_jit_block* else_block = gcc_jit_function_new_block(function, NULL);
        gcc_jit_block* after_block = NULL;

        if (!then_returns || !else_returns)
            after_block = gcc_jit_function_new_block(function, NULL);

        gcc_jit_block_end_with_conditional(block, NULL, condition, then_block,
                                           else_block);

        gcc_jit_block* then_end =
            compile_statement(c, function, then_block, then_stmt, scope);

        if (!then_returns)
            gcc_jit_block_end_with_jump(then_end, NULL, after_block);

        if (else_stmt != NULL) {
            gcc_jit_block* else_end =
                compile_statement(c, function, else_block, else_stmt, scope);

            if (!else_returns)
                gcc_jit_block_end_with_jump(else_end, NULL, after_block);
        } else {
            gcc_jit_block_end_with_jump(else_block, NULL, after_block);
        }


        return after_block;
    }


    case AST_WHILE: {
        gcc_jit_block* cond_block = gcc_jit_function_new_block(function, NULL);
        gcc_jit_block* body_block = gcc_jit_function_new_block(function, NULL);
        gcc_jit_block* after_block = gcc_jit_function_new_block(function, NULL);

        gcc_jit_block_end_with_jump(block, NULL, cond_block);

        gcc_jit_rvalue* condition =
            compile_expression(c, ast->data.while_loop.cond, scope, cond_block);

        gcc_jit_block_end_with_conditional(cond_block, NULL, condition,
                                             body_block, after_block);

        gcc_jit_block* body_end = compile_statement(
            c, function, body_block, ast->data.while_loop.body, scope);

        if (body_end != NULL) {
            if (!statement_ends_with_return(ast->data.while_loop.body))
                gcc_jit_block_end_with_jump(body_end, NULL, cond_block);
        }

        return after_block;
    }


    case AST_FUNCTION:
    case AST_FUNCTION_DECL:
        return block;


    case AST_RETURN:
        if (ast->data.return_stmt.value == NULL) {
            gcc_jit_block_end_with_void_return(block, NULL);
        } else {
            gcc_jit_block_end_with_return(
                block, NULL,
                compile_expression(c, ast->data.return_stmt.value, scope,
                                   block));
        }

        return NULL;


    case AST_VARIABLE_DECLARATION: {
        Type* type = ast->data.var_decl.type;

        assert(type != NULL, "variable declaration has no type\n");

        Symbol* symbol =
            get_symbol(scope, ast->data.var_decl.name, ast_pos(ast));

        char* name = compile_string(c, &symbol->mangled);

        gcc_jit_lvalue* local = gcc_jit_function_new_local(
            function, NULL, compile_type(c, type), name);

        FREE(c->alloc, name);

        assert(local != NULL, "failed to create local variable\n");

        symbol->compiler_data = local;


        if (ast->data.var_decl.val != NULL) {
            gcc_jit_block_add_assignment(
                block, NULL, local,
                compile_expression(c, ast->data.var_decl.val, scope, block));
        }

        return block;
    }


    case AST_EXPR_STMT: {
        gcc_jit_rvalue* value =
            compile_expression(c, ast->data.expr_stmt.value, scope, block);

        gcc_jit_block_add_eval(block, NULL, value);

        return block;
    }


    default:
        assert(false, "unsupported statement in compile_statement\n");
        return block;
    }
}


static gcc_jit_block* compile_block(Compiler* c, gcc_jit_function* function,
                                    gcc_jit_block* block, AST* ast) {
    Scope* scope = ast->data.block.scope;


    da_foreach(statement, &ast->data.block.statements) {
        block = compile_statement(c, function, block, *statement, scope);

        if (block == NULL)
            break;
    }


    return block;
}


/* ------------------------------------------------------------------------- */
/* Function bodies                                                           */
/* ------------------------------------------------------------------------- */

static void compile_function_body(Compiler* c, AST* ast) {
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

    block = compile_statement(c, function, block, body, scope);

    if (block != NULL && ast->data.function.return_type->kind == TYPE_VOID &&
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

    compile_structs(c, ast);


    da_foreach(statement, &ast->data.block.statements) {
        AST* node = *statement;

        if (node->kind == AST_VARIABLE_DECLARATION)
            compile_global(c, node, scope);
    }


    da_foreach(statement, &ast->data.block.statements) {
        AST* node = *statement;

        if (node->kind == AST_FUNCTION) {
            compile_function(c, node, true);
        } else if (node->kind == AST_FUNCTION_DECL) {
            compile_function(c, node, false);
        }
    }


    da_foreach(statement, &ast->data.block.statements) {
        AST* node = *statement;

        if (node->kind == AST_VARIABLE_DECLARATION)
            initialize_global(c, node, scope);
    }


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

    da_init(&c.structs, c.alloc);


    assert(c.ctx != NULL, "failed to acquire libgccjit context\n");


    compile_ast(&c, ast);


    const char* error = gcc_jit_context_get_first_error(c.ctx);

    if (error != NULL) {
        fprintf(stderr, "libgccjit: %s\n", error);

        da_foreach(info, &c.structs) {
            if (info->fields != NULL)
                FREE(c.alloc, info->fields);
        }
        da_free(&c.structs);

        gcc_jit_context_release(c.ctx);
        return;
    }


    gcc_jit_context_compile_to_file(c.ctx, GCC_JIT_OUTPUT_KIND_OBJECT_FILE,
                                    path);

    error = gcc_jit_context_get_first_error(c.ctx);

    if (error != NULL)
        fprintf(stderr, "libgccjit: %s\n", error);


    da_foreach(info, &c.structs) {
        if (info->fields != NULL)
            FREE(c.alloc, info->fields);
    }
    da_free(&c.structs);


    gcc_jit_context_release(c.ctx);
}
