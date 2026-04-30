/*
 * Copyright 2025 Jeck Christopher Anog
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef COMPILER_H
#define COMPILER_H

#include "common.h"
#include "func.h"
#include "ast.h"
#include "error.h"

typedef struct { char name[MAX_IDENT_LEN]; int index; bool is_let; bool defined; } Symbol;
typedef struct { char name[MAX_IDENT_LEN]; int slot;  bool is_let; bool defined; } Local;

typedef enum { CTX_LOOP, CTX_SWITCH } CtxKind;

typedef struct {
    CtxKind kind;
    int break_patches[MAX_BREAKS];
    int break_count;
    int continue_patches[MAX_BREAKS];
    int continue_count;
    int continue_target;
} CtxFrame;

typedef struct Compiler {
    Chunk  *current_chunk;
    Chunk   top_chunk;

    Symbol  globals[MAX_VARIABLES];
    int     global_count;

    Local   locals[MAX_LOCALS];
    int     local_count;
    bool    in_function;
    FunctionObject *current_func;

    FunctionObject *functions[MAX_FUNCS];
    int             func_count;

    FunctionObject *imports[MAX_FUNCS];
    int             import_count;

    char source_file[1024];
    bool had_error;
    bool has_entry_main;   /* true if a public entry main was compiled */
    bool is_bundle_pass;   /* 1.0: true during pass-2 CCO compilation - relaxes visibility for intra-bundle imports */

    /* 1.0: module alias table - imp::lib mymod as pkg  /  imp mymod as pkg */
    struct {
        char alias[256];    /* the alias name, e.g. "pkg"                   */
        int  import_start;  /* slice into C->imports[]: functions start here */
        int  import_end;    /* slice end (exclusive)                         */
    } aliases[64];
    int alias_count;

    int  lambda_count;  /* serial number for unique lambda names */

    char imp_names[MAX_FUNCS][256];
    int  imp_name_count;

    CtxFrame ctx_stack[MAX_CTX_DEPTH];
    int      ctx_depth;

    /* optimisation level  0=off  1=basic  2=full (default) */
    int opt_level;

    bool (*import_handler)(const char *path, struct Compiler *C);
} Compiler;

extern bool (*chn_import_handler)(const char *path, Compiler *C);

void  compiler_init   (Compiler *C, const char *source_file);
void  compiler_free   (Compiler *C);
bool  compiler_compile(Compiler *C, ASTNode *ast);
void  chunk_disasm    (Chunk *ch, const char *name);

#endif
