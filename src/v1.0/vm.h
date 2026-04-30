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

#ifndef VM_H
#define VM_H

#include "common.h"
#include "func.h"

typedef enum { VM_OK, VM_RUNTIME_ERROR } VMResult;

typedef struct {
    FunctionObject *function;
    uint8_t        *ip;
    int             base_idx;
} CallFrame;

typedef struct {
    uint8_t *handler_ip;    
    int      stack_top;     
    int      frame_count;   
} TryFrame;

typedef struct VM {
    Chunk      *top_chunk;
    CallFrame   frames[MAX_CALL_DEPTH];
    int         frame_count;
    Value       stack[MAX_STACK];
    int         stack_top;
    Value       globals[MAX_VARIABLES];
    int         global_count;
    
    TryFrame    try_stack[MAX_TRY_DEPTH];
    int         try_top;
    Value       error_value;   
    bool        has_error;
} VM;

void     vm_init       (VM *vm);          
void     vm_init_no_gc  (VM *vm);          
VMResult vm_run  (VM *vm, Chunk *top_chunk);
void     vm_free (VM *vm);

#endif
