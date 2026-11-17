#include "ast.h"
#include "lexer.h"
#include <inttypes.h>
AST* ast_parse_block(Allocator* alloc, Lexer* lex, bool toplevel, Pos pos);
AST* ast_parse_stmt(Allocator* alloc, Lexer* lexl);
ParameterList parse_params(Allocator* alloc, Lexer* lex, bool* variadic,
                           bool optional_name);

Token require_token(TokenKind token_kind, Lexer* lex) {
    Token token = getToken(lex);
    assert(token.kind == token_kind,
           "expected '" TOKEN "' got '" TOKEN "' at " POS "\n",
           FMTTOKENKIND(token_kind), FMTTOKEN(&token), FMTPOS(&token));
    return token;
}
Token peekToken(Lexer* lex) {
    Token token = getToken(lex);
    unreadToken(token, lex);
    return token;
}

void consume_required(TokenKind kind, Lexer* lex) {
    Token token = require_token(kind, lex);
    freeToken(&token);
}

bool optional_token(TokenKind token_kind, Lexer* lex) {
    Token token = getToken(lex);
    if (token.kind == token_kind) {
        freeToken(&token);
        return true;
    }
    unreadToken(token, lex);
    return false;
}

AST* ast_parse_expr(Allocator* alloc, Lexer* lex);
AST* ast_parse_primary_expr(Allocator* alloc, Lexer* lex);

AST* ast_parse_unop(Allocator* alloc, Lexer* lex) {
    Token token = getToken(lex);
    auto unop = token_to_unop[token.kind];
    if (unop.kind == UP_NULL)
        panic("unknown token '" TOKEN "' at " POS "\n", FMTTOKEN(&token),
              FMTPOS(&token));
    bool postfix = unop.postfix;
    UnOpKind kind = unop.kind;
    AST* operand = ast_parse_primary_expr(alloc, lex);

    AST* n = ALLOC(alloc, sizeof(AST));
    *n = (AST){
        SETPOS(&token),
        .alloc = alloc,
        .kind = AST_UNOP,
        .data.unop.postfix = postfix,
        .data.unop.operand = operand,
        .data.unop.op = kind,
    };

    freeToken(&token);
    return n;
}

AST* ast_parse_primary_expr(Allocator* alloc, Lexer* lex) {
    Token token = getToken(lex);
    switch (token.kind) {
    case TOKEN_INT_LIT: {
        char* cstr = from_string(&token.data, &DEFAULT_ALLOCATOR);

        u64 i = strtoull(cstr, NULL, token.base);

        FREE(&DEFAULT_ALLOCATOR, cstr);

        AST* n = ALLOC(alloc, sizeof(AST));
        *n = (AST){
            SETPOS(&token),
            .data.int_literal.value = i,
            .alloc = alloc,
            .kind = AST_INT_LITERAL,
        };

        freeToken(&token);
        return n;
    }
    case TOKEN_STR_LIT: {
        AST* n = ALLOC(alloc, sizeof(AST));
        *n = (AST){
            SETPOS(&token),
            .data.str_lit.value = string_clone(&token.data, alloc),
            .alloc = alloc,
            .kind = AST_STR_LITERAL,
        };
        freeToken(&token);
        return n;
    }
    case TOKEN_IDENT: {
        if (optional_token(TOKEN_OPEN_PAREN, lex)) {
            ASTList args = {0};
            da_init(&args, alloc);
            for (;;) {
                Token c = getToken(lex);

                if (c.kind == TOKEN_EOF)
                    panic("unexpected EOF\n");

                if (c.kind == TOKEN_CLOSE_PAREN) {
                    freeToken(&c);
                    break;
                }

                unreadToken(c, lex);

                da_append(&args, ast_parse_expr(alloc, lex));

                c = getToken(lex);

                if (c.kind == TOKEN_CLOSE_PAREN) {
                    freeToken(&c);
                    break;
                }

                if (c.kind != TOKEN_COMMA)
                    panic("expected ',' or ')' at " POS "\n", FMTPOS(&c));

                freeToken(&c);
            }
            AST* n = ALLOC(alloc, sizeof(AST));
            *n = (AST){
                SETPOS(&token),
                .data.function_call.name = string_clone(&token.data, alloc),
                .data.function_call.args = args,
                .alloc = alloc,
                .kind = AST_FUNCTION_CALL,
            };
            freeToken(&token);
            return n;
        } else {
            AST* n = ALLOC(alloc, sizeof(AST));
            *n = (AST){
                SETPOS(&token),
                .data.ident.name = string_clone(&token.data, alloc),
                .alloc = alloc,
                .kind = AST_IDENT,
            };
            freeToken(&token);
            return n;
        }
    }
    case TOKEN_TRUE: {
        AST* n = ALLOC(alloc, sizeof(AST));
        *n = (AST){SETPOS(&token), .alloc = alloc, .kind = AST_BOOL_LIT,
                   .data.bool_lit.value = true};
        freeToken(&token);
        return n;
    }
    case TOKEN_FALSE: {
        AST* n = ALLOC(alloc, sizeof(AST));
        *n = (AST){SETPOS(&token), .alloc = alloc, .kind = AST_BOOL_LIT,
                   .data.bool_lit.value = false};
        freeToken(&token);
        return n;
    }
    case TOKEN_OPEN_PAREN: {
        auto p = ast_parse_expr(alloc, lex);
        consume_required(TOKEN_CLOSE_PAREN, lex);
        freeToken(&token);
        return p;
    }
    default:
        unreadToken(token, lex);
        return ast_parse_unop(alloc, lex);
    }
}

int binop_precedence(BinOpKind op) {
    for (int i = 0; i < PRECEDENCE_COUNT; i++) {
        for (int j = 0; PRECEDENCE[i][j] != OP_NULL; j++) {
            if (PRECEDENCE[i][j] == op)
                return i;
        }
    }

    // if this reaches its likely an error on my part
    panic("binop '" TOKEN "' not defined in precedence",
          FMTTOKENKIND(binop_to_token[op]));
}

// need it to be signed
AST* ast_parse_binop(Allocator* alloc, Lexer* lex, int precedence) {
    if (precedence < 0)
        return ast_parse_primary_expr(alloc, lex);
    AST* lhs = ast_parse_binop(alloc, lex, precedence - 1);
    for (;;) {
        Token tok = getToken(lex);
        BinOpKind binop = token_to_binop[tok.kind];
        if (binop == OP_NULL) {
            unreadToken(tok, lex);
            break;
        }
        if (binop_precedence(binop) != precedence) {
            unreadToken(tok, lex);
            break;
        }
        AST* p = ALLOC(alloc, sizeof(AST));
        *p = (AST){
            SETPOS(&tok),
            .kind = AST_BINOP,
            .alloc = alloc,
            .data.binop.op = binop,
            .data.binop.lhs = lhs,
            .data.binop.rhs = ast_parse_binop(alloc, lex, precedence - 1),
        };
        freeToken(&tok);
        lhs = p;
    }
    return lhs;
}

AST* ast_parse_expr(Allocator* alloc, Lexer* lex) {
    return ast_parse_binop(alloc, lex, PRECEDENCE_COUNT - 1);
}

typedef struct {
    const char* name;
    TypeKind kind;
} TypeEntry;

#define MAP(name, type) {name, type},
static const TypeEntry BUILTIN_TYPES[] = {TYPE_KIND(MAP)};
#undef MAP

Type parse_type(Allocator* alloc, Lexer* lex) {
    Token token = getToken(lex);
    Type type = {
        .alloc = alloc,
    };
    bool found = false;
    if (token.kind == TOKEN_IDENT) {
        fa_foreach(d, BUILTIN_TYPES) {
            if (string_compare_cstr(token.data, d->name)) {
                type.kind = d->kind;
                found = true;
                break;
            }
        }
    } else if (token.kind == TOKEN_FN) {
        found = true;
        Token conv = getToken(lex);
        FnConv convention;
        if (conv.kind == TOKEN_IDENT && string_compare_cstr(conv.data, "C")) {
            convention = CONV_C;
            freeToken(&conv);
        } else {
            convention = CONV_LUT;
            unreadToken(conv, lex);
        }
        consume_required(TOKEN_OPEN_PAREN, lex);
        bool variadic = false;
        ParameterList params = parse_params(
            alloc, lex, convention == CONV_C ? &variadic : NULL, true);
        Type* ret_type = ALLOC(alloc, sizeof(Type));
        if (optional_token(TOKEN_ARROW, lex)) {
            *ret_type = parse_type(alloc, lex);
        } else {
            *ret_type = (Type){.kind = TYPE_VOID, .alloc = alloc};
        }
        type.kind = TYPE_FUNCTION;
        type.data.function = (typeof(type.data.function)){
            .params = params,
            .return_type = ret_type,
            .conv = convention,
            .va_args = variadic,
        };
    } else if (token.kind == TOKEN_OPEN_PAREN) {
        found = true;
        type = parse_type(alloc, lex);
        consume_required(TOKEN_CLOSE_PAREN, lex);
    }
    assert(found, "unknown type '" STRING "' at " POS "\n",
           FMTSTRING(&token.data), FMTPOS(&token));
    freeToken(&token);
    while (optional_token(TOKEN_STAR, lex)) {
        Type* temp = ALLOC(alloc, sizeof(Type));
        *temp = type;
        type.kind = TYPE_POINTER;
        type.data.pointer.child = temp;
    }

    return type;
}

ParameterList parse_params(Allocator* alloc, Lexer* lex, bool* variadic,
                           bool optional_name) {
    ParameterList params = {0};
    da_init(&params, alloc);

    if (variadic)
        *variadic = false;

    while (peekToken(lex).kind != TOKEN_CLOSE_PAREN) {
        if (optional_token(TOKEN_VA_ARGS, lex)) {
            assert(variadic,
                   "variadic parameters are only allowed in C declarations\n");
            *variadic = true;
            break;
        }

        Token ident = getToken(lex);
        String name = {0};

        if (ident.kind == TOKEN_IDENT && optional_token(TOKEN_COLON, lex)) {
            name = string_clone(&ident.data, alloc);
            freeToken(&ident);
        } else {
            assert(optional_name, "expected parameter name followed by ':'\n");
            unreadToken(ident, lex);
        }

        Type* type = ALLOC(alloc, sizeof(Type));
        *type = parse_type(alloc, lex);

        da_append(&params, ((Parameter){
                               .name = name,
                               .type = type,
                           }));

        if (!optional_token(TOKEN_COMMA, lex))
            break;
    }

    consume_required(TOKEN_CLOSE_PAREN, lex);
    return params;
}

AST* parse_fn(Allocator* alloc, Lexer* lex, Pos pos) {
    Token ident = require_token(TOKEN_IDENT, lex);
    String name = string_clone(&ident.data, alloc);
    freeToken(&ident);
    consume_required(TOKEN_OPEN_PAREN, lex);
    // TASK(20261112-140400-119-n6-8118a53c): add argument support
    ParameterList params = parse_params(alloc, lex, NULL, false);
    Type* ret_type = ALLOC(alloc, sizeof(Type));
    if (optional_token(TOKEN_ARROW, lex)) {
        *ret_type = parse_type(alloc, lex);
    } else {
        *ret_type = (Type){.kind = TYPE_VOID, .alloc = alloc};
    }
    optional_token(TOKEN_COLON, lex);
    AST* p = ALLOC(alloc, sizeof(AST));
    *p = (AST){
        SETPOS(&pos),
        .kind = AST_FUNCTION,
        .alloc = alloc,
        .data.function.name = name,
        .data.function.return_type = ret_type,
        .data.function.body = ast_parse_stmt(alloc, lex),
        .data.function.params = params,
    };
    return p;
}

AST* parse_extern(Allocator* alloc, Lexer* lex, Pos pos) {
    Token convention = getToken(lex);
    assert(convention.kind == TOKEN_IDENT &&
               string_compare_cstr(convention.data, "C"),
           "currently only c convention extern functions are supported at " POS
           "\n",
           FMTPOS(&pos));
    freeToken(&convention);
    consume_required(TOKEN_FN, lex);
    Token ident = require_token(TOKEN_IDENT, lex);
    String name = string_clone(&ident.data, alloc);
    freeToken(&ident);
    consume_required(TOKEN_OPEN_PAREN, lex);
    bool va_args;
    ParameterList params = parse_params(alloc, lex, &va_args, true);
    Type* ret_type = ALLOC(alloc, sizeof(Type));
    if (optional_token(TOKEN_ARROW, lex)) {
        *ret_type = parse_type(alloc, lex);
    } else {
        *ret_type = (Type){.kind = TYPE_VOID, .alloc = alloc};
    }
    String symbol = {};
    if (optional_token(TOKEN_AT, lex)) {
        Token attr = require_token(TOKEN_IDENT, lex);
        assert(string_compare_cstr(attr.data, "symbol"),
               "unknown attribute '" STRING "' at " POS "\n",
               FMTSTRING(&attr.data), FMTPOS(&attr));
        freeToken(&attr);
        consume_required(TOKEN_OPEN_PAREN, lex);
        Token str = require_token(TOKEN_STR_LIT, lex);
        symbol = string_clone(&str.data, alloc);
        freeToken(&str);
        consume_required(TOKEN_CLOSE_PAREN, lex);
    }
    consume_required(TOKEN_SEMI, lex);
    AST* p = ALLOC(alloc, sizeof(AST));
    *p = (AST){
        SETPOS(&pos),
        .kind = AST_FUNCTION_DECL,
        .alloc = alloc,
        .data.function_decl.name = name,
        .data.function_decl.return_type = ret_type,
        .data.function_decl.va_args = va_args,
        .data.function_decl.params = params,
        .data.function_decl.symbol = symbol,
        .data.function_decl.conv = CONV_C,
    };
    return p;
}

AST* parse_var_decl(Allocator* alloc, Lexer* lex, Pos pos) {
    Token ident = require_token(TOKEN_IDENT, lex);
    String name = string_clone(&ident.data, alloc);
    freeToken(&ident);
    Type* type = ALLOC(alloc, sizeof(Type));
    if (optional_token(TOKEN_COLON, lex)) {
        *type = parse_type(alloc, lex);
    } else {
        *type = (Type){
            .alloc = alloc,
            .kind = TYPE_INFER,
        };
    }
    AST* ast = NULL;
    if (optional_token(TOKEN_EQUALS, lex)) {
        ast = ast_parse_expr(alloc, lex);
    }
    consume_required(TOKEN_SEMI, lex);

    AST* p = ALLOC(alloc, sizeof(AST));
    *p = (AST){
        SETPOS(&pos),
        .kind = AST_VARIABLE_DECLARATION,
        .alloc = alloc,
        .data.var_decl.name = name,
        .data.var_decl.type = type,
        .data.var_decl.val = ast,
    };
    return p;
}

AST* ast_parse_stmt(Allocator* alloc, Lexer* lex) {
    Token token = getToken(lex);
    switch (token.kind) {
    case TOKEN_WHILE: {
        AST* cond = ast_parse_expr(alloc, lex);
        AST* body = ast_parse_stmt(alloc, lex);
        AST* p = ALLOC(alloc, sizeof(AST));
        *p = (AST){
            SETPOS(&token),
            .kind = AST_WHILE,
            .alloc = alloc,
            .data.while_loop.cond = cond,
            .data.while_loop.body = body,
        };
        freeToken(&token);
        return p;
    }
    case TOKEN_IF: {
        AST* cond = ast_parse_expr(alloc, lex);
        AST* then = ast_parse_stmt(alloc, lex);
        AST* el = NULL;
        if (optional_token(TOKEN_ELSE, lex)) {
            el = ast_parse_stmt(alloc, lex);
        }
        AST* p = ALLOC(alloc, sizeof(AST));
        *p = (AST){
            SETPOS(&token),
            .kind = AST_IF,
            .alloc = alloc,
            .data.if_stmt.cond = cond,
            .data.if_stmt.then = then,
            .data.if_stmt.el = el,
        };
        freeToken(&token);
        return p;
    }
    case TOKEN_OPEN_BRACE:
        auto block = ast_parse_block(alloc, lex, false, ASPOS(&token));
        freeToken(&token);
        return block;
    case TOKEN_RETURN: {
        if (optional_token(TOKEN_SEMI, lex)) {
            AST* p = ALLOC(alloc, sizeof(AST));
            *p = (AST){
                SETPOS(&token),
                .kind = AST_RETURN,
                .alloc = alloc,
                .data.return_stmt.value = NULL,
            };
            freeToken(&token);
            return p;
        } else {
            AST* expr = ast_parse_expr(alloc, lex);
            consume_required(TOKEN_SEMI, lex);
            AST* p = ALLOC(alloc, sizeof(AST));
            *p = (AST){
                SETPOS(&token),
                .kind = AST_RETURN,
                .alloc = alloc,
                .data.return_stmt.value = expr,
            };
            freeToken(&token);
            return p;
        }
    }
    default:
        unreadToken(token, lex);
        AST* expr = ast_parse_expr(alloc, lex);
        consume_required(TOKEN_SEMI, lex);
        AST* p = ALLOC(alloc, sizeof(AST));
        *p = (AST){
            SETPOS(expr),
            .kind = AST_EXPR_STMT,
            .alloc = alloc,
            .data.expr_stmt.value = expr,
        };
        return p;
    }
}

AST* ast_parse_decl(Allocator* alloc, Lexer* lex, bool toplevel) {
    Token token = getToken(lex);
    switch (token.kind) {
    case TOKEN_FN:
        assert(toplevel, "function declarations must be at toplevel\n");
        auto fn = parse_fn(alloc, lex, ASPOS(&token));
        freeToken(&token);
        return fn;
    case TOKEN_EXTERN:
        assert(toplevel, "extern symbols must be at toplevel\n");
        auto extern_v = parse_extern(alloc, lex, ASPOS(&token));
        freeToken(&token);
        return extern_v;
    case TOKEN_LET:
        auto decl = parse_var_decl(alloc, lex, ASPOS(&token));
        freeToken(&token);
        return decl;
    default:
        unreadToken(token, lex);
        return ast_parse_stmt(alloc, lex);
    }
}

AST* ast_parse_block(Allocator* alloc, Lexer* lex, bool toplevel, Pos pos) {
    ASTList statements = {0};
    da_init(&statements, alloc);

    for (;;) {
        Token token = getToken(lex);
        if (token.kind == TOKEN_EOF) {
            assert(toplevel, "unexpected EOF inside block\n");
            freeToken(&token);
            break;
        }
        if (token.kind == TOKEN_SEMI) {
            freeToken(&token);
            continue;
        }

        if (!toplevel && token.kind == TOKEN_CLOSE_BRACE) {
            freeToken(&token);
            break;
        }
        unreadToken(token, lex);
        da_append(&statements, ast_parse_decl(alloc, lex, toplevel));
    }

    AST* p = ALLOC(alloc, sizeof(AST));
    *p = (AST){
        SETPOS(&pos),
        .alloc = alloc,
        .kind = AST_BLOCK,
        .data.block.statements = statements,
        .data.block.toplevel = toplevel,
    };
    return p;
}

AST* ast_parse_toplevel(Allocator* alloc, Lexer* lex) {
    return ast_parse_block(alloc, lex, true, (Pos){0, 0});
}

static void print_escaped_string(String str) {
    putchar('"');

    for (size_t i = 0; i < str.len; i++) {
        switch (str.data[i]) {
        case '\n':
            printf("\\n");
            break;
        case '\t':
            printf("\\t");
            break;
        case '\r':
            printf("\\r");
            break;
        case '\0':
            printf("\\0");
            break;
        case '\\':
            printf("\\\\");
            break;
        case '"':
            printf("\\\"");
            break;
        default:
            putchar(str.data[i]);
            break;
        }
    }

    putchar('"');
}

void type_display(Type* type) {
    if (type->kind == TYPE_POINTER) {
        type_display(type->data.pointer.child);
        printf("*");
        return;
    } else if (type->kind == TYPE_FUNCTION) {
        printf("(fn ");
        if (type->data.function.conv == CONV_C) {
            printf("C ");
        }
        printf("(");
        da_foreach_enum(i, param, &type->data.function.params) {
            if (i != 0)
                printf(", ");
            if (param->name.data == NULL) {
                type_display(param->type);
            } else {
                printf(STRING ": ", FMTSTRING(&param->name));
                type_display(param->type);
            }
        }
        printf(") -> ");
        type_display(type->data.function.return_type);
        printf(")");
        return;
    }
    printf("%s", TYPEKIND[type->kind]);
}

void ast_display_i(AST* ast, size_t depth) {
    if (ast == NULL)
        return;

    for (size_t i = 0; i < depth; i++)
        printf("    ");

    switch (ast->kind) {
    case AST_UNOP:
        // TASK(20261117-000114-418-n6-72f292f8): make printing also show if its
        // postfix
        printf("OP '" TOKEN "': " POS "\n",
               FMTTOKENKIND(unop_to_token[ast->data.unop.op]), FMTPOS(ast));
        ast_display_i(ast->data.unop.operand, depth + 1);
        break;
    case AST_BINOP:
        printf("OP '" TOKEN "': " POS "\n",
               FMTTOKENKIND(binop_to_token[ast->data.binop.op]), FMTPOS(ast));
        ast_display_i(ast->data.binop.lhs, depth + 1);
        ast_display_i(ast->data.binop.rhs, depth + 1);
        break;
    case AST_WHILE:
        printf("WHILE: " POS "\n", FMTPOS(ast));
        ast_display_i(ast->data.while_loop.cond, depth + 1);
        for (size_t i = 0; i < depth; i++)
            printf("    ");
        printf("DO\n");
        ast_display_i(ast->data.while_loop.body, depth + 1);
        break;
    case AST_IF:
        printf("IF: " POS "\n", FMTPOS(ast));
        ast_display_i(ast->data.if_stmt.cond, depth + 1);
        for (size_t i = 0; i < depth; i++)
            printf("    ");
        printf("THEN\n");
        ast_display_i(ast->data.if_stmt.then, depth + 1);
        if (ast->data.if_stmt.el) {
            for (size_t i = 0; i < depth; i++)
                printf("    ");
            printf("ELSE\n");
            ast_display_i(ast->data.if_stmt.el, depth + 1);
        }
        break;
    case AST_VARIABLE_DECLARATION:
        printf("VAR " STRING ": ", FMTSTRING(&ast->data.var_decl.name));
        type_display(ast->data.var_decl.type);
        printf(": " POS "\n", FMTPOS(ast));

        ast_display_i(ast->data.var_decl.val, depth + 1);
        break;

    case AST_FUNCTION_CALL:
        printf("Function Call: " STRING ": " POS "\n",
               FMTSTRING(&ast->data.function_call.name), FMTPOS(ast));

        da_foreach(expr, &ast->data.function_call.args) {
            ast_display_i(*expr, depth + 1);
        }
        break;

    case AST_FUNCTION:
        printf("Function: " STRING "(", FMTSTRING(&ast->data.function.name));

        da_foreach_enum(i, param, &ast->data.function.params) {
            if (i != 0)
                printf(", ");

            printf(STRING ": ", FMTSTRING(&param->name));
            type_display(param->type);
        }

        printf(") -> ");
        type_display(ast->data.function.return_type);
        printf(": " POS "\n", FMTPOS(ast));

        ast_display_i(ast->data.function.body, depth + 1);
        break;

    case AST_FUNCTION_DECL:
        printf("Extern Function " CONV ": " STRING "(",
               FMTCONV(ast->data.function_decl.conv),
               FMTSTRING(&ast->data.function_decl.name));

        da_foreach_enum(i, param, &ast->data.function_decl.params) {
            if (i != 0)
                printf(", ");

            if (param->name.data == NULL) {
                type_display(param->type);
            } else {
                printf(STRING ": ", FMTSTRING(&param->name));
                type_display(param->type);
            }
        }

        if (ast->data.function_decl.va_args) {
            if (ast->data.function_decl.params.len != 0)
                printf(", ");
            printf("...");
        }

        printf(") -> ");
        type_display(ast->data.function_decl.return_type);
        if (ast->data.function_decl.symbol.data == NULL) {
            printf(" @symbol(\"" STRING "\"): " POS "\n",
                   FMTSTRING(&ast->data.function_decl.name), FMTPOS(ast));
        } else {
            printf(" @symbol(\"" STRING "\"): " POS "\n",
                   FMTSTRING(&ast->data.function_decl.symbol), FMTPOS(ast));
        }
        break;

    case AST_RETURN:
        printf("Return: " POS "\n", FMTPOS(ast));
        ast_display_i(ast->data.return_stmt.value, depth + 1);
        break;

    case AST_INT_LITERAL:
        printf("Integer: %" PRIu64 ": " POS "\n", ast->data.int_literal.value,
               FMTPOS(ast));
        break;

    case AST_BOOL_LIT:
        printf("BOOL: %s: " POS "\n",
               ast->data.bool_lit.value ? "true" : "false", FMTPOS(ast));
        break;

    case AST_IDENT:
        printf("IDENT '" STRING "': " POS "\n",
               FMTSTRING(&ast->data.ident.name), FMTPOS(ast));
        break;

    case AST_STR_LITERAL:
        printf("String ");
        print_escaped_string(ast->data.str_lit.value);
        printf(": " POS "\n", FMTPOS(ast));
        break;

    case AST_BLOCK:
        printf("Block%s: " POS "\n",
               ast->data.block.toplevel ? " (toplevel)" : "", FMTPOS(ast));

        da_foreach(stmt, &ast->data.block.statements) {
            ast_display_i(*stmt, depth + 1);
        }
        break;

    case AST_EXPR_STMT:
        printf("ExprStmt: " POS "\n", FMTPOS(ast));
        ast_display_i(ast->data.expr_stmt.value, depth + 1);
        break;
    }
}

void ast_display(AST* ast) { ast_display_i(ast, 0); }

void type_free(Type* type) {
    if (type == NULL)
        return;
    if (type->kind == TYPE_POINTER) {
        type_free(type->data.pointer.child);
    } else if (type->kind == TYPE_FUNCTION) {
        type_free(type->data.function.return_type);
        da_free_call(&type->data.function.params, b, {
            da_free(&b->name);
            type_free(b->type);
        });
    }
    FREE(type->alloc, type);
}

void ast_free(AST* ast) {
    if (ast == NULL)
        return;
    switch (ast->kind) {
    case AST_UNOP:
        ast_free(ast->data.unop.operand);
        break;
    case AST_BINOP:
        ast_free(ast->data.binop.lhs);
        ast_free(ast->data.binop.rhs);
        break;
    case AST_IF:
        ast_free(ast->data.if_stmt.cond);
        ast_free(ast->data.if_stmt.then);
        ast_free(ast->data.if_stmt.el);
        break;
    case AST_WHILE:
        ast_free(ast->data.while_loop.cond);
        ast_free(ast->data.while_loop.body);
        break;
    case AST_VARIABLE_DECLARATION:
        da_free(&ast->data.var_decl.name);
        ast_free(ast->data.var_decl.val);
        type_free(ast->data.var_decl.type);
        break;
    case AST_FUNCTION_CALL:
        da_free(&ast->data.function_call.name);
        da_free_call(&ast->data.function_call.args, arg, { ast_free(*arg); });
        break;

    case AST_FUNCTION:
        da_free(&ast->data.function.name);
        da_free_call(&ast->data.function.params, b, {
            da_free(&b->name);
            type_free(b->type);
        });
        type_free(ast->data.function.return_type);
        ast_free(ast->data.function.body);
        free_scope(ast->data.function.scope);
        break;
    case AST_RETURN:
        ast_free(ast->data.return_stmt.value);
        break;
    case AST_EXPR_STMT:
        ast_free(ast->data.expr_stmt.value);
        break;
    case AST_IDENT:
        da_free(&ast->data.ident.name);
        break;
    case AST_STR_LITERAL:
        da_free(&ast->data.str_lit.value);
        break;
    case AST_INT_LITERAL:
    case AST_BOOL_LIT:
        // nothing to do
        break;
    case AST_BLOCK:
        da_free_call(&ast->data.block.statements, stmt, { ast_free(*stmt); });
        free_scope(ast->data.block.scope);
        break;
    case AST_FUNCTION_DECL:
        da_free(&ast->data.function_decl.name);
        da_free(&ast->data.function_decl.symbol);
        da_free_call(&ast->data.function_decl.params, b, {
            da_free(&b->name);
            type_free(b->type);
        });
        type_free(ast->data.function_decl.return_type);
        free_scope(ast->data.function_decl.scope);
        break;
    }
    type_free(ast->type);
    FREE(ast->alloc, ast);
}
