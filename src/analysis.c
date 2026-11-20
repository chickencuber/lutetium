//TASK(20261115-202733-164-n6-1d8bf5a8): make this smarter
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
    if (type == NULL)
        return NULL;

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

    case TYPE_USER_DEFINED:
        String other = type->data.user_defined.name;
        clone->data.user_defined.name = string_clone(&other, alloc);
        break;

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
    case TYPE_USER_DEFINED:
        return string_compare(a->data.user_defined.name,
                              b->data.user_defined.name);

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
    case TYPE_I128:
    case TYPE_U128:
        return true;

    default:
        return false;
    }
}

static unsigned integer_width(TypeKind kind) {
    switch (kind) {
    case TYPE_I8:
    case TYPE_U8:
        return 8;

    case TYPE_I16:
    case TYPE_U16:
        return 16;

    case TYPE_I32:
    case TYPE_U32:
        return 32;

    case TYPE_I64:
    case TYPE_U64:
        return 64;

    case TYPE_I128:
    case TYPE_U128:
        return 128;

    default:
        return 0;
    }
}

static bool integer_is_signed(TypeKind kind) {
    switch (kind) {
    case TYPE_I8:
    case TYPE_I16:
    case TYPE_I32:
    case TYPE_I64:
    case TYPE_I128:
        return true;

    default:
        return false;
    }
}

bool integer_fits(u128 value, TypeKind kind) {
    switch (kind) {
    case TYPE_I8:
        return value <= INT8_MAX;
    case TYPE_I16:
        return value <= INT16_MAX;
    case TYPE_I32:
        return value <= INT32_MAX;
    case TYPE_I64:
        return value <= INT64_MAX;
    case TYPE_I128:
        return value <= INT128_MAX;

    case TYPE_U8:
        return value <= UINT8_MAX;
    case TYPE_U16:
        return value <= UINT16_MAX;
    case TYPE_U32:
        return value <= UINT32_MAX;
    case TYPE_U64:
        return value <= UINT64_MAX;
    case TYPE_U128:
        return true;

    default:
        return false;
    }
}

static TypeKind integer_type(unsigned width, bool is_signed) {
    switch (width) {
    case 8:
        return is_signed ? TYPE_I8 : TYPE_U8;
    case 16:
        return is_signed ? TYPE_I16 : TYPE_U16;
    case 32:
        return is_signed ? TYPE_I32 : TYPE_U32;
    case 128:
        return is_signed ? TYPE_I128 : TYPE_U128;
    default:
        return is_signed ? TYPE_I64 : TYPE_U64;
    }
}

/*
 * Infer an integer literal using the signed default.
 *
 * The literal's radix does not affect its signedness.
 */
static TypeKind infer_integer_type(u128 value) {
    if (value <= INT32_MAX)
        return TYPE_I32;

    if (value <= INT64_MAX)
        return TYPE_I64;

    return TYPE_U64;
}

/*
 * Infer an integer literal using the unsigned default.
 *
 * This is selected by the unsigned syntax hint.
 */
static TypeKind infer_unsigned_integer_type(u128 value) {
    if (value <= UINT32_MAX)
        return TYPE_U32;

    return TYPE_U64;
}

/*
 * Select a common type for two integer literals.
 *
 * This is only used for literals. Non-literal integer expressions are
 * never implicitly converted to another integer type.
 */
static TypeKind common_integer_type(TypeKind a, TypeKind b) {
    unsigned aw = integer_width(a);
    unsigned bw = integer_width(b);

    bool as = integer_is_signed(a);
    bool bs = integer_is_signed(b);

    if (as == bs)
        return integer_type(aw > bw ? aw : bw, as);

    if (as && aw > bw)
        return a;

    if (bs && bw > aw)
        return b;

    unsigned width = aw > bw ? aw : bw;
    return integer_type(width, false);
}

/* ------------------------------------------------------------------------- */
/* Mangling                                                                  */
/* ------------------------------------------------------------------------- */

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
        .kind = SYMBOL_VALUE,
        .data.type = type_clone(alloc, &type),
        .mangled = mangle_name(alloc, name),
        .compiler_data = NULL,
    };

    hm_set(&scope->symbols, key, symbol);
    return hm_get(&scope->symbols, key);
}

Symbol* get_symbol(Scope* scope, String name, Pos pos) {
    if (scope == NULL) {
        assert(false, "symbol '" STRING "' doesn't exist at: " POS "\n",
               FMTSTRING(&name), FMTPOS(&pos));
        return NULL;
    }

    Symbol* symbol = hm_get(&scope->symbols, name);

    if (symbol != NULL)
        return symbol;

    if (scope->parent != NULL)
        return get_symbol(scope->parent, name, pos);

    assert(false, "symbol '" STRING "' doesn't exist at: " POS "\n",
           FMTSTRING(&name), FMTPOS(&pos));
    return NULL;
}

void free_scope(Scope* scope) {
    if (scope == NULL)
        return;

    hm_free_call(&scope->symbols, d, {
        da_free(&d->key);
        da_free(&d->value.mangled);
        if (d->value.kind == SYMBOL_VALUE)
            type_free(d->value.data.type);
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
void register_struct(AST*, Scope*);

void register_struct(AST* ast, Scope* scope) {
    String name = ast->data.struct_def.name;

    assert(!hm_has(&scope->symbols, name),
           "symbol '" STRING "' already defined at: " POS "\n",
           FMTSTRING(&name), FMTPOS(ast));

    Symbol symbol = {
        .kind = SYMBOL_TYPE,
        .data.params = &ast->data.struct_def.fields,
        .mangled = {0},
        .compiler_data = NULL,
    };

    hm_set(&scope->symbols, string_clone(&name, scope->alloc), symbol);
}

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
        .kind = SYMBOL_VALUE,
        .data.type = type,
        .mangled = mangled,
        .compiler_data = NULL,
    };

    hm_set(&scope->symbols, key, symbol);
}

/* ------------------------------------------------------------------------- */
/* Expressions                                                               */
/* ------------------------------------------------------------------------- */

/*
 * An lvalue is an expression that refers to storage.
 *
 * Identifiers refer to variables, and dereferencing a pointer produces
 * an lvalue. Other unary operators do not produce lvalues.
 */
static bool is_lvalue(AST* ast) {
    switch (ast->kind) {
    case AST_IDENT:
        return true;

    case AST_UNOP:
        return ast->data.unop.op == UP_DEREF;

    default:
        return false;
    }
}

static bool is_assignable(AST* ast) { return is_lvalue(ast); }

static bool is_integer_literal(AST* ast) {
    return ast->kind == AST_INT_LITERAL;
}

static void check_expected_type(Type* actual, Type* expected, Pos pos) {
    if (expected == NULL)
        return;

    assert(type_equal(actual, expected),
           "expression type doesn't match expected type at: " POS "\n",
           FMTPOS(&pos));
}

/*
 * Analyze both operands of an integer operator.
 *
 * The returned types are borrowed from the operands' AST nodes.
 * No operand is analyzed more than once.
 */
static void integer_operands(Allocator* alloc, AST* lhs, AST* rhs, Scope* scope,
                             Type* expected, Type** left, Type** right) {
    bool lhs_literal = is_integer_literal(lhs);
    bool rhs_literal = is_integer_literal(rhs);

    if (lhs_literal && rhs_literal) {
        TypeKind common;

        if (expected != NULL && type_is_integer(expected->kind)) {
            common = expected->kind;
        } else {
            TypeKind lk = infer_integer_type(lhs->data.int_literal.value);
            TypeKind rk = infer_integer_type(rhs->data.int_literal.value);
            common = common_integer_type(lk, rk);
        }

        assert(integer_fits(lhs->data.int_literal.value, common),
               "left integer literal doesn't fit inferred type\n");
        assert(integer_fits(rhs->data.int_literal.value, common),
               "right integer literal doesn't fit inferred type\n");

        Type target = {.kind = common};

        *left = expression(alloc, lhs, scope, &target);
        *right = expression(alloc, rhs, scope, &target);
        return;
    }

    if (lhs_literal) {
        /*
         * Analyze the non-literal first. Its type determines the type
         * expected by the literal.
         */
        *right = expression(alloc, rhs, scope, expected);

        Type* literal_expected =
            type_is_integer((*right)->kind) ? *right : NULL;

        *left = expression(alloc, lhs, scope, literal_expected);
        return;
    }

    if (rhs_literal) {
        *left = expression(alloc, lhs, scope, expected);

        Type* literal_expected = type_is_integer((*left)->kind) ? *left : NULL;

        *right = expression(alloc, rhs, scope, literal_expected);
        return;
    }

    *left = expression(alloc, lhs, scope, expected);
    *right = expression(alloc, rhs, scope, expected);
}

/*
 * Analyze a unary expression.
 *
 * The returned Type* is owned by the caller, which stores it in the
 * current expression's AST node. Operand expression types are owned
 * by their respective AST nodes and must not be freed here.
 */
static Type* unop(Allocator* alloc, AST* ast, Scope* scope, Type* expected) {
    AST* operand = ast->data.unop.operand;

    switch (ast->data.unop.op) {
    case UP_REF: {
        assert(is_lvalue(operand), "cannot take the address of a non-lvalue\n");

        /*
         * Address-of changes the type:
         *
         *     operand: i32
         *     result:  i32*
         *
         * If an expected pointer type exists, its child is the
         * expected type of the operand.
         */
        Type* operand_expected = NULL;

        if (expected != NULL && expected->kind == TYPE_POINTER)
            operand_expected = expected->data.pointer.child;

        Type* operand_type =
            expression(alloc, operand, scope, operand_expected);

        Type* result = make_type(alloc, TYPE_POINTER);
        result->data.pointer.child = type_clone(alloc, operand_type);

        return result;
    }

    case UP_DEREF: {
        /*
         * Dereference also changes the type:
         *
         *     operand: i32*
         *     result:  i32
         *
         * The expected result type must not be passed to the pointer
         * operand. The pointer expression has its own independent type.
         */
        Type* operand_type = expression(alloc, operand, scope, NULL);

        assert(operand_type->kind == TYPE_POINTER,
               "cannot dereference a non-pointer\n");

        return type_clone(alloc, operand_type->data.pointer.child);
    }

    case UP_POS: {
        Type unsigned_default = {0};
        Type* operand_expected = expected;

        /*
         * Unary plus is the unsigned syntax hint.
         *
         * An explicit expected type takes precedence over the default.
         */
        if (operand_expected == NULL && operand->kind == AST_INT_LITERAL) {
            unsigned_default.kind =
                infer_unsigned_integer_type(operand->data.int_literal.value);

            operand_expected = &unsigned_default;
        }

        Type* operand_type =
            expression(alloc, operand, scope, operand_expected);

        assert(type_is_integer(operand_type->kind),
               "unary plus requires an integer\n");

        return type_clone(alloc, operand_type);
    }

    case UP_NEG: {
        Type* operand_type = expression(alloc, operand, scope, expected);

        assert(type_is_integer(operand_type->kind),
               "unary minus requires an integer\n");

        return type_clone(alloc, operand_type);
    }

    case UP_LNOT: {
        Type* operand_type = expression(alloc, operand, scope, NULL);

        assert(operand_type->kind == TYPE_BOOL,
               "logical NOT requires a bool operand\n");

        return make_type(alloc, TYPE_BOOL);
    }

    default:
        assert(false, "unsupported unary operator\n");
        return NULL;
    }
}

static Type* binop(Allocator* alloc, AST* ast, Scope* scope, Type* expected) {
    AST* lhs = ast->data.binop.lhs;
    AST* rhs = ast->data.binop.rhs;

    BinOpKind op = ast->data.binop.op;

    if (op == OP_ASSIGN) {
        assert(is_assignable(lhs),
               "left operand of assignment must be assignable\n");

        Type* left = expression(alloc, lhs, scope, NULL);
        Type* right = expression(alloc, rhs, scope, left);

        assert(type_equal(left, right),
               "assignment operands must have matching types\n");

        return type_clone(alloc, left);
    }

    if (op == OP_LAND || op == OP_LOR) {
        Type* left = expression(alloc, lhs, scope, NULL);
        Type* right = expression(alloc, rhs, scope, NULL);

        assert(left->kind == TYPE_BOOL && right->kind == TYPE_BOOL,
               "logical operators require bool operands\n");

        return make_type(alloc, TYPE_BOOL);
    }

    bool integer_operator = op == OP_ADD || op == OP_SUB || op == OP_MUL ||
                            op == OP_DIV || op == OP_MOD || op == OP_SHL ||
                            op == OP_SHR || op == OP_BAND || op == OP_BOR ||
                            op == OP_XOR || op == OP_LT || op == OP_GT ||
                            op == OP_LE || op == OP_GE;

    if (integer_operator) {
        bool integer_result = op == OP_ADD || op == OP_SUB || op == OP_MUL ||
                              op == OP_DIV || op == OP_MOD || op == OP_SHL ||
                              op == OP_SHR || op == OP_BAND || op == OP_BOR ||
                              op == OP_XOR;

        Type* operand_expected = integer_result && expected != NULL &&
                                         type_is_integer(expected->kind)
                                     ? expected
                                     : NULL;

        Type* left;
        Type* right;

        integer_operands(alloc, lhs, rhs, scope, operand_expected, &left,
                         &right);

        assert(type_equal(left, right),
               "binary operator operands must have matching types\n");

        assert(type_is_integer(left->kind),
               "operator requires integer operands\n");

        switch (op) {
        case OP_LT:
        case OP_GT:
        case OP_LE:
        case OP_GE:
            return make_type(alloc, TYPE_BOOL);

        default:
            return type_clone(alloc, left);
        }
    }

    if (op == OP_EQ || op == OP_NE) {
        Type* left;
        Type* right;

        /*
         * Equality supports non-integer types too. Integer literals still
         * get the type of the opposite operand when possible.
         */
        if (is_integer_literal(lhs) && !is_integer_literal(rhs)) {
            right = expression(alloc, rhs, scope, NULL);
            left = expression(alloc, lhs, scope,
                              type_is_integer(right->kind) ? right : NULL);
        } else if (!is_integer_literal(lhs) && is_integer_literal(rhs)) {
            left = expression(alloc, lhs, scope, NULL);
            right = expression(alloc, rhs, scope,
                               type_is_integer(left->kind) ? left : NULL);
        } else {
            left = expression(alloc, lhs, scope, NULL);
            right = expression(alloc, rhs, scope, NULL);
        }

        assert(type_equal(left, right),
               "equality operands must have matching types\n");

        assert(type_is_integer(left->kind) || left->kind == TYPE_BOOL ||
                   left->kind == TYPE_CSTR || left->kind == TYPE_POINTER,
               "equality operators don't support these operands\n");

        return make_type(alloc, TYPE_BOOL);
    }

    assert(false, "unsupported binary operator\n");
    return NULL;
}

Type* expression(Allocator* alloc, AST* ast, Scope* scope, Type* expected) {
    assert(ast != NULL, "expected an expression\n");

    Pos pos = {ast->line, ast->col};

    /*
     * Expression types belong to their AST nodes.
     *
     * Once analyzed, an expression's type is reused. The AST is
     * responsible for freeing ast->type after compilation.
     */
    if (ast->type != NULL) {
        check_expected_type(ast->type, expected, pos);
        return ast->type;
    }

    switch (ast->kind) {
    case AST_INT_LITERAL: {
        TypeKind kind = infer_integer_type(ast->data.int_literal.value);

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
        ast->type = unop(alloc, ast, scope, expected);
        break;

    case AST_BINOP:
        ast->type = binop(alloc, ast, scope, expected);
        break;

    case AST_BOOL_LIT:
        ast->type = make_type(alloc, TYPE_BOOL);
        break;

    case AST_STR_LITERAL:
        ast->type = make_type(alloc, TYPE_CSTR);
        break;

    case AST_IDENT: {
        Symbol* symbol = get_symbol(scope, ast->data.ident.name, pos);
        assert(symbol->kind == SYMBOL_VALUE,
               "type name used as a value at: " POS "\n", FMTPOS(&pos));
        ast->type = type_clone(alloc, symbol->data.type);
        break;
    }

    case AST_FUNCTION_CALL: {
        Symbol* symbol = get_symbol(scope, ast->data.function_call.name, pos);

        assert(symbol->kind == SYMBOL_VALUE,
               "type name used as a function at: " POS "\n", FMTPOS(&pos));
        Type* function = symbol->data.type;

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

    case AST_STRUCT_INIT: {
        String name = ast->data.struct_init.name;
        Symbol* symbol = get_symbol(scope, name, pos);

        assert(symbol->kind == SYMBOL_TYPE,
               "'" STRING "' is not a struct type at: " POS "\n",
               FMTSTRING(&name), FMTPOS(&pos));

        ParameterList* fields = symbol->data.params;
        StructInitializerFields* initializers = ast->data.struct_init.fields;

        if (initializers != NULL) {
            for (size_t i = 0; i < initializers->len; i++) {
                StructInitializerField* initializer = &initializers->data[i];
                Parameter* field = NULL;

                for (size_t j = 0; j < fields->len; j++) {
                    if (string_compare(initializer->name,
                                       fields->data[j].name)) {
                        field = &fields->data[j];
                        break;
                    }
                }

                Pos field_pos = {initializer->value->line,
                                 initializer->value->col};
                assert(field != NULL,
                       "unknown field '" STRING "' in struct '" STRING
                       "' at: " POS "\n",
                       FMTSTRING(&initializer->name), FMTSTRING(&name),
                       FMTPOS(&field_pos));

                for (size_t j = 0; j < i; j++) {
                    assert(!string_compare(initializers->data[j].name,
                                           initializer->name),
                           "duplicate struct initializer field '" STRING
                           "' at: " POS "\n",
                           FMTSTRING(&initializer->name), FMTPOS(&field_pos));
                }

                Type* actual =
                    expression(alloc, initializer->value, scope, field->type);
                assert(type_equal(actual, field->type),
                       "initializer type mismatch for field '" STRING
                       "' at: " POS "\n",
                       FMTSTRING(&initializer->name), FMTPOS(&field_pos));
            }
        }

        ast->type = make_type(alloc, TYPE_USER_DEFINED);
        ast->type->data.user_defined.name = string_clone(&name, alloc);
        break;
    }

    default:
        assert(false, "unsupported expression node at: " POS "\n",
               FMTPOS(&pos));
    }

    check_expected_type(ast->type, expected, pos);
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

    case AST_STRUCT:
        /* Structs are registered during the block's declaration prepass. */
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

    /* Register types and functions before analyzing statements so later
     * declarations are visible throughout the block. */
    da_foreach(stmt, &ast->data.block.statements) {
        if ((*stmt)->kind == AST_STRUCT)
            register_struct(*stmt, scope);
    }

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
