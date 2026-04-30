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

#ifndef BYTECODE_H
#define BYTECODE_H

#include "common.h"
#include "func.h"
#include "compiler.h"

#define CHNO_MAGIC        0x43484E4Fu   /* "CHNO" */
#define CHNO_VERSION      7
#define CHNO_PROG_FLAG    0x0001        /* file has an entry main */

/* Legacy .chn3/.chno function-bundle format */
#define MAGIC_CHNF        0x43483346u

typedef enum {
    SECT_IMPORTS = 1,
    SECT_EXPORTS = 2,
    SECT_FUNCS   = 3,
    SECT_MAIN    = 4,
    SECT_SYMBOLS = 5,
    SECT_DEBUG     = 6,
    SECT_INTERNAL  = 7,   /* 1.0: non-exported bundle-internal functions */
} SectionType;

typedef enum {
    CTAG_INT     = 0,   /* int64   whole number               */
    CTAG_FLOAT   = 1,   /* ieee754 fractional / large number  */
    CTAG_STRING  = 2,   /* length-prefixed UTF-8              */
    CTAG_BOOL    = 3,   /* uint8 0=false 1=true               */
    CTAG_NIL     = 4,   /* no payload                         */
    CTAG_FUNCREF = 5,   /* length-prefixed function name      */
} ConstTag;

#define COFLAG_ENTRY    0x0001
#define COFLAG_EXPORTED 0x0002
#define COFLAG_PRIVATE  0x0004

typedef enum {
    BC_OK=0,
    BC_ERR_IO,
    BC_ERR_MAGIC,
    BC_ERR_VERSION,
    BC_ERR_CHECKSUM,
    BC_ERR_TRUNCATED,
    BC_ERR_OOM,
    BC_ERR_SECTION,    /* required section missing */
    BC_ERR_CCO,        /* 1.0: malformed CCO container   */
} BCResult;

const char *bc_result_str(BCResult r);

/*
 * FILE FORMAT (.cco) - v1 (8 bytes, library-only, CHN 4.0)
 *   magic       : uint32  = 0x43434F00  ("CCO\0")
 *   version     : uint16  = 1
 *   unit_count  : uint16
 *
 * FILE FORMAT (.cco) - v2 (12 bytes, runnable entry support, CHN 4.1)
 *   magic       : uint32  = 0x43434F00  ("CCO\0")
 *   version     : uint16  = 2
 *   flags       : uint16  (CCO_FLAG_HAS_ENTRY = 0x0001)
 *   unit_count  : uint16
 *   entry_unit  : uint16  (index of unit containing SECT_MAIN, 0xFFFF = none)
 *
 *   [for each unit, sequentially:]
 *     name_len   : uint16  length of original source filename
 *     name       : name_len bytes
 *     chno_len   : uint32  byte size of embedded CHNO blob
 *     chno_data  : chno_len bytes  (SECT_FUNCS + SECT_EXPORTS, plus
 *                                   SECT_MAIN if this is the entry unit)
 *
 * Execution: ./chn4.1 package.cco
 *   - Loads all exports from all units (same as imp::lib)
 *   - If CCO_FLAG_HAS_ENTRY: locates entry unit, runs its SECT_MAIN chunk
 *   - If no entry: error "this CCO is a library, not a runnable program"
 */
#define CCO_MAGIC          0x43434F00u
#define CCO_VERSION        3           /* v4.2 bumped: SECT_INTERNAL support  */
#define CCO_VERSION_V2     2           /* v4.1 files still loadable           */
#define CCO_VERSION_LEGACY 1           /* v4.0 files still loadable           */
#define CCO_FLAG_HAS_ENTRY 0x0001      /* CCO contains a runnable entry       */
#define CCO_NO_ENTRY       0xFFFFu     /* entry_unit sentinel = no entry      */

BCResult bc_write_program  (const char *path, Compiler *C);
BCResult bc_write_functions(const char *path, Compiler *C);

/*
 * bc_write_cco - compile multiple source Compilers into one .cco bundle.
 *   compilers[]  : array of already-compiled Compiler structs
 *   src_names[]  : original source filenames (parallel to compilers[])
 *   count        : number of entries
 *   path         : output .cco file path
 */
BCResult bc_write_cco(const char *path,
                      Compiler **compilers,
                      const char **src_names,
                      int count);

BCResult bc_read_imports   (const char *path, char out_imps[][256], int *out_nimp);
BCResult bc_read_program   (const char *path, Chunk *out_chunk,
                             FunctionObject ***out_funcs, int *out_nf,
                             char out_imps[][256], int *out_nimp);
BCResult bc_read_functions (const char *path, FunctionObject ***out_funcs, int *out_nf);

/*
 * bc_read_cco - load all exported functions from a .cco bundle.
 *   Duplicate function names (first-wins) are silently skipped.
 *   Supports both CCO v1 (library-only) and v2 (with optional entry).
 */
BCResult bc_read_cco(const char *path,
                     FunctionObject ***out_funcs,
                     int *out_nf);

/*
 * bc_read_cco_entry - load a runnable CCO (v4.1).
 *   - out_funcs / out_nf  : all exported functions across all units
 *   - out_chunk           : entry main bytecode (valid only if *out_has_entry)
 *   - out_has_entry       : true if CCO_FLAG_HAS_ENTRY is set and SECT_MAIN found
 *   Returns BC_ERR_CCO if the entry flag is set but no SECT_MAIN is found.
 */
BCResult bc_read_cco_entry(const char *path,
                            FunctionObject ***out_funcs, int *out_nf,
                            Chunk *out_chunk, bool *out_has_entry);

#endif /* BYTECODE_H */

/*
 * When g_minify is true (default), bc_write_* applies aggressive bytecode
 * minification:
 *   - Symbol renaming:   function names -> short hash-based identifiers
 *   - String obfuscation: string constants XOR-encoded with a per-chunk key
 *   - Debug stripping:   co_varnames, co_lnotab, line table omitted
 *   - Constant dedup:    identical constants merged in the constant pool
 *   - Instruction encoding: bytecode stream XOR-encoded with a rolling seed
 *
 * --no-minify sets g_minify = false, disabling all of the above.
 * Runtime behaviour is identical in both modes.
 */
extern bool g_minify;   /* default: true - set by --no-minify flag */

/* Read SECT_IMPORTS from the entry unit of a .cco file.
 * Used by run_cco_file() to resolve external dependencies at runtime. */
BCResult bc_read_cco_entry_imports(const char *path,
                                   char out_imps[][256], int *out_nimp);
