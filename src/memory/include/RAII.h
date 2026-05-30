

#ifndef CHN_RAII_H
#define CHN_RAII_H

#ifdef __cplusplus

#include "MemoryTypes.h"
#include <cstddef>
#include <cstdlib>
#include <cassert>
#include <stdexcept>
#include <utility>
#include <functional>

namespace chn {

class GCPinGuard {
    void *obj_;
public:
    explicit GCPinGuard(void *obj) noexcept : obj_(obj) {
        if (obj_) chn_gc_pin(obj_);
    }
    ~GCPinGuard() noexcept {
        if (obj_) chn_gc_unpin(obj_);
    }

    GCPinGuard(const GCPinGuard &)            = delete;
    GCPinGuard &operator=(const GCPinGuard &) = delete;

    GCPinGuard(GCPinGuard &&o) noexcept : obj_(o.obj_) { o.obj_ = nullptr; }
    GCPinGuard &operator=(GCPinGuard &&o) noexcept {
        if (this != &o) {
            if (obj_) chn_gc_unpin(obj_);
            obj_   = o.obj_;
            o.obj_ = nullptr;
        }
        return *this;
    }

    
    void *release() noexcept { void *p = obj_; obj_ = nullptr; return p; }
};

class MallocGuard {
    void *ptr_;
public:
    explicit MallocGuard(void *p) noexcept : ptr_(p) {}
    ~MallocGuard() noexcept { std::free(ptr_); }

    MallocGuard(const MallocGuard &)            = delete;
    MallocGuard &operator=(const MallocGuard &) = delete;

    MallocGuard(MallocGuard &&o) noexcept : ptr_(o.ptr_) { o.ptr_ = nullptr; }
    MallocGuard &operator=(MallocGuard &&o) noexcept {
        if (this != &o) {
            std::free(ptr_);
            ptr_   = o.ptr_;
            o.ptr_ = nullptr;
        }
        return *this;
    }

    void  *get()     const noexcept { return ptr_; }
    char  *chars()   const noexcept { return static_cast<char *>(ptr_); }
    void  *release() noexcept       { void *p = ptr_; ptr_ = nullptr; return p; }
    bool   empty()   const noexcept { return ptr_ == nullptr; }
};

class ChunkGuard {
    OpaqueChunk *ch_;
public:
    explicit ChunkGuard(OpaqueChunk *ch) noexcept : ch_(ch) {}
    ~ChunkGuard() noexcept;   

    ChunkGuard(const ChunkGuard &)            = delete;
    ChunkGuard &operator=(const ChunkGuard &) = delete;

    ChunkGuard(ChunkGuard &&o) noexcept : ch_(o.ch_) { o.ch_ = nullptr; }
    ChunkGuard &operator=(ChunkGuard &&o) noexcept;

    OpaqueChunk *get()     const noexcept { return ch_; }
    OpaqueChunk *release() noexcept       { auto *p = ch_; ch_ = nullptr; return p; }
};

class VMGuard {
    OpaqueVM *vm_;
public:
    explicit VMGuard(OpaqueVM *vm) noexcept : vm_(vm) {}
    ~VMGuard() noexcept;   

    VMGuard(const VMGuard &)            = delete;
    VMGuard &operator=(const VMGuard &) = delete;

    VMGuard(VMGuard &&o) noexcept : vm_(o.vm_) { o.vm_ = nullptr; }
    VMGuard &operator=(VMGuard &&o) noexcept;

    OpaqueVM *get()     const noexcept { return vm_; }
    OpaqueVM *release() noexcept       { auto *p = vm_; vm_ = nullptr; return p; }
};

template <typename F>
class ScopeExit {
    F    fn_;
    bool active_;
public:
    explicit ScopeExit(F &&f) noexcept(noexcept(F(std::move(f))))
        : fn_(std::move(f)), active_(true) {}

    ~ScopeExit() noexcept {
        if (active_) {
            try { fn_(); } catch (...) {  }
        }
    }

    void cancel() noexcept { active_ = false; }
    void invoke() noexcept {
        if (active_) { active_ = false; try { fn_(); } catch (...) {} }
    }

    ScopeExit(const ScopeExit &)            = delete;
    ScopeExit &operator=(const ScopeExit &) = delete;
};

template <typename F>
ScopeExit<F> defer(F &&f) {
    return ScopeExit<F>(std::forward<F>(f));
}

template <typename T>
class SafeBuffer {
    T  *data_;
    int size_;
    int cap_;

    void grow() {
        int new_cap = (cap_ < 8) ? 8 : cap_ * 2;
        T  *new_data = static_cast<T *>(
            chn_safe_realloc(data_,
                             static_cast<size_t>(cap_)  * sizeof(T),
                             static_cast<size_t>(new_cap) * sizeof(T),
                             "SafeBuffer"));
        data_ = new_data;
        cap_  = new_cap;
    }

public:
    SafeBuffer() noexcept : data_(nullptr), size_(0), cap_(0) {}

    ~SafeBuffer() noexcept {
        chn_safe_free(data_, static_cast<size_t>(cap_) * sizeof(T));
    }

    SafeBuffer(const SafeBuffer &)            = delete;
    SafeBuffer &operator=(const SafeBuffer &) = delete;

    SafeBuffer(SafeBuffer &&o) noexcept
        : data_(o.data_), size_(o.size_), cap_(o.cap_) {
        o.data_ = nullptr; o.size_ = 0; o.cap_ = 0;
    }

    void push(const T &item) {
        if (size_ >= cap_) grow();
        data_[size_++] = item;
    }

    void push(T &&item) {
        if (size_ >= cap_) grow();
        data_[size_++] = std::move(item);
    }

    T &operator[](int i) {
        if (i < 0 || i >= size_)
            throw std::out_of_range("SafeBuffer: index out of range");
        return data_[i];
    }

    const T &operator[](int i) const {
        if (i < 0 || i >= size_)
            throw std::out_of_range("SafeBuffer: index out of range");
        return data_[i];
    }

    int   size()  const noexcept { return size_; }
    int   cap()   const noexcept { return cap_;  }
    bool  empty() const noexcept { return size_ == 0; }
    T    *data()  const noexcept { return data_; }

    void clear() noexcept { size_ = 0; }

    
    T *release() noexcept {
        T *p = data_; data_ = nullptr; size_ = 0; cap_ = 0; return p;
    }
};

} 

#endif 
#endif 
