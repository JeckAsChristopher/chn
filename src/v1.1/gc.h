

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef GC_H
#define GC_H

#include "common.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef enum { OBJ_STRING, OBJ_ARRAY, OBJ_DICT } ObjType;
typedef enum { GC_WHITE=0, GC_GRAY=1, GC_BLACK=2 } GCColor;
typedef enum { GEN_YOUNG=0, GEN_OLD=1, GEN_PINNED=2 } GCGen;

#define GC_PROMOTE_AGE  4

typedef struct Obj {
    ObjType     type;
    GCColor     color;
    GCGen       generation;
    uint8_t     age;
    uint16_t    pin_count;   
    struct Obj *next;
    struct Obj *gen_next;
} Obj;

typedef struct ObjString {
    Obj      header;
    int      len;
    uint32_t hash;
    bool     slab_alloc;    
    char     chars[];
} ObjString;

typedef struct ObjArray {
    Obj    header;
    Value *items;
    int    len, cap;
} ObjArray;

typedef struct DictEntry {
    ObjString *key;
    Value      value;
} DictEntry;

typedef struct ObjDict {
    Obj       header;
    DictEntry *entries;
    int        count;
    int        cap;
} ObjDict;

#define SLAB_STR_MAX   128
#define SLAB_SIZE      (64*1024)

typedef struct StringSlab {
    uint8_t          *mem;
    size_t            used;
    int               live;
    struct StringSlab *next;
} StringSlab;

#define INTERN_CAP  32768

typedef struct {
    ObjString *str;
    uint32_t   hash;
    bool       used;
    bool       tombstone;
} InternEntry;

#define REMSET_CAP  4096
typedef struct { Obj *entries[REMSET_CAP]; int count; } RememberedSet;
typedef struct { Obj **stack; int top, cap; } GrayStack;

#define GC_YOUNG_THRESHOLD   (1024 * 1024)
#define GC_MAJOR_THRESHOLD   (16   * 1024 * 1024)
#define GC_GROWTH_FACTOR     2
#define GC_STEP_SIZE         512
#define GC_MAJOR_EVERY       12

typedef struct {
    uint64_t total_ns;
    uint64_t max_ns;
    uint64_t min_ns;
    uint32_t count;
} GCPauseStat;

typedef struct {
    Obj        *objects;
    Obj        *young_list;
    Obj        *old_list;
    Obj        *pinned_list;     

    GrayStack   gray;
    InternEntry intern[INTERN_CAP];
    RememberedSet remset;

    StringSlab *slab_head;       
    StringSlab *slab_list;       

    size_t      bytes_allocated;
    size_t      next_gc_young;
    size_t      next_gc_major;
    int         minor_collections;
    int         major_collections;
    size_t      total_allocated;
    size_t      total_freed;

    bool        paused;
    bool        marking;
    bool        major_pending;

    int         live_strings;
    int         live_arrays;
    int         live_dicts;

    GCPauseStat pause_minor;     
    GCPauseStat pause_major;
} GCState;

extern GCState gc;

typedef void (*ChnOomHook)(size_t requested_bytes, const char *tag);
extern ChnOomHook gc_oom_hook;  

static inline void *gc_realloc(void *ptr, size_t old_sz, size_t new_sz){
    if(new_sz==0){
        gc.bytes_allocated=(gc.bytes_allocated>old_sz)?(gc.bytes_allocated-old_sz):0;
        gc.total_freed+=old_sz;
        free(ptr); return NULL;
    }
    if(gc.bytes_allocated>=old_sz)
        gc.bytes_allocated=gc.bytes_allocated-old_sz+new_sz;
    else
        gc.bytes_allocated=new_sz;
    if(new_sz>old_sz) gc.total_allocated+=new_sz-old_sz;
    void *r=realloc(ptr,new_sz);
    if(!r && new_sz>0){
        if(gc_oom_hook) gc_oom_hook(new_sz, NULL);
        fprintf(stderr,"ChnMemoryError: out of memory (requested %zu bytes)\n",new_sz);
        exit(1);
    }
    return r;
}

#define GC_ALLOC(sz)      gc_realloc(NULL,0,(sz))
#define GC_FREE(p,sz)     gc_realloc((p),(sz),0)
#define GC_GROW(p,os,ns)  gc_realloc((p),(os),(ns))

#define GC_WRITE_BARRIER(container,new_val) \
    do{ if((container)->generation==GEN_OLD) gc_barrier_slow(new_val); }while(0)

void       gc_init         (void);
ObjString *gc_string       (const char *chars, int len);
ObjString *gc_string_own   (char *heap_str, int len);
ObjString *gc_cstring      (const char *cstr);
ObjArray  *gc_array        (void);
ObjDict   *gc_dict         (void);
void       gc_arr_push     (ObjArray *a, Value v);
void       gc_dict_set     (ObjDict *d, ObjString *key, Value val);
Value      gc_dict_get     (ObjDict *d, ObjString *key);
bool       gc_dict_delete  (ObjDict *d, ObjString *key);
void       gc_mark_value   (Value v);
void       gc_mark_obj     (Obj *obj);
void       gc_barrier_slow (Value v);
void       gc_pin          (Obj *obj);
void       gc_unpin        (Obj *obj);
void       gc_pressure_hint(void *vm_ptr);
void       gc_collect      (void *vm_ptr);
void       gc_step         (void *vm_ptr);
void       gc_free_all     (void);
void       gc_print_stats  (void);

#endif
