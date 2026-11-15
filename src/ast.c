#include "ast.h"
#include "lexer.h"
#include <inttypes.h>
AST* ast_parse_block(Allocator* alloc, Lexer* lex, bool toplevel, Pos pos);

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

u64 parse_u64(Str str) {
    u64 result = 0;

    for (size_t i = 0; i < str.len; i++) {
        char c = str.data[i];
        assert(c >= '0' && c <= '9', "invalid digit\n");

        u64 digit = (u64)(c - '0');
        assert(result <= (UINT64_MAX - digit) / 10,
               "integer literal exceeds u64\n");

        result = result * 10 + digit;
    }

    return result;
}

AST* ast_parse_expr(Allocator* alloc, Lexer* lex);

AST* ast_parse_primary_expr(Allocator* alloc, Lexer* lex) {
    Token token = getToken(lex);
    switch (token.kind) {
    case TOKEN_INT_LIT: {
        Str num = string_slice(&token.data);
        u64 i = parse_u64(num);
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
                    panic("expected ',' or ')'\n");

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
    default:
        panic("unknown token '" TOKEN "' at " POS "\n", FMTTOKEN(&token),
              FMTPOS(&token));
    }
}

AST* ast_parse_expr(Allocator* alloc, Lexer* lex) {
    return ast_parse_primary_expr(alloc, lex);
}

typedef struct {
    const char* name;
    TypeKind kind;
} TypeEntry;

#define MAP(name, type) {name, type},
static const TypeEntry BUILTIN_TYPES[] = {TYPE_KIND(MAP)};
#undef MAP

Type parse_type(Lexer* lex) {
    Token token = require_token(TOKEN_IDENT, lex);
    bool found = false;
    Type type = {0};
    fa_foreach(d, BUILTIN_TYPES) {
        if (string_compare_cstr(token.data, d->name)) {
            type.kind = d->kind;
            found = true;
            break;
        }
    }
    assert(found, "unknown type '" STRING "' at " POS "\n",
           FMTSTRING(&token.data), FMTPOS(&token));
    freeToken(&token);
    while (optional_token(TOKEN_STAR, lex)) {
        type.depth++;
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
        if (peekToken(lex).kind == TOKEN_VA_ARGS) {
            assert(variadic, "variadic parameters are only allowed in C declarations\n");
            getToken(lex);
            *variadic = true;
            break;
        }

        Token ident = getToken(lex);
        String name = {0};

        if (ident.kind == TOKEN_IDENT && peekToken(lex).kind == TOKEN_COLON) {
            name = string_clone(&ident.data, alloc);
            consume_required(TOKEN_COLON, lex);
            freeToken(&ident);
        } else {
            assert(optional_name, "expected parameter name followed by ':'\n");
            unreadToken(ident, lex);
        }

        Parameter param = {
            .name = name,
            .type = parse_type(lex),
        };

        da_append(&params, param);

        if (peekToken(lex).kind != TOKEN_COMMA)
            break;

        consume_required(TOKEN_COMMA, lex);
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
    Type ret_type;
    if (optional_token(TOKEN_ARROW, lex)) {
        ret_type = parse_type(lex);
    } else {
        ret_type = (Type){.kind = TYPE_VOID, .depth = 0};
    }
    Token t = require_token(TOKEN_OPEN_BRACE, lex);
    AST* p = ALLOC(alloc, sizeof(AST));
    *p = (AST){
        SETPOS(&pos),
        .kind = AST_FUNCTION,
        .alloc = alloc,
        .data.function.name = name,
        .data.function.return_type = ret_type,
        .data.function.body =
            ast_parse_block(alloc, lex, false, (Pos){SETPOS(&t)}),
        .data.function.params = params,
    };
    freeToken(&t);
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
    Type ret_type;
    if (optional_token(TOKEN_ARROW, lex)) {
        ret_type = parse_type(lex);
    } else {
        ret_type = (Type){.kind = TYPE_VOID, .depth = 0};
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
        .data.function_decl.conv = CONV_C,
    };
    return p;
}

AST* parse_var_decl(Allocator* alloc, Lexer* lex, Pos pos) {
    Token ident = require_token(TOKEN_IDENT, lex);
    String name = string_clone(&ident.data, alloc);
    freeToken(&ident);
    consume_required(TOKEN_COLON, lex);
    Type type = parse_type(lex);
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

AST* ast_parse_stmt(Allocator* alloc, Lexer* lex, bool toplevel) {
    Token token = getToken(lex);
    switch (token.kind) {
    case TOKEN_FN:
        assert(toplevel, "function declarations must be at toplevel\n");
        auto fn = parse_fn(alloc, lex, (Pos){SETPOS(&token)});
        freeToken(&token);
        return fn;
    case TOKEN_EXTERN:
        assert(toplevel, "extern symbols must be at toplevel\n");
        auto extern_v = parse_extern(alloc, lex, (Pos){SETPOS(&token)});
        freeToken(&token);
        return extern_v;
    case TOKEN_LET:
        auto decl = parse_var_decl(alloc, lex, (Pos){SETPOS(&token)});
        freeToken(&token);
        return decl;
    case TOKEN_RETURN: {
        assert(!toplevel, "return statements must not be at toplevel\n");
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
        assert(!toplevel, "expression statments must not be at toplevel\n");
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
        da_append(&statements, ast_parse_stmt(alloc, lex, toplevel));
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

void ast_display_i(AST* ast, size_t depth) {
    if (ast == NULL)
        return;
    for (size_t i = 0; i < depth; i++)
        printf("    ");

    switch (ast->kind) {
    case AST_VARIABLE_DECLARATION:
        printf("VAR " STRING ": " TYPE ": " POS "\n",
               FMTSTRING(&ast->data.var_decl.name),
               FMTTYPE(&ast->data.var_decl.type), FMTPOS(ast));
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

        for (size_t i = 0; i < ast->data.function.params.len; i++) {
            Parameter* param = &ast->data.function.params.data[i];

            if (i != 0)
                printf(", ");

            printf(STRING ": " TYPE, FMTSTRING(&param->name),
                   FMTTYPE(&param->type));
        }

        printf(") -> " TYPE ": " POS "\n",
               FMTTYPE(&ast->data.function.return_type), FMTPOS(ast));
        ast_display_i(ast->data.function.body, depth + 1);
        break;
    case AST_FUNCTION_DECL:
        printf("Extern Function " CONV ": " STRING "(",
               FMTCONV(ast->data.function_decl.conv),
               FMTSTRING(&ast->data.function_decl.name));

        for (size_t i = 0; i < ast->data.function_decl.params.len; i++) {
            Parameter* param = &ast->data.function_decl.params.data[i];

            if (i != 0)
                printf(", ");

            if (param->name.data == NULL) {
                printf(TYPE, FMTTYPE(&param->type));
            } else {
                printf(STRING ": " TYPE, FMTSTRING(&param->name),
                       FMTTYPE(&param->type));
            }
        }

        printf(") -> " TYPE ": " POS "\n",
               FMTTYPE(&ast->data.function_decl.return_type), FMTPOS(ast));
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

void ast_free(AST* ast) {
    if (ast == NULL)
        return;
    switch (ast->kind) {
    case AST_VARIABLE_DECLARATION:
        da_free(&ast->data.var_decl.name);
        ast_free(ast->data.var_decl.val);
        break;
    case AST_FUNCTION_CALL:
        da_free(&ast->data.function_call.name);
        da_free_call(&ast->data.function_call.args, arg, { ast_free(*arg); });
        break;

    case AST_FUNCTION:
        da_free(&ast->data.function.name);
        da_free_call(&ast->data.function.params, b, { da_free(&b->name); });
        ast_free(ast->data.function.body);
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
        break;
    case AST_FUNCTION_DECL:
        da_free(&ast->data.function_decl.name);
        da_free_call(&ast->data.function_decl.params, b,
                     { da_free(&b->name); });
        break;
    }
    FREE(ast->alloc, ast);
}
