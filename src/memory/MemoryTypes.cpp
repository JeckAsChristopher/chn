

extern "C" {
#define _POSIX_C_SOURCE 200809L
#include "../v1.1/gc.h"     
#include "../v1.1/vm.h"     
#include "../v1.1/error.h"  
}

#include "include/MemoryTypes.h"
#include "include/MemoryHandler.h"   
#include "include/RAII.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>

static ChnOomHook g_oom_hook = nullptr;

extern "C" void chn_mem_init(void) {
    
}

extern "C" void chn_mem_shutdown(void) {
    
}

extern "C" void chn_set_oom_hook(ChnOomHook hook) {
    g_oom_hook    = hook;
    gc_oom_hook   = hook;   
}

extern "C" void *chn_safe_malloc(size_t sz, const char *tag) {
    void *p = gc_realloc(nullptr, 0, sz);
    if (!p && sz > 0) {
        if (g_oom_hook) g_oom_hook(sz, tag);
        std::fprintf(stderr, "chn: fatal OOM allocating %zu bytes (%s)\n",
                     sz, tag ? tag : "?");
        std::abort();
    }
    return p;
}

extern "C" void *chn_safe_realloc(void *ptr, size_t old_sz, size_t new_sz,
                                   const char *tag) {
    void *p = gc_realloc(ptr, old_sz, new_sz);
    if (!p && new_sz > 0) {
        if (g_oom_hook) g_oom_hook(new_sz, tag);
        std::fprintf(stderr, "chn: fatal OOM reallocating %zu→%zu bytes (%s)\n",
                     old_sz, new_sz, tag ? tag : "?");
        std::abort();
    }
    return p;
}

extern "C" void chn_safe_free(void *ptr, size_t sz) {
    if (ptr) gc_realloc(ptr, sz, 0);
}

extern "C" void chn_gc_pin(void *obj) {
    if (obj) gc_pin(reinterpret_cast<Obj *>(obj));
}

extern "C" void chn_gc_unpin(void *obj) {
    if (obj) gc_unpin(reinterpret_cast<Obj *>(obj));
}

extern "C" void chn_mem_report(void) {
    chn::MemoryHandler::report();
}

extern "C" int chn_vm_exec_safe(void *vm_ptr, void *ch_ptr) {
    VM    *vm = reinterpret_cast<VM    *>(vm_ptr);
    Chunk *ch = reinterpret_cast<Chunk *>(ch_ptr);

    
    chn::VMGuard guard(reinterpret_cast<chn::OpaqueVM *>(vm));

    try {
        VMResult res = vm_run(vm, ch);
        return (res == VM_OK && !g_had_error) ? 0 : 1;
    } catch (const chn::ChnMemoryError &e) {
        std::fprintf(stderr, "\nChnMemoryError: %s\n", e.what());
        return 1;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "\ninternal error: %s\n", e.what());
        return 1;
    } catch (...) {
        std::fprintf(stderr, "\ninternal error: unknown exception\n");
        return 1;
    }
    
}
