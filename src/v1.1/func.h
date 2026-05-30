

#ifndef FUNC_H
#define FUNC_H
#include "common.h"

typedef struct FunctionObject {
    char               name[MAX_IDENT_LEN];
    int                arity;
    char             **params;
    int                param_cap;
    Chunk              chunk;
    FunctionVisibility visibility;
    bool               exported;
    bool               has_captures;  
    char               source_file[1024];
    struct FunctionObject **nested;
    int                     nested_count, nested_cap;
    struct FunctionObject  *parent;
} FunctionObject;

#define MAX_REGISTRY MAX_FUNCS
extern FunctionObject *func_registry[MAX_REGISTRY];
extern int             func_registry_count;

FunctionObject *func_new      (const char *name, FunctionVisibility vis, const char *src);
void            func_free     (FunctionObject *f);
int             func_register (FunctionObject *f);
FunctionObject *func_lookup   (const char *name);
int             func_index    (const char *name);
bool            func_can_access(FunctionObject *caller, FunctionObject *callee);
#endif
