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
    bool               has_captures;  /* vars also stored in globals for nested funcs */
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
