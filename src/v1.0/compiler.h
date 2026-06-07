

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
    bool has_entry_main;   
    bool is_bundle_pass;   

    
    struct {
        char alias[256];    
        int  import_start;  
        int  import_end;    
    } aliases[64];
    int alias_count;

    int  lambda_count;  

    char imp_names[MAX_FUNCS][256];
    int  imp_name_count;

    CtxFrame ctx_stack[MAX_CTX_DEPTH];
    int      ctx_depth;

    
    int opt_level;

    bool (*import_handler)(const char *path, struct Compiler *C);
} Compiler;

extern bool (*chn_import_handler)(const char *path, Compiler *C);

void  compiler_init   (Compiler *C, const char *source_file);
void  compiler_free   (Compiler *C);
bool  compiler_compile(Compiler *C, ASTNode *ast);
void  chunk_disasm    (Chunk *ch, const char *name);

#endif
