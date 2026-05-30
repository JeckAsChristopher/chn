

#include "func.h"
#include "gc.h"
#include <stdlib.h>
#include <string.h>

FunctionObject *func_registry[MAX_REGISTRY];
int             func_registry_count=0;

FunctionObject *func_new(const char *name, FunctionVisibility vis, const char *src){
    FunctionObject *f=(FunctionObject*)calloc(1,sizeof(FunctionObject));
    if(!f){fprintf(stderr,"oom\n");exit(1);}
    strncpy(f->name,name?name:"<anon>",MAX_IDENT_LEN-1);
    f->visibility=vis;
    strncpy(f->source_file,src?src:"<unknown>",1023);
    chunk_init(&f->chunk);
    f->params=NULL; f->param_cap=0;
    f->nested=NULL; f->nested_count=0; f->nested_cap=0;
    return f;
}

void func_free(FunctionObject *f){
    if(!f) return;
    chunk_free(&f->chunk);
    for(int i=0;i<f->arity;i++) free(f->params[i]);
    free(f->params);
    free(f->nested);
    free(f);
}

int func_register(FunctionObject *f){
    for(int i=0;i<func_registry_count;i++) if(func_registry[i]==f) return i;
    if(func_registry_count>=MAX_REGISTRY) return -1;
    int idx=func_registry_count++;
    func_registry[idx]=f;
    return idx;
}

FunctionObject *func_lookup(const char *name){
    for(int i=0;i<func_registry_count;i++)
        if(!strcmp(func_registry[i]->name,name)) return func_registry[i];
    return NULL;
}

int func_index(const char *name){
    for(int i=0;i<func_registry_count;i++)
        if(!strcmp(func_registry[i]->name,name)) return i;
    return -1;
}

bool func_can_access(FunctionObject *caller, FunctionObject *callee){
    if(!callee||callee->visibility!=VIS_PROTECTED) return true;
    FunctionObject *owner=callee->parent;
    

    if(!owner) return true;
    

    FunctionObject *c=caller;
    while(c){ if(c==owner) return true; c=c->parent; }
    return false;
}
