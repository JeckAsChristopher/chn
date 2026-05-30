

#ifndef CHN_MEMORY_TYPES_H
#define CHN_MEMORY_TYPES_H

#ifdef __cplusplus
#include <cstddef>
#include <cstdint>

namespace chn {

enum class Type : uint8_t {
    Number   = 0,
    String   = 1,
    Bool     = 2,
    Nil      = 3,
    Function = 4,
    Array    = 5,
    Dict     = 6,
};

struct OpaqueObj;        
struct OpaqueString;     
struct OpaqueArray;      
struct OpaqueDict;       
struct OpaqueChunk;      
struct OpaqueVM;         
struct OpaqueFunction;   

} 

#endif 

#ifdef __cplusplus
extern "C" {
#endif

void  chn_mem_init    (void);   
void  chn_mem_shutdown(void);   

void *chn_safe_malloc  (size_t sz,                              const char *tag);
void *chn_safe_realloc (void *ptr, size_t old_sz, size_t new_sz, const char *tag);
void  chn_safe_free    (void *ptr, size_t sz);

void  chn_gc_pin  (void *obj);
void  chn_gc_unpin(void *obj);

void  chn_mem_report(void);   

typedef void (*ChnOomHook)(size_t requested_bytes, const char *tag);
void  chn_set_oom_hook(ChnOomHook hook);

int   chn_vm_exec_safe(void *vm_ptr, void *ch_ptr);

void  chn_repl_run(void);

#ifdef __cplusplus
} 
#endif

#endif 
