#include "ast.h"
#include <stdio.h>
#define AIDS_IMPLEMENTATION
#define AIDS_SILENT_WARNINGS
#include "aids.h"
#include "lexer.h"

int main(int argc, char** argv) {
    assert(argc > 1, "expected at least one arg\n");
    char* file = argv[1];
    auto fd = fopen(file, "rb");
    assert(fd != NULL, "could not open %s\n", file);


    Lexer lexer = createLexer(fd, &DEFAULT_ALLOCATOR);
    AST* ast = ast_parse_toplevel(&DEFAULT_ALLOCATOR, &lexer);
    freeLexer(&lexer);
    fclose(fd);
    ast_display(ast);
    ast_free(ast);

    return 0;
}
