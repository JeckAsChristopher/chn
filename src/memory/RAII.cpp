

extern "C" {
#define _POSIX_C_SOURCE 200809L
#include "../v1.1/common.h"   
#include "../v1.1/gc.h"       
#include "../v1.1/vm.h"       
}

#include "include/RAII.h"
#include <cstdlib>

namespace chn {

ChunkGuard::~ChunkGuard() noexcept {
    if (ch_) {
        chunk_free(reinterpret_cast<Chunk *>(ch_));
        ch_ = nullptr;
    }
}

ChunkGuard &ChunkGuard::operator=(ChunkGuard &&o) noexcept {
    if (this != &o) {
        if (ch_) chunk_free(reinterpret_cast<Chunk *>(ch_));
        ch_   = o.ch_;
        o.ch_ = nullptr;
    }
    return *this;
}

VMGuard::~VMGuard() noexcept {
    if (vm_) {
        vm_free(reinterpret_cast<VM *>(vm_));
        vm_ = nullptr;
    }
}

VMGuard &VMGuard::operator=(VMGuard &&o) noexcept {
    if (this != &o) {
        if (vm_) vm_free(reinterpret_cast<VM *>(vm_));
        vm_   = o.vm_;
        o.vm_ = nullptr;
    }
    return *this;
}

} 
