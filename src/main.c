#define AIDS_IMPLEMENTATION
#define AIDS_SILENT_WARNINGS
#include "aids.h"
#define AIDS_CLI_IMPLEMENTATION
#include "aids_cli.h"
#include "analysis.h"
#include "ast.h"
#include "compile.h"
#include <stdio.h>
#include "lexer.h"

String str_without_extension(Str s, Allocator* alloc) {
    size_t end = s.len;

    for (size_t i = s.len; i > 0; i--) {
        char c = s.data[i - 1];

        if (c == '/' || c == '\\')
            break;

        if (c == '.') {
            end = i - 1;
            break;
        }
    }

    String result = string_create(alloc);
    da_reserve(&result, end);
    memcpy(result.data, s.data, end);
    result.len = end;

    return result;
}

int main(int argc, char** argv) {
    auto parser = initArgParser(&DEFAULT_ALLOCATOR, "lutc", "Compiler for lutetium", "Happy coding!");
    argHelp(parser);
    Str* file = argString(parser, "file", "the input file");
    Str* out = argOption(parser, "output", "o", "the output file");
    parseArgs(parser, argc,argv);
    char* output;
    if(out->len == 0) {
        String s = str_without_extension(*out, &DEFAULT_ALLOCATOR);
        append_cstr(&s, ".0");
        output = from_string(&s, &DEFAULT_ALLOCATOR);
        da_free(&s);
    } else {
        output = str_to_cstr(*out, &DEFAULT_ALLOCATOR);
    }

    char* f = str_to_cstr(*file, &DEFAULT_ALLOCATOR);
    auto fd = fopen(f, "rb");
    assert(fd != NULL, "could not open "STRING"\n", FMTSTRING(file));
    FREE(&DEFAULT_ALLOCATOR, f);


    Lexer lexer = createLexer(fd, &DEFAULT_ALLOCATOR);
    AST* ast = ast_parse_toplevel(&DEFAULT_ALLOCATOR, &lexer);

    freeLexer(&lexer);
    fclose(fd);
    analyze(ast);
#ifdef DEBUG
    ast_display(ast);
#endif

    compile_to_file(ast, output);
    FREE(&DEFAULT_ALLOCATOR, output);
    ast_free(ast);
    freeArgParser(parser);
    return 0;
}
