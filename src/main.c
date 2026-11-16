#include "analysis.h"
#include "ast.h"
#include "compile.h"
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
    analyze(ast);
    ast_display(ast);

    compile_to_file(ast, "test.o");
    ast_free(ast);
    return 0;
}
