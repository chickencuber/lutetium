#pragma once

#include "ast.h"
#include "aids.h"

typedef struct Compiler Compiler;

Compiler* compiler_create(Allocator* alloc);
void compiler_free(Compiler* compiler);

bool compiler_compile(Compiler* compiler, AST* ast);
bool compiler_compile_to_file(Compiler* compiler, AST* ast, const char* path);

const char* compiler_get_error(Compiler* compiler);
void* compiler_get_function(Compiler* compiler, const char* name);
