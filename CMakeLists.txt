cmake_minimum_required(VERSION 3.15)

# ─────────────────────────────────────────────────────────────────────────────
# CHN 1.1
#
# Mixed C11 / C++17 build.
#   src/v1.1/      – core language (C11)
#   src/memory/    – RAII memory layer + enhanced REPL (C++17)
# ─────────────────────────────────────────────────────────────────────────────
project(CHN VERSION 1.1 LANGUAGES C CXX)

# ── Standards ─────────────────────────────────────────────────────────────────
set(CMAKE_C_STANDARD            11)
set(CMAKE_C_STANDARD_REQUIRED   ON)
set(CMAKE_C_EXTENSIONS          ON)   # allows _POSIX_C_SOURCE, __GNU__…

set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ── Output directory ──────────────────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ── Source paths ──────────────────────────────────────────────────────────────
set(CHN_SRC  ${CMAKE_SOURCE_DIR}/src/v1.1)
set(CHN_MEM  ${CMAKE_SOURCE_DIR}/src/memory)
set(CHN_INC  ${CMAKE_SOURCE_DIR}/src/memory/include)

# ── Optional packages ─────────────────────────────────────────────────────────
find_package(OpenSSL)
find_package(Threads REQUIRED)

# Readline: optional, used by the enhanced REPL for line-editing + history
find_library(READLINE_LIB readline)
find_path(READLINE_INCLUDE readline/readline.h)

# ── Core C sources (compiled as C11) ─────────────────────────────────────────
set(CHN_C_SOURCES
    ${CHN_SRC}/main.c
    ${CHN_SRC}/lexer.c
    ${CHN_SRC}/parser.c
    ${CHN_SRC}/ast.c
    ${CHN_SRC}/compiler.c
    ${CHN_SRC}/bytecode.c
    ${CHN_SRC}/vm.c
    ${CHN_SRC}/gc.c
    ${CHN_SRC}/func.c
    ${CHN_SRC}/native.c
    ${CHN_SRC}/error.c
)

# native_net.c requires OpenSSL; fall back to the no-op stub when absent
if(OpenSSL_FOUND)
    list(APPEND CHN_C_SOURCES ${CHN_SRC}/native_net.c)
    message(STATUS "CHN: OpenSSL found – network functions enabled")
else()
    list(APPEND CHN_C_SOURCES ${CHN_SRC}/native_net_stub.c)
    message(STATUS "CHN: OpenSSL not found – using native_net stub (network disabled)")
endif()

# ── Memory layer C++ sources (compiled as C++17) ──────────────────────────────
set(CHN_CXX_SOURCES
    ${CHN_MEM}/MemoryTypes.cpp     # C-callable bridge + safe alloc
    ${CHN_MEM}/RAII.cpp            # ChunkGuard / VMGuard destructors
    ${CHN_MEM}/MemoryHandler.cpp   # ChnMemoryError + MemoryHandler
    ${CHN_MEM}/Repl.cpp            # Enhanced REPL
)

# ── Executable ────────────────────────────────────────────────────────────────
add_executable(chn
    ${CHN_C_SOURCES}
    ${CHN_CXX_SOURCES}
)

# ── Include directories ───────────────────────────────────────────────────────
target_include_directories(chn PRIVATE
    ${CHN_SRC}     # common.h, vm.h, gc.h, …
    ${CHN_INC}     # MemoryTypes.h, RAII.h, MemoryHandler.h
)

# ── Per-source language overrides ─────────────────────────────────────────────
# Force C sources to C11 and C++ sources to C++17 regardless of any
# global CMAKE_*_STANDARD overrides from parent projects.
set_source_files_properties(${CHN_C_SOURCES}   PROPERTIES LANGUAGE C)
set_source_files_properties(${CHN_CXX_SOURCES} PROPERTIES LANGUAGE CXX)

# ── Compile options ───────────────────────────────────────────────────────────
target_compile_options(chn PRIVATE
    # Shared
    -Wall
    -Wextra

    # C-only suppressions (legacy typedef redefinitions are harmless)
    $<$<COMPILE_LANGUAGE:C>:
        $<$<C_COMPILER_ID:GNU>:   -Wno-redundant-decls>
        $<$<C_COMPILER_ID:Clang>: -Wno-typedef-redefinition>
    >

    # C++ requires exceptions ON so that ChnMemoryError can be thrown
    # from the OOM hook and caught in chn_vm_exec_safe / Repl.cpp.
    $<$<COMPILE_LANGUAGE:CXX>: -fexceptions>
)

# ── Compile definitions ───────────────────────────────────────────────────────
if(OpenSSL_FOUND)
    target_compile_definitions(chn PRIVATE HAVE_OPENSSL)
endif()

if(READLINE_LIB AND READLINE_INCLUDE)
    target_compile_definitions(chn PRIVATE HAVE_READLINE)
    target_include_directories(chn PRIVATE ${READLINE_INCLUDE})
    message(STATUS "CHN: readline found – REPL line-editing enabled")
else()
    message(STATUS "CHN: readline not found – REPL uses raw fgets fallback")
endif()

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(chn PRIVATE
    Threads::Threads
)

# Math (not needed on Windows/MSVC)
if(UNIX)
    target_link_libraries(chn PRIVATE m)
endif()

# dl (dynamic linking, needed by some native functions on Linux)
if(UNIX AND NOT APPLE)
    target_link_libraries(chn PRIVATE dl)
endif()

# OpenSSL
if(OpenSSL_FOUND)
    target_link_libraries(chn PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

# Readline
if(READLINE_LIB AND READLINE_INCLUDE)
    target_link_libraries(chn PRIVATE ${READLINE_LIB})
endif()

# Windows sockets (needed by native_net.c)
if(WIN32)
    target_link_libraries(chn PRIVATE ws2_32 crypt32)
endif()

# ── Debug / Release presets ───────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

target_compile_options(chn PRIVATE
    $<$<CONFIG:Debug>:   -g -O0 -DDEBUG>
    $<$<CONFIG:Release>: -O2 -DNDEBUG>
)

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS chn DESTINATION bin)

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "  CHN ${PROJECT_VERSION}  configuration summary")
message(STATUS "  ─────────────────────────────────────────")
message(STATUS "  Build type  : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C compiler  : ${CMAKE_C_COMPILER}  (C${CMAKE_C_STANDARD})")
message(STATUS "  C++ compiler: ${CMAKE_CXX_COMPILER}  (C++${CMAKE_CXX_STANDARD})")
message(STATUS "  OpenSSL     : ${OpenSSL_FOUND}")
message(STATUS "  Readline    : ${READLINE_LIB}")
message(STATUS "  Install to  : ${CMAKE_INSTALL_PREFIX}/bin")
message(STATUS "")
