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
    /* A protected function with no parent was declared at top-level scope.
       It is accessible from anywhere in the same file (same-file access is
       already enforced by the compiler only checking C->functions here). */
    if(!owner) return true;
    /* Otherwise the function belongs to a specific owner; allow the owner
       itself and any of its direct or transitive nested functions. */
    FunctionObject *c=caller;
    while(c){ if(c==owner) return true; c=c->parent; }
    return false;
}
