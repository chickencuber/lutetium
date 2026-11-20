#pragma once
#include "aids.h"
#include <stddef.h>
#include <stdint.h>
#define SETPOS(p) .col = (p)->col, .line = (p)->line
#define ASPOS(p)                                                               \
    (Pos) { SETPOS(p) }
typedef struct Pos Pos;
struct Pos {
    size_t line, col;
};

#define POS "%zu:%zu"
#define FMTPOS(p) (p)->line, (p)->col

#define TOKEN "%s"
#define FMTTOKEN(p) TOKENKIND[(p)->kind]
#define FMTTOKENKIND(p) TOKENKIND[p]

#define TOKEN_BUFFER_SIZE 5

#define OTHER_TOKEN_KINDS(X)                                                   \
    X("IF you have this token I dont know wtf you did", TOKEN_NULL)            \
    X("EOF", TOKEN_EOF)                                                        \
    X("Ident", TOKEN_IDENT)                                                    \
    X("String", TOKEN_STR_LIT)                                                 \
    X("Integer", TOKEN_INT_LIT)

#define SYMBOL_LIST(X)                                                         \
    X("(", TOKEN_OPEN_PAREN)                                                   \
    X(")", TOKEN_CLOSE_PAREN)                                                  \
    X("{", TOKEN_OPEN_BRACE)                                                   \
    X("}", TOKEN_CLOSE_BRACE)                                                  \
    X("->", TOKEN_ARROW)                                                       \
    X("*", TOKEN_STAR)                                                         \
    X(";", TOKEN_SEMI)                                                         \
    X(",", TOKEN_COMMA)                                                        \
    X(":", TOKEN_COLON)                                                        \
    X("...", TOKEN_VA_ARGS)                                                    \
    X("+", TOKEN_PLUS)                                                         \
    X("/", TOKEN_SLASH)                                                        \
    X("%", TOKEN_PER)                                                          \
    X("-", TOKEN_MINUS)                                                        \
    X("<<", TOKEN_SHL)                                                         \
    X(">>", TOKEN_SHR)                                                         \
    X("<", TOKEN_LT)                                                           \
    X(">", TOKEN_GT)                                                           \
    X("<=", TOKEN_LE)                                                          \
    X(">=", TOKEN_GE)                                                          \
    X("==", TOKEN_EQ)                                                          \
    X("!=", TOKEN_NE)                                                          \
    X("=", TOKEN_EQUALS)                                                       \
    X("&", TOKEN_BAND)                                                         \
    X("|", TOKEN_PIPE)                                                         \
    X("^", TOKEN_CARET)                                                        \
    X("&&", TOKEN_LAND)                                                        \
    X("||", TOKEN_OR)                                                          \
    X("@", TOKEN_AT)                                                           \
    X("!", TOKEN_EXC)                                                          \
    X(".", TOKEN_DOT)

#define KEYWORD_LIST(X)                                                        \
    X("fn", TOKEN_FN)                                                          \
    X("extern", TOKEN_EXTERN)                                                  \
    X("true", TOKEN_TRUE)                                                      \
    X("false", TOKEN_FALSE)                                                    \
    X("let", TOKEN_LET)                                                        \
    X("return", TOKEN_RETURN)                                                  \
    X("if", TOKEN_IF)                                                          \
    X("while", TOKEN_WHILE)                                                    \
    X("else", TOKEN_ELSE)                                                      \
    X("struct", TOKEN_STRUCT)

#define KV_ENUM_VALUE(_, kind) kind,
typedef enum TokenKind {
    OTHER_TOKEN_KINDS(KV_ENUM_VALUE) SYMBOL_LIST(KV_ENUM_VALUE)
        KEYWORD_LIST(KV_ENUM_VALUE) TOKEN_COUNT,
} TokenKind;
#undef KV_ENUM_VALUE

#define KV_ENUM_STRING(name, _) name,
static const char* TOKENKIND[] = {OTHER_TOKEN_KINDS(KV_ENUM_STRING)
                                      SYMBOL_LIST(KV_ENUM_STRING)
                                          KEYWORD_LIST(KV_ENUM_STRING)

};
#undef KV_ENUM_STRING

static const char SYMBOL_CHARS[] = "!@#$%^&*()-+=|\\]}[{\"'?/>.<,:;";

typedef struct TrieNode TrieNode;
struct TrieNode {
    bool terminal;
    TrieNode* children[sizeof(SYMBOL_CHARS) - 1];
    TokenKind kind;
};
typedef struct Token Token;
struct Token {
    size_t line, col;
    String data;
    TokenKind kind;
    int base;
};

typedef struct Lexer Lexer;
struct Lexer {
    Allocator* alloc;
    FILE* fd;
    size_t line, col;
    TrieNode* trie;
    bool eof;
    Token unread[TOKEN_BUFFER_SIZE];
    size_t count;
};

Lexer createLexer(FILE*, Allocator*);
Token getToken(Lexer*);
void unreadToken(Token, Lexer*);
void freeLexer(Lexer* p);

void freeToken(Token*);
