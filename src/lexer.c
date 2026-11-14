#include "lexer.h"
#include "aids.h"
#include <ctype.h>
#include <stdio.h>

typedef struct Keyword Keyword;
struct Keyword {
    const char* name;
    TokenKind kind;
};

#define VALUE(name, kind) {name, kind},
static Keyword KEYWORDS[] = {KEYWORD_LIST(VALUE)};
#undef VALUE

int symbolIndex(char ch) {
    for (size_t i = 0; i < sizeof(SYMBOL_CHARS) - 1; i++) {
        if (SYMBOL_CHARS[i] == ch)
            return (int)i;
    }
    return -1;
}
TrieNode* getNext(TrieNode* trie, char ch) {
    int i = symbolIndex(ch);
    if (i == -1) {
        return NULL;
    }
    return trie->children[i];
}

static TrieNode* trie_node_create(Allocator* alloc) {
    TrieNode* node = CALLOC(alloc, 1, sizeof(TrieNode));
    assert(node != NULL, "Failed to allocate trie node\n");
    return node;
}
static void trie_insert(TrieNode* root, const char* symbol, TokenKind kind,
                        Allocator* alloc) {
    TrieNode* node = root;

    for (size_t i = 0; symbol[i] != '\0'; i++) {
        int index = symbolIndex(symbol[i]);

        assert(index >= 0, "Invalid symbol character: %c\n", symbol[i]);

        if (node->children[index] == NULL) {
            node->children[index] = trie_node_create(alloc);
        }

        node = node->children[index];
    }

    node->terminal = true;
    node->kind = kind;
}

// TASK(20261112-001117-550-n6-f2d668b1): add a function to free a parser

Lexer createLexer(FILE* fd, Allocator* alloc) {
    Lexer p = {
        .alloc = alloc,
        .fd = fd,
        .line = 1,
        .col = 1,
        .unread = {0},
        .count = 0,
    };
    p.trie = trie_node_create(alloc);

#define INSERT_SYMBOL(symbol, kind) trie_insert(p.trie, symbol, kind, alloc);

    SYMBOL_LIST(INSERT_SYMBOL)

#undef INSERT_SYMBOL
    return p;
}

void unreadToken(Token token, Lexer* lex) {
    assert(lex->count < TOKEN_BUFFER_SIZE, "too many tokens in the buffer\n");
    lex->unread[lex->count++] = token;
}

typedef struct ReadChar ReadChar;
struct ReadChar {
    char ch;
    Pos pos;
};

Pos read(Lexer* p, char* ch) {
    Pos last = {SETPOS(p)};

    *ch = (char)fgetc(p->fd);

    if (*ch == EOF) {
        p->eof = true;
        return last;
    }

    p->col++;

    if (*ch == '\n') {
        p->col = 1;
        p->line++;
    }

    return last;
}

void unread(char ch, Lexer* p, Pos pos) {
    ungetc(ch, p->fd);
    if (ch == EOF) {
        p->eof = false;
    } else {
        p->line = pos.line;
        p->col = pos.col;
    }
}

Token parseSymbol(Lexer* p, char first, Pos start) {
    TrieNode* current = getNext(p->trie, first);

    if (current == NULL) {
        panic("unknown character at " POS "\n", start.line, start.col);
    }

    DA(ReadChar) consumed = {0};
    da_init(&consumed, p->alloc);

    TrieNode* last_valid = NULL;
    size_t last_valid_len = 0;

    da_append(&consumed, ((ReadChar){
                             .ch = first,
                             .pos = start,
                         }));

    while (current != NULL) {
        if (current->terminal) {
            last_valid = current;
            last_valid_len = consumed.len;
        }

        char ch;
        Pos pos = read(p, &ch);

        if (p->eof) {
            break;
        }

        TrieNode* next = getNext(current, ch);

        if (next == NULL) {
            unread(ch, p, pos);
            break;
        }

        da_append(&consumed, ((ReadChar){
                                 .ch = ch,
                                 .pos = pos,
                             }));

        current = next;
    }

    if (last_valid == NULL) {
        da_free(&consumed);
        panic("unknown symbol at " POS "\n", start.line, start.col);
    }

    // Restore characters beyond the longest valid match.
    for (size_t i = consumed.len; i > last_valid_len; i--) {
        ReadChar rc = consumed.data[i - 1];
        unread(rc.ch, p, rc.pos);
    }

    Token result = {
        .kind = last_valid->kind,
        .data = {0},
        SETPOS(&start),
    };

    da_free(&consumed);
    return result;
}

TokenKind getKeywordType(String sb) {
    fa_foreach(key, KEYWORDS) {
        bool same = string_compare_cstr(sb, key->name);
        if (same) {
            return key->kind;
        }
    }
    return TOKEN_IDENT;
}

Token getToken(Lexer* p) {
    if (p->count > 0) {
        return p->unread[--p->count];
    }
    bool line_comment = false;
    while (!p->eof) {
        char ch;
        TrieNode* current = p->trie;
        auto pos = read(p, &ch);
        if (ch == EOF) {
            continue; // basically breaking because of how it works
        }
        if (line_comment) {
            if (ch == '\n') {
                line_comment = false;
            }
            continue;
        }
        if (isspace(ch)) {
            continue;
        }
        // comments
        if (ch == '/') {
            Pos pp = read(p, &ch);
            if (ch == '/') {
                line_comment = true;
                continue;
            } else {
                unread(ch, p, pp);
            }
        }
        if (isalpha(ch) || ch == '_') {
            String sb = string_create(p->alloc);
            Pos pp;
            while (isalnum(ch) || ch == '_') {
                append_char(&sb, ch);

                pp = read(p, &ch);
            }
            unread(ch, p, pp);
            TokenKind kind = getKeywordType(sb);
            if (kind != TOKEN_IDENT) {
                da_free(&sb);
                sb = (String){0};
            }

            return (Token){
                .data = sb,
                SETPOS(&pos),
                .kind = kind,
            };
        }

        // TASK(20261112-003526-307-n6-b2f02461): allow '_' between digits
        if (isdigit(ch)) {
            String sb = string_create(p->alloc);
            Pos pp;
            while (isdigit(ch)) {
                append_char(&sb, ch);
                pp = read(p, &ch);
            }
            unread(ch, p, pp);
            return (Token){
                .data = sb,
                SETPOS(&pos),
                .kind = TOKEN_INT_LIT,
            };
        }
        if (ch == '"') {
            String sb = string_create(p->alloc);

            while (true) {
                read(p, &ch);

                if (ch == '"')
                    break;

                if (ch == EOF) {
                    panic("string literal unclosed at " POS "\n", FMTPOS(&pos));
                }

                if (ch == '\\') {
                    read(p, &ch);

                    switch (ch) {
                    case 'n':
                        ch = '\n';
                        break;
                    case 't':
                        ch = '\t';
                        break;
                    case 'r':
                        ch = '\r';
                        break;
                    case '0':
                        ch = '\0';
                        break;
                    case '\\':
                        ch = '\\';
                        break;
                    case '"':
                        ch = '"';
                        break;
                    case '\'':
                        ch = '\'';
                        break;

                    default:
                        panic("unknown escape sequence '\\%c' at " POS "\n", ch,
                              FMTPOS(&pos));
                    }
                }

                append_char(&sb, ch);
            }

            return (Token){
                .data = sb,
                SETPOS(&pos),
                .kind = TOKEN_STR_LIT,
            };
        }

        current = getNext(current, ch);

        if (current != NULL) {
            return parseSymbol(p, ch, pos);
        }

        panic("unknown character '%c' at " POS "\n", ch, FMTPOS(p));
    }
    if (p->eof) {
        return (Token){
            .data = {0},
            SETPOS(p),
            .kind = TOKEN_EOF,
        };
    }
    unreachable();
}

static void trie_node_free(TrieNode* node, Allocator* alloc) {
    if (node == NULL)
        return;

    for (size_t i = 0; i < sizeof(SYMBOL_CHARS) - 1; i++) {
        trie_node_free(node->children[i], alloc);
    }

    FREE(alloc, node);
}

void freeLexer(Lexer* p) {
    if (p == NULL)
        return;

    trie_node_free(p->trie, p->alloc);
    fa_foreach_len(token, p->unread, p->count) { freeToken(token); }
    *p = (Lexer){0};
}

void freeToken(Token* token) { da_free(&token->data); }
