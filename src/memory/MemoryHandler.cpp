

extern "C" {
#define _POSIX_C_SOURCE 200809L
#include "../v1.1/gc.h"     
#include "../v1.1/error.h"  
}

#include "include/MemoryHandler.h"
#include "include/MemoryTypes.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <exception>
#include <string>

namespace chn {

ChnMemoryError::ChnMemoryError(std::size_t requested, const char *tag)
    : requested_(requested) {
    char buf[256];
    if (tag && tag[0])
        std::snprintf(buf, sizeof(buf),
                      "out of memory: failed to allocate %zu byte(s) for '%s'",
                      requested, tag);
    else
        std::snprintf(buf, sizeof(buf),
                      "out of memory: failed to allocate %zu byte(s)",
                      requested);
    msg_ = buf;
}

void MemoryHandler::oom_hook_impl(std::size_t requested, const char *tag) {
    throw ChnMemoryError(requested, tag);
}

void MemoryHandler::install() {
    
    gc_oom_hook = [](size_t sz, const char *t) {
        MemoryHandler::oom_hook_impl(sz, t);
    };

    
    std::set_terminate([]() {
        std::fprintf(stderr,
            "\nCHN internal error: "
            "unhandled exception — program terminated.\n");
        std::abort();
    });
}

MemoryStats MemoryHandler::stats() {
    MemoryStats s{};
    s.bytes_live = gc.bytes_allocated;
    s.total_allocated   = gc.total_allocated;
    s.total_freed       = gc.total_freed;
    s.live_strings      = gc.live_strings;
    s.live_arrays       = gc.live_arrays;
    s.live_dicts        = gc.live_dicts;
    s.minor_collections = gc.minor_collections;
    s.major_collections = gc.major_collections;

    
    if (gc.pause_minor.count > 0)
        s.minor_avg_ms = static_cast<double>(gc.pause_minor.total_ns)
                         / gc.pause_minor.count / 1e6;
    s.minor_max_ms = static_cast<double>(gc.pause_minor.max_ns) / 1e6;

    if (gc.pause_major.count > 0)
        s.major_avg_ms = static_cast<double>(gc.pause_major.total_ns)
                         / gc.pause_major.count / 1e6;
    s.major_max_ms = static_cast<double>(gc.pause_major.max_ns) / 1e6;

    return s;
}

static const char *fmt_bytes(std::size_t b, char *buf, int blen) {
    if      (b >= 1024*1024*1024)
        std::snprintf(buf, blen, "%.2f GB", b / 1073741824.0);
    else if (b >= 1024*1024)
        std::snprintf(buf, blen, "%.2f MB", b / 1048576.0);
    else if (b >= 1024)
        std::snprintf(buf, blen, "%.2f KB", b / 1024.0);
    else
        std::snprintf(buf, blen, "%zu B", b);
    return buf;
}

void MemoryHandler::report() {
    MemoryStats s = stats();
    char b1[32], b2[32], b3[32];

    const char *line = "";
    std::fprintf(stderr,
        "\n CHN Memory Report %s\n", line);
    std::fprintf(stderr,
        "  Live heap:        %s"
        "  (strings: %d  arrays: %d  dicts: %d)\n",
        fmt_bytes(s.bytes_live, b1, sizeof b1),
        s.live_strings, s.live_arrays, s.live_dicts);
    std::fprintf(stderr,
        "  Total allocated:  %s\n",
        fmt_bytes(s.total_allocated, b2, sizeof b2));
    std::fprintf(stderr,
        "  Total freed:      %s\n",
        fmt_bytes(s.total_freed, b3, sizeof b3));
    std::fprintf(stderr,
        "  GC collections:   minor: %d   "
                            "major: %d\n",
        s.minor_collections, s.major_collections);
    if (s.minor_collections > 0)
        std::fprintf(stderr,
            "  GC pauses (minor) avg %.3f ms  "
                               "max %.3f ms\n",
            s.minor_avg_ms, s.minor_max_ms);
    if (s.major_collections > 0)
        std::fprintf(stderr,
            "  GC pauses (major) avg %.3f ms  "
                               "max %.3f ms\n",
            s.major_avg_ms, s.major_max_ms);
    std::fprintf(stderr, "  %s\n\n", line);
}

void *MemoryHandler::safe_malloc(std::size_t sz, const char *tag) {
    return chn_safe_malloc(sz, tag);
}

void *MemoryHandler::safe_realloc(void *ptr, std::size_t old_sz,
                                   std::size_t new_sz, const char *tag) {
    return chn_safe_realloc(ptr, old_sz, new_sz, tag);
}

void MemoryHandler::safe_free(void *ptr, std::size_t sz) noexcept {
    chn_safe_free(ptr, sz);
}

void MemoryHandler::pin(void *gc_obj) noexcept {
    chn_gc_pin(gc_obj);
}

void MemoryHandler::unpin(void *gc_obj) noexcept {
    chn_gc_unpin(gc_obj);
}

} 
