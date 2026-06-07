

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
