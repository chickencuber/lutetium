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
    ast_display(ast);


    Compiler* cc = compiler_create(&DEFAULT_ALLOCATOR);
    compiler_compile(cc, ast);
    ast_free(ast);

    int (*program_main)(void) = compiler_get_function(cc, "main");
    assert(program_main, "Errors compiling: %s\n", compiler_get_error(cc));
    printf("-----------------------------------------------------\n");
    printf("running\n");
    printf("-----------------------------------------------------\n");
    int exit_code = program_main();
    printf("exit code: %d\n", exit_code);
    compiler_free(cc);
    return 0;
}
