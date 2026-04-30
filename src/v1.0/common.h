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

#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <math.h>
#include <setjmp.h>

#define MAX_IDENT_LEN    256
#define MAX_STRING_LEN   65536
#define MAX_PARAMS        64
#define MAX_CALL_DEPTH   512
#define MAX_STACK       8192
#define MAX_VARIABLES   1024   
#define MAX_FUNCS        512
#define MAX_LOCALS       512
#define MAX_TRY_DEPTH     64
#define MAX_CTX_DEPTH     64
#define MAX_BREAKS        256

typedef struct ObjString      ObjString;
typedef struct ObjArray       ObjArray;
typedef struct ObjDict        ObjDict;
typedef struct FunctionObject FunctionObject;

typedef enum { VIS_PUBLIC, VIS_PRIVATE, VIS_PROTECTED } FunctionVisibility;

typedef enum {
    VAL_NUMBER, VAL_STRING, VAL_BOOL, VAL_NIL,
    VAL_FUNCTION, VAL_ARRAY, VAL_DICT
} ValueType;

typedef struct Value_s {
    ValueType type;
    union {
        double          number;
        ObjString      *string;
        bool            boolean;
        FunctionObject *function;
        ObjArray       *array;
        ObjDict        *dict;
    } as;
} Value;

#define IS_NUMBER(v)   ((v).type==VAL_NUMBER)
#define IS_STRING(v)   ((v).type==VAL_STRING)
#define IS_BOOL(v)     ((v).type==VAL_BOOL)
#define IS_NIL(v)      ((v).type==VAL_NIL)
#define IS_FUNCTION(v) ((v).type==VAL_FUNCTION)
#define IS_ARRAY(v)    ((v).type==VAL_ARRAY)
#define IS_DICT(v)     ((v).type==VAL_DICT)

#define AS_NUMBER(v)   ((v).as.number)
#define AS_STRING(v)   ((v).as.string)
#define AS_BOOL(v)     ((v).as.boolean)
#define AS_FUNCTION(v) ((v).as.function)
#define AS_ARRAY(v)    ((v).as.array)
#define AS_DICT(v)     ((v).as.dict)
#define AS_CSTR(v)     ((v).as.string->chars)

#define NUMBER_VAL(n)  ((Value){VAL_NUMBER,  {.number   =(double)(n)}})
#define STRING_VAL(s)  ((Value){VAL_STRING,  {.string   =(ObjString*)(s)}})
#define BOOL_VAL(b)    ((Value){VAL_BOOL,    {.boolean  =(bool)(b)}})
#define NIL_VAL        ((Value){VAL_NIL,     {.number   =0}})
#define FUNC_VAL(f)    ((Value){VAL_FUNCTION,{.function =(FunctionObject*)(f)}})
#define ARRAY_VAL(a)   ((Value){VAL_ARRAY,   {.array    =(ObjArray*)(a)}})
#define DICT_VAL(d)    ((Value){VAL_DICT,    {.dict     =(ObjDict*)(d)}})

typedef struct {
    uint8_t *code;
    int      code_len, code_cap;

    struct LineRun { int offset; int line; } *lines;
    int lines_len, lines_cap;

    Value   *constants;
    int      const_count, const_cap;

    char   **var_names;
    int      var_count, var_cap;

    /* pre-computed max stack depth (used by VM and bytecode) */
    int      stack_size;

    /* local variable name table for debugger (co_varnames) */
    char   **co_varnames;
    int      varname_count, varname_cap;
} Chunk;

void chunk_init   (Chunk *ch);
void chunk_free   (Chunk *ch);
int  chunk_add_line(Chunk *ch, int offset, int line); 
int  chunk_line_at (Chunk *ch, int offset);           

typedef enum {
    OP_CONST,
    OP_NIL, OP_TRUE, OP_FALSE,
    OP_POP, OP_DUP,

    OP_GET_VAR, OP_SET_VAR, OP_DEF_VAR,
    OP_GET_LOCAL, OP_SET_LOCAL,

    
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG,
    OP_POW,

    
    OP_BAND, OP_BOR, OP_BXOR, OP_BNOT, OP_LSHIFT, OP_RSHIFT,

    
    OP_EQ, OP_NEQ, OP_LT, OP_GT, OP_LE, OP_GE, OP_NOT,

    
    OP_JUMP, OP_JUMP_IF_FALSE, OP_JUMP_IF_TRUE, OP_JUMP_IF_NIL,

    
    OP_CALL, OP_RETURN,

    
    OP_ARRAY_NEW, OP_ARRAY_PUSH, OP_ARRAY_INDEX, OP_ARRAY_SET, OP_ARRAY_LEN,
    OP_METHOD_CALL,

    
    OP_DICT_NEW, OP_DICT_SET, OP_DICT_GET,

    
    OP_FOREACH_INIT,  
    OP_FOREACH_STEP,  

    
    OP_PUSH_HANDLER,  
    OP_POP_HANDLER,
    OP_THROW,
    OP_GET_ERROR,     

    
    OP_TYPEOF,

    
    OP_GC_SAFEPOINT,

    
    OP_PRINT, OP_PROMPT, OP_INPUT,
    OP_NATIVE,

    /* v3.1 bytecode additions */
    OP_CONST_0,         /* push number 0  - no operand */
    OP_CONST_1,         /* push number 1  - no operand */
    OP_INC_LOCAL,       /* slot: local[slot]++ in-place */
    OP_DEC_LOCAL,       /* slot: local[slot]-- in-place */
    OP_CALL_TAIL,       /* argc: tail-call optimisation - reuse frame */

    /* v3.5 new opcodes */
    OP_AWAIT,       /* await expr - identity now, future: unblock promise */

    /* v3.6 new opcodes */
    OP_IN,          /* val in container -> bool (array membership / dict key / substring) */

    OP_HALT
} OpCode;

typedef enum {
    /* array mutation */
    METHOD_ADD=0, METHOD_INSERT, METHOD_CUT,
    METHOD_REMOVE, METHOD_RALL, METHOD_LENGTH,
    METHOD_SORT, METHOD_REVERSE_ARR,
    /* string */
    METHOD_UPPER, METHOD_LOWER, METHOD_TRIM,
    METHOD_SPLIT, METHOD_CONTAINS, METHOD_STARTS_WITH,
    METHOD_ENDS_WITH, METHOD_REPLACE, METHOD_FIND,
    METHOD_SLICE, METHOD_STR_REVERSE, METHOD_TO_NUM,
    /* 1.0: new array */
    METHOD_JOIN, METHOD_MAP, METHOD_FILTER, METHOD_REDUCE,
    METHOD_FLAT, METHOD_UNIQUE, METHOD_POP, METHOD_SHIFT,
    METHOD_COUNT_VAL, METHOD_FIRST, METHOD_LAST,
    METHOD_SUM, METHOD_ARR_MIN, METHOD_ARR_MAX,
    METHOD_ANY, METHOD_ALL, METHOD_COPY, METHOD_FILL, METHOD_INDEX_OF,
    /* 1.0: new string */
    METHOD_PAD_LEFT, METHOD_PAD_RIGHT, METHOD_REPEAT,
    METHOD_CHAR_AT, METHOD_COUNT_STR,
    /* 1.0: dict */
    METHOD_DICT_KEYS, METHOD_DICT_VALUES, METHOD_DICT_HAS,
    METHOD_DICT_DELETE, METHOD_DICT_SIZE, METHOD_DICT_MERGE,
    METHOD_DICT_GET, METHOD_DICT_TO_ARR,
    METHOD_UNKNOWN
} ArrayMethod;

static inline ArrayMethod method_id(const char *n){
    if(!strcmp(n,"add"))         return METHOD_ADD;
    if(!strcmp(n,"push"))        return METHOD_ADD;   /* alias */
    if(!strcmp(n,"append"))      return METHOD_ADD;   /* alias */
    if(!strcmp(n,"insert"))      return METHOD_INSERT;
    if(!strcmp(n,"cut"))         return METHOD_CUT;
    if(!strcmp(n,"remove"))      return METHOD_REMOVE;
    if(!strcmp(n,"rall"))        return METHOD_RALL;
    if(!strcmp(n,"length"))      return METHOD_LENGTH;
    if(!strcmp(n,"sort"))        return METHOD_SORT;
    if(!strcmp(n,"reverse"))     return METHOD_REVERSE_ARR;
    if(!strcmp(n,"upper"))       return METHOD_UPPER;
    if(!strcmp(n,"lower"))       return METHOD_LOWER;
    if(!strcmp(n,"trim"))        return METHOD_TRIM;
    if(!strcmp(n,"split"))       return METHOD_SPLIT;
    if(!strcmp(n,"contains"))    return METHOD_CONTAINS;
    if(!strcmp(n,"starts_with")) return METHOD_STARTS_WITH;
    if(!strcmp(n,"ends_with"))   return METHOD_ENDS_WITH;
    if(!strcmp(n,"replace"))     return METHOD_REPLACE;
    if(!strcmp(n,"find"))        return METHOD_FIND;
    if(!strcmp(n,"slice"))       return METHOD_SLICE;
    if(!strcmp(n,"sub"))         return METHOD_SLICE;   /* alias */
    if(!strcmp(n,"to_num"))      return METHOD_TO_NUM;
    /* v4.3 array */
    if(!strcmp(n,"join"))        return METHOD_JOIN;
    if(!strcmp(n,"map"))         return METHOD_MAP;
    if(!strcmp(n,"filter"))      return METHOD_FILTER;
    if(!strcmp(n,"reduce"))      return METHOD_REDUCE;
    if(!strcmp(n,"flat"))        return METHOD_FLAT;
    if(!strcmp(n,"unique"))      return METHOD_UNIQUE;
    if(!strcmp(n,"pop"))         return METHOD_POP;
    if(!strcmp(n,"shift"))       return METHOD_SHIFT;
    if(!strcmp(n,"count"))       return METHOD_COUNT_VAL;
    if(!strcmp(n,"first"))       return METHOD_FIRST;
    if(!strcmp(n,"last"))        return METHOD_LAST;
    if(!strcmp(n,"sum"))         return METHOD_SUM;
    if(!strcmp(n,"min"))         return METHOD_ARR_MIN;
    if(!strcmp(n,"max"))         return METHOD_ARR_MAX;
    if(!strcmp(n,"any"))         return METHOD_ANY;
    if(!strcmp(n,"all"))         return METHOD_ALL;
    if(!strcmp(n,"copy"))        return METHOD_COPY;
    if(!strcmp(n,"fill"))        return METHOD_FILL;
    if(!strcmp(n,"index_of"))    return METHOD_INDEX_OF;
    /* v4.3 string */
    if(!strcmp(n,"pad_left"))    return METHOD_PAD_LEFT;
    if(!strcmp(n,"pad_right"))   return METHOD_PAD_RIGHT;
    if(!strcmp(n,"repeat"))      return METHOD_REPEAT;
    if(!strcmp(n,"char_at"))     return METHOD_CHAR_AT;
    /* v4.3 dict */
    if(!strcmp(n,"keys"))        return METHOD_DICT_KEYS;
    if(!strcmp(n,"values"))      return METHOD_DICT_VALUES;
    if(!strcmp(n,"has"))         return METHOD_DICT_HAS;
    if(!strcmp(n,"delete"))      return METHOD_DICT_DELETE;
    if(!strcmp(n,"size"))        return METHOD_DICT_SIZE;
    if(!strcmp(n,"merge"))       return METHOD_DICT_MERGE;
    if(!strcmp(n,"get"))         return METHOD_DICT_GET;
    if(!strcmp(n,"to_arr"))      return METHOD_DICT_TO_ARR;
    return METHOD_UNKNOWN;
}

static inline const char *method_name(ArrayMethod m){
    switch(m){
        case METHOD_ADD:          return "add";
        case METHOD_INSERT:       return "insert";
        case METHOD_CUT:          return "cut";
        case METHOD_REMOVE:       return "remove";
        case METHOD_RALL:         return "rall";
        case METHOD_LENGTH:       return "length";
        case METHOD_SORT:         return "sort";
        case METHOD_REVERSE_ARR:  return "reverse";
        case METHOD_UPPER:        return "upper";
        case METHOD_LOWER:        return "lower";
        case METHOD_TRIM:         return "trim";
        case METHOD_SPLIT:        return "split";
        case METHOD_CONTAINS:     return "contains";
        case METHOD_STARTS_WITH:  return "starts_with";
        case METHOD_ENDS_WITH:    return "ends_with";
        case METHOD_REPLACE:      return "replace";
        case METHOD_FIND:         return "find";
        case METHOD_SLICE:        return "slice";
        case METHOD_STR_REVERSE:  return "reverse";
        case METHOD_TO_NUM:       return "to_num";
        default:                  return "?";
    }
}

static inline const char *visibility_name(FunctionVisibility v){
    switch(v){
        case VIS_PUBLIC:    return "public";
        case VIS_PRIVATE:   return "private";
        case VIS_PROTECTED: return "protected";
        default:            return "unknown";
    }
}

static inline const char *opcode_name(OpCode op){
    switch(op){
        case OP_CONST:          return "CONST";
        case OP_NIL:            return "NIL";
        case OP_TRUE:           return "TRUE";
        case OP_FALSE:          return "FALSE";
        case OP_POP:            return "POP";
        case OP_DUP:            return "DUP";
        case OP_GET_VAR:        return "GET_VAR";
        case OP_SET_VAR:        return "SET_VAR";
        case OP_DEF_VAR:        return "DEF_VAR";
        case OP_GET_LOCAL:      return "GET_LOCAL";
        case OP_SET_LOCAL:      return "SET_LOCAL";
        case OP_ADD:            return "ADD";
        case OP_SUB:            return "SUB";
        case OP_MUL:            return "MUL";
        case OP_DIV:            return "DIV";
        case OP_MOD:            return "MOD";
        case OP_NEG:            return "NEG";
        case OP_POW:            return "POW";
        case OP_BAND:           return "BAND";
        case OP_BOR:            return "BOR";
        case OP_BXOR:           return "BXOR";
        case OP_BNOT:           return "BNOT";
        case OP_LSHIFT:         return "LSHIFT";
        case OP_RSHIFT:         return "RSHIFT";
        case OP_EQ:             return "EQ";
        case OP_NEQ:            return "NEQ";
        case OP_LT:             return "LT";
        case OP_GT:             return "GT";
        case OP_LE:             return "LE";
        case OP_GE:             return "GE";
        case OP_NOT:            return "NOT";
        case OP_JUMP:           return "JUMP";
        case OP_JUMP_IF_FALSE:  return "JUMP_IF_FALSE";
        case OP_JUMP_IF_TRUE:   return "JUMP_IF_TRUE";
        case OP_JUMP_IF_NIL:    return "JUMP_IF_NIL";
        case OP_CALL:           return "CALL";
        case OP_RETURN:         return "RETURN";
        case OP_ARRAY_NEW:      return "ARRAY_NEW";
        case OP_ARRAY_PUSH:     return "ARRAY_PUSH";
        case OP_ARRAY_INDEX:    return "ARRAY_INDEX";
        case OP_ARRAY_SET:      return "ARRAY_SET";
        case OP_ARRAY_LEN:      return "ARRAY_LEN";
        case OP_METHOD_CALL:    return "METHOD_CALL";
        case OP_DICT_NEW:       return "DICT_NEW";
        case OP_DICT_SET:       return "DICT_SET";
        case OP_DICT_GET:       return "DICT_GET";
        case OP_FOREACH_INIT:   return "FOREACH_INIT";
        case OP_FOREACH_STEP:   return "FOREACH_STEP";
        case OP_PUSH_HANDLER:   return "PUSH_HANDLER";
        case OP_POP_HANDLER:    return "POP_HANDLER";
        case OP_THROW:          return "THROW";
        case OP_GET_ERROR:      return "GET_ERROR";
        case OP_TYPEOF:         return "TYPEOF";
        case OP_GC_SAFEPOINT:   return "GC_SAFEPOINT";
        case OP_PRINT:          return "PRINT";
        case OP_PROMPT:         return "PROMPT";
        case OP_INPUT:          return "INPUT";
        case OP_NATIVE:         return "NATIVE";
        case OP_CONST_0:        return "CONST_0";
        case OP_CONST_1:        return "CONST_1";
        case OP_INC_LOCAL:      return "INC_LOCAL";
        case OP_DEC_LOCAL:      return "DEC_LOCAL";
        case OP_CALL_TAIL:      return "CALL_TAIL";
        case OP_AWAIT:          return "AWAIT";
        case OP_IN:             return "IN";
        case OP_HALT:           return "HALT";
        default:                return "UNKNOWN";
    }
}

#endif
