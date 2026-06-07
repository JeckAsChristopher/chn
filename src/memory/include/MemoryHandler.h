

#ifndef CHN_MEMORY_HANDLER_H
#define CHN_MEMORY_HANDLER_H

#ifdef __cplusplus

#include "MemoryTypes.h"
#include <cstddef>
#include <string>
#include <stdexcept>

namespace chn {

class ChnMemoryError : public std::bad_alloc {
    std::string msg_;
    std::size_t requested_;
public:
    ChnMemoryError(std::size_t requested, const char *tag = nullptr);

    const char  *what()      const noexcept override { return msg_.c_str(); }
    std::size_t  requested() const noexcept          { return requested_;   }
};

struct MemoryStats {
    
    std::size_t bytes_live;

    
    std::size_t total_allocated;
    std::size_t total_freed;

    
    int live_strings;
    int live_arrays;
    int live_dicts;

    
    int minor_collections;
    int major_collections;

    
    double minor_avg_ms;
    double minor_max_ms;
    double major_avg_ms;
    double major_max_ms;

    
    std::size_t bytes_freed()  const noexcept { return total_freed;   }
    std::size_t bytes_net()    const noexcept {
        return (total_allocated > total_freed)
                   ? (total_allocated - total_freed)
                   : 0;
    }
    int total_collections() const noexcept {
        return minor_collections + major_collections;
    }
};

class MemoryHandler {
public:
    MemoryHandler()  = delete;
    ~MemoryHandler() = delete;

    

    

    static void install();

    

    

    static void *safe_malloc  (std::size_t sz,
                                const char *tag = nullptr);

    static void *safe_realloc (void       *ptr,
                                std::size_t old_sz,
                                std::size_t new_sz,
                                const char *tag = nullptr);

    static void  safe_free    (void *ptr, std::size_t sz) noexcept;

    

    
    static MemoryStats stats();

    

    static void report();

    

    
    static void pin  (void *gc_obj) noexcept;
    static void unpin(void *gc_obj) noexcept;

private:
    
    static void oom_hook_impl(std::size_t requested, const char *tag);
};

} 

#endif 
#endif 
