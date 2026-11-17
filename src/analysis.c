// TASK(20261115-202733-164-n6-1d8bf5a8): make this smarter
#include "analysis.h"
#include "aids.h"
#include "ast.h"
#include "lexer.h"

#include <stdint.h>
#include <stdio.h>

/* ------------------------------------------------------------------------- */
/* Types                                                                     */
/* ------------------------------------------------------------------------- */

Type* type_clone(Allocator* alloc, const Type* type) {
    Type* clone = ALLOC(alloc, sizeof(Type));
    *clone = *type;
    clone->alloc = alloc;

    switch (type->kind) {
    case TYPE_POINTER:
        clone->data.pointer.child = type_clone(alloc, type->data.pointer.child);
        break;

    case TYPE_FUNCTION: {
        clone->data.function.return_type =
            type_clone(alloc, type->data.function.return_type);

        ParameterList params = {0};
        da_init(&params, alloc);

        da_foreach(param, &type->data.function.params) {
            Parameter cloned = {
                .name = string_clone(&param->name, alloc),
                .type = type_clone(alloc, param->type),
            };

            da_append(&params, cloned);
        }

        clone->data.function.params = params;
        break;
    }

    default:
        break;
    }

    return clone;
}

Type* make_type(Allocator* alloc, TypeKind kind) {
    Type* type = ALLOC(alloc, sizeof(Type));

    *type = (Type){
        .alloc = alloc,
        .kind = kind,
    };

    return type;
}

bool type_equal(const Type* a, const Type* b) {
    if (a == NULL || b == NULL)
        return a == b;

    if (a->kind != b->kind)
        return false;

    switch (a->kind) {
    case TYPE_POINTER:
        return type_equal(a->data.pointer.child, b->data.pointer.child);

    case TYPE_FUNCTION: {
        if (a->data.function.conv != b->data.function.conv ||
            a->data.function.va_args != b->data.function.va_args ||
            !type_equal(a->data.function.return_type,
                        b->data.function.return_type) ||
            a->data.function.params.len != b->data.function.params.len)
            return false;

        for (size_t i = 0; i < a->data.function.params.len; i++) {
            if (!type_equal(a->data.function.params.data[i].type,
                            b->data.function.params.data[i].type))
                return false;
        }

        return true;
    }

    default:
        return true;
    }
}

bool type_is_integer(TypeKind kind) {
    switch (kind) {
    case TYPE_I8:
    case TYPE_I16:
    case TYPE_I32:
    case TYPE_I64:
    case TYPE_U8:
    case TYPE_U16:
    case TYPE_U32:
    case TYPE_U64:
        return true;

    default:
        return false;
    }
}

bool integer_fits(u64 value, TypeKind kind) {
    switch (kind) {
    case TYPE_I8:
        return value <= INT8_MAX;
    case TYPE_I16:
        return value <= INT16_MAX;
    case TYPE_I32:
        return value <= INT32_MAX;
    case TYPE_I64:
        return value <= INT64_MAX;
    case TYPE_U8:
        return value <= UINT8_MAX;
    case TYPE_U16:
        return value <= UINT16_MAX;
    case TYPE_U32:
        return value <= UINT32_MAX;
    case TYPE_U64:
        return true;

    default:
        return false;
    }
}

/* ------------------------------------------------------------------------- */
/* Mangling                                                                  */
/* ------------------------------------------------------------------------- */

/*
 * Translation-unit-wide counter.
 *
 * Every generated name gets a unique ID, including names in nested scopes.
 */
static u64 next_mangled_id = 0;

String mangle_name(Allocator* alloc, String name) {
    char prefix[32];

    snprintf(prefix, sizeof(prefix), "_L%llu_",
             (unsigned long long)next_mangled_id++);

    String mangled = string_create(alloc);
    append_cstr(&mangled, prefix);
    append_string(&mangled, &name);

    return mangled;
}

String symbol_name(Allocator* alloc, String name, FnConv conv,
                   String explicit_symbol) {
    if (explicit_symbol.len != 0)
        return string_clone(&explicit_symbol, alloc);

    if (conv == CONV_C)
        return string_clone(&name, alloc);

    return mangle_name(alloc, name);
}

/* ------------------------------------------------------------------------- */
/* Symbols and scopes                                                        */
/* ------------------------------------------------------------------------- */

Symbol* add_symbol(Allocator* alloc, Scope* scope, String name, Type type,
                   Pos pos) {
    assert(!hm_has(&scope->symbols, name),
           "symbol '" STRING "' already defined at: " POS "\n",
           FMTSTRING(&name), FMTPOS(&pos));

    String key = string_clone(&name, alloc);

    Symbol symbol = {
        .type = type_clone(alloc, &type),
        .mangled = mangle_name(alloc, name),
        .compiler_data = NULL,
    };

    hm_set(&scope->symbols, key, symbol);
    return hm_get(&scope->symbols, key);
}

Symbol* get_symbol(Scope* scope, String name, Pos pos) {
    assert(scope != NULL, "symbol '" STRING "' doesn't exist at: " POS "\n",
           FMTSTRING(&name), FMTPOS(&pos));

    Symbol* symbol = hm_get(&scope->symbols, name);

    if (symbol != NULL)
        return symbol;

    return get_symbol(scope->parent, name, pos);
}

void free_scope(Scope* scope) {
    if (scope == NULL)
        return;

    hm_free_call(&scope->symbols, d, {
        da_free(&d->key);
        da_free(&d->value.mangled);
        type_free(d->value.type);
    });

    FREE(scope->alloc, scope);
}

Scope* createScope(Allocator* alloc, Scope* parent) {
    Scope* scope = ALLOC(alloc, sizeof(Scope));

    *scope = (Scope){
        .alloc = alloc,
        .parent = parent,
        .symbols = {0},
    };

    hm_init(&scope->symbols, alloc);
    return scope;
}

/* ------------------------------------------------------------------------- */
/* Forward declarations                                                      */
/* ------------------------------------------------------------------------- */

Type* expression(Allocator*, AST*, Scope*, Type*);
void statement(Allocator*, AST*, Scope*, Type*);
void block(Allocator*, AST*, Scope*, Type*);
void fn(Allocator*, AST*, Scope*);
void fn_decl(Allocator*, AST*, Scope*);

void register_function(Allocator*, AST*, Scope*);

/* ------------------------------------------------------------------------- */
/* Function registration                                                     */
/* ------------------------------------------------------------------------- */

void register_function(Allocator* alloc, AST* ast, Scope* scope) {
    String name;
    String explicit_symbol = {0};
    Type* return_type;
    ParameterList* source_params;
    FnConv conv;
    bool va_args;

    if (ast->kind == AST_FUNCTION) {
        name = ast->data.function.name;
        return_type = ast->data.function.return_type;
        source_params = &ast->data.function.params;
        conv = CONV_LUT;
        va_args = false;
    } else {
        name = ast->data.function_decl.name;
        return_type = ast->data.function_decl.return_type;
        source_params = &ast->data.function_decl.params;
        conv = ast->data.function_decl.conv;
        va_args = ast->data.function_decl.va_args;
        explicit_symbol = ast->data.function_decl.symbol;
    }

    assert(!hm_has(&scope->symbols, name),
           "symbol '" STRING "' already defined at: " POS "\n",
           FMTSTRING(&name), FMTPOS(ast));

    Type* type = make_type(alloc, TYPE_FUNCTION);

    type->data.function.return_type = type_clone(alloc, return_type);

    type->data.function.conv = conv;
    type->data.function.va_args = va_args;

    da_init(&type->data.function.params, alloc);

    da_foreach(param, source_params) {
        Parameter cloned = {
            .name = string_clone(&param->name, alloc),
            .type = type_clone(alloc, param->type),
        };

        da_append(&type->data.function.params, cloned);
    }

    bool is_entrypoint = ast->kind == AST_FUNCTION && scope->parent == NULL &&
                         string_compare_cstr(name, "main");

    String mangled;

    if (explicit_symbol.len != 0)
        mangled = string_clone(&explicit_symbol, alloc);
    else if (conv == CONV_C || is_entrypoint)
        mangled = string_clone(&name, alloc);
    else
        mangled = mangle_name(alloc, name);

    String key = string_clone(&name, alloc);

    Symbol symbol = {
        .type = type,
        .mangled = mangled,
        .compiler_data = NULL,
    };

    hm_set(&scope->symbols, key, symbol);
}

/* ------------------------------------------------------------------------- */
/* Expressions                                                               */
/* ------------------------------------------------------------------------- */

static bool is_assignable(AST* ast) {
    switch (ast->kind) {
    case AST_IDENT:
        return true;

        // Later:
        // case AST_UNOP: // dereference
        // case AST_MEMBER:
        // case AST_INDEX:

    default:
        return false;
    }
}

static Type* unop(Allocator* alloc, AST* ast, Scope* scope) {
    AST* operand = ast->data.unop.operand;
    Type* type = expression(alloc, operand, scope, NULL);

    switch (ast->data.unop.op) {
    case UP_LNOT:
        assert(type->kind == TYPE_BOOL,
               "logical NOT requires a bool operand\n");
        return make_type(alloc, TYPE_BOOL);
    default:
        assert(false, "unsupported unary operator\n");
        return NULL;
    }
}

static Type* binop(Allocator* alloc, AST* ast, Scope* scope) {
    AST* lhs = ast->data.binop.lhs;
    AST* rhs = ast->data.binop.rhs;

    Type* left = expression(alloc, lhs, scope, NULL);
    Type* right = expression(alloc, rhs, scope, left);

    BinOpKind op = ast->data.binop.op;

    assert(type_equal(left, right),
           "binary operator operands must have matching types\n");

    switch (op) {
    case OP_ASSIGN:
        assert(is_assignable(lhs),
               "left operand of assignment must be assignable\n");
        assert(type_equal(left, right),
               "assignment operands must have matching types\n");
        return type_clone(alloc, left);
    case OP_ADD:
    case OP_SUB:
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
        assert(type_is_integer(left->kind),
               "arithmetic operators require integer operands\n");
        return type_clone(alloc, left);

    case OP_SHL:
    case OP_SHR:
    case OP_BAND:
    case OP_BOR:
    case OP_XOR:
        assert(type_is_integer(left->kind),
               "bitwise operators require integer operands\n");
        return type_clone(alloc, left);

    case OP_LT:
    case OP_GT:
    case OP_LE:
    case OP_GE:
        assert(type_is_integer(left->kind),
               "comparison operators require integer operands\n");
        return make_type(alloc, TYPE_BOOL);

    case OP_EQ:
    case OP_NE:
        assert(type_is_integer(left->kind) || left->kind == TYPE_BOOL ||
                   left->kind == TYPE_CSTR || left->kind == TYPE_POINTER,
               "equality operators don't support these operands\n");
        return make_type(alloc, TYPE_BOOL);

    case OP_LAND:
    case OP_LOR:
        assert(left->kind == TYPE_BOOL,
               "logical operators require bool operands\n");
        return make_type(alloc, TYPE_BOOL);

    default:
        assert(false, "unsupported binary operator\n");
        return NULL;
    }
}

Type* expression(Allocator* alloc, AST* ast, Scope* scope, Type* expected) {
    assert(ast != NULL, "expected an expression\n");

    Pos pos = {ast->line, ast->col};

    switch (ast->kind) {
    case AST_INT_LITERAL: {
        TypeKind kind = TYPE_I32;

        if (expected != NULL && type_is_integer(expected->kind)) {
            assert(integer_fits(ast->data.int_literal.value, expected->kind),
                   "integer literal doesn't fit expected type at: " POS "\n",
                   FMTPOS(&pos));

            kind = expected->kind;
        }

        ast->type = make_type(alloc, kind);
        break;
    }
    case AST_UNOP:
        ast->type = unop(alloc, ast, scope);
        break;
    case AST_BINOP:
        ast->type = binop(alloc, ast, scope);
        break;
    case AST_BOOL_LIT:
        ast->type = make_type(alloc, TYPE_BOOL);
        break;

    case AST_STR_LITERAL:
        ast->type = make_type(alloc, TYPE_CSTR);
        break;

    case AST_IDENT: {
        Symbol* symbol = get_symbol(scope, ast->data.ident.name, pos);

        ast->type = type_clone(alloc, symbol->type);
        break;
    }

    case AST_FUNCTION_CALL: {
        Symbol* symbol = get_symbol(scope, ast->data.function_call.name, pos);

        Type* function = symbol->type;

        if (function->kind == TYPE_POINTER)
            function = function->data.pointer.child;

        assert(function->kind == TYPE_FUNCTION,
               "called object is not a function at: " POS "\n", FMTPOS(&pos));

        ParameterList* params = &function->data.function.params;
        ASTList* args = &ast->data.function_call.args;

        assert(function->data.function.va_args ? args->len >= params->len
                                               : args->len == params->len,
               "incorrect argument count in function call at: " POS "\n",
               FMTPOS(&pos));

        for (size_t i = 0; i < args->len; i++) {
            Type* expected_arg = NULL;

            if (i < params->len)
                expected_arg = params->data[i].type;

            Type* actual =
                expression(alloc, args->data[i], scope, expected_arg);

            if (expected_arg != NULL) {
                assert(type_equal(actual, expected_arg),
                       "incorrect argument type at: " POS "\n", FMTPOS(&pos));
            }
        }

        ast->type = type_clone(alloc, function->data.function.return_type);

        break;
    }

    default:
        assert(false, "unsupported expression node at: " POS "\n",
               FMTPOS(&pos));
    }

    return ast->type;
}

/* ------------------------------------------------------------------------- */
/* Statements                                                                */
/* ------------------------------------------------------------------------- */

void if_stmt(Allocator* alloc, AST* ast, Scope* scope, Type* return_type) {
    AST* cond = ast->data.if_stmt.cond;

    Type* actual = expression(alloc, cond, scope, NULL);

    assert(type_equal(actual, &(Type){.kind = TYPE_BOOL}),
           "if condition must be bool\n");

    statement(alloc, ast->data.if_stmt.then, scope, return_type);

    if (ast->data.if_stmt.el != NULL)
        statement(alloc, ast->data.if_stmt.el, scope, return_type);
}

void while_loop(Allocator* alloc, AST* ast, Scope* scope, Type* return_type) {
    AST* cond = ast->data.while_loop.cond;

    Type* actual = expression(alloc, cond, scope, NULL);

    assert(type_equal(actual, &(Type){.kind = TYPE_BOOL}),
           "while condition must be bool\n");

    statement(alloc, ast->data.while_loop.body, scope, return_type);
}

void statement(Allocator* alloc, AST* ast, Scope* scope, Type* return_type) {
    Pos pos = {ast->line, ast->col};

    switch (ast->kind) {
    case AST_BLOCK:
        block(alloc, ast, scope, return_type);
        break;

    case AST_FUNCTION:
        fn(alloc, ast, scope);
        break;

    case AST_FUNCTION_DECL:
        fn_decl(alloc, ast, scope);
        break;

    case AST_IF:
        if_stmt(alloc, ast, scope, return_type);
        break;

    case AST_WHILE:
        while_loop(alloc, ast, scope, return_type);
        break;

    case AST_VARIABLE_DECLARATION: {
        Type* declared = ast->data.var_decl.type;
        AST* initializer = ast->data.var_decl.val;

        if (declared != NULL && declared->kind == TYPE_INFER) {
            type_free(declared);
            declared = NULL;
            ast->data.var_decl.type = NULL;
        }

        if (initializer != NULL) {
            Type* actual = expression(alloc, initializer, scope, declared);

            if (declared == NULL) {
                declared = type_clone(alloc, actual);
                ast->data.var_decl.type = declared;
            } else {
                assert(type_equal(actual, declared),
                       "initializer type doesn't match variable type at: " POS
                       "\n",
                       FMTPOS(&pos));
            }
        } else {
            assert(declared != NULL,
                   "cannot infer type of uninitialized variable at: " POS "\n",
                   FMTPOS(&pos));
        }

        add_symbol(alloc, scope, ast->data.var_decl.name, *declared, pos);
        break;
    }

    case AST_RETURN: {
        AST* value = ast->data.return_stmt.value;

        assert(return_type != NULL, "return outside of function at: " POS "\n",
               FMTPOS(&pos));

        if (value == NULL) {
            assert(return_type->kind == TYPE_VOID,
                   "missing return value at: " POS "\n", FMTPOS(&pos));
        } else {
            assert(return_type->kind != TYPE_VOID,
                   "void function cannot return a value at: " POS "\n",
                   FMTPOS(&pos));

            Type* actual = expression(alloc, value, scope, return_type);

            assert(type_equal(actual, return_type),
                   "return type mismatch at: " POS "\n", FMTPOS(&pos));
        }

        break;
    }

    case AST_EXPR_STMT:
        expression(alloc, ast->data.expr_stmt.value, scope, NULL);
        break;

    default:
        assert(false, "unsupported statement node at: " POS "\n", FMTPOS(&pos));
    }
}

/* ------------------------------------------------------------------------- */
/* Functions and blocks                                                      */
/* ------------------------------------------------------------------------- */

void fn(Allocator* alloc, AST* ast, Scope* parent) {
    Scope* scope = ast->data.function.scope = createScope(alloc, parent);

    da_foreach(param, &ast->data.function.params) {
        add_symbol(alloc, scope, param->name, *param->type,
                   (Pos){ast->line, ast->col});
    }

    statement(alloc, ast->data.function.body, scope,
              ast->data.function.return_type);
}

void fn_decl(Allocator* alloc, AST* ast, Scope* parent) {
    Scope* scope = ast->data.function_decl.scope = createScope(alloc, parent);

    da_foreach(param, &ast->data.function_decl.params) {
        if (param->name.data == NULL)
            continue;

        add_symbol(alloc, scope, param->name, *param->type,
                   (Pos){ast->line, ast->col});
    }
}

void block(Allocator* alloc, AST* ast, Scope* parent, Type* return_type) {
    assert(ast->kind == AST_BLOCK, "expected a block\n");

    Scope* scope = ast->data.block.scope = createScope(alloc, parent);

    /*
     * Register functions first so they can refer to declarations
     * appearing later in the same block.
     */
    da_foreach(stmt, &ast->data.block.statements) {
        if ((*stmt)->kind == AST_FUNCTION ||
            (*stmt)->kind == AST_FUNCTION_DECL) {
            register_function(alloc, *stmt, scope);
        }
    }

    da_foreach(stmt, &ast->data.block.statements) {
        statement(alloc, *stmt, scope, return_type);
    }
}

/* ------------------------------------------------------------------------- */
/* Entry point                                                               */
/* ------------------------------------------------------------------------- */

void analyze(AST* ast) {
    Allocator* alloc = ast->alloc;

    assert(ast->kind == AST_BLOCK && ast->data.block.toplevel,
           "expected a top level block\n");

    next_mangled_id = 0;

    block(alloc, ast, NULL, NULL);
}
