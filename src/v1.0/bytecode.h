

#ifndef BYTECODE_H
#define BYTECODE_H

#include "common.h"
#include "func.h"
#include "compiler.h"

#define CHNO_MAGIC        0x43484E4Fu   
#define CHNO_VERSION      7
#define CHNO_PROG_FLAG    0x0001        

#define MAGIC_CHNF        0x43483346u

typedef enum {
    SECT_IMPORTS = 1,
    SECT_EXPORTS = 2,
    SECT_FUNCS   = 3,
    SECT_MAIN    = 4,
    SECT_SYMBOLS = 5,
    SECT_DEBUG     = 6,
    SECT_INTERNAL  = 7,   
} SectionType;

typedef enum {
    CTAG_INT     = 0,   
    CTAG_FLOAT   = 1,   
    CTAG_STRING  = 2,   
    CTAG_BOOL    = 3,   
    CTAG_NIL     = 4,   
    CTAG_FUNCREF = 5,   
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
    BC_ERR_SECTION,    
    BC_ERR_CCO,        
} BCResult;

const char *bc_result_str(BCResult r);

#define CCO_MAGIC          0x43434F00u
#define CCO_VERSION        3           
#define CCO_VERSION_V2     2           
#define CCO_VERSION_LEGACY 1           
#define CCO_FLAG_HAS_ENTRY 0x0001      
#define CCO_NO_ENTRY       0xFFFFu     

BCResult bc_write_program  (const char *path, Compiler *C);
BCResult bc_write_functions(const char *path, Compiler *C);

BCResult bc_write_cco(const char *path,
                      Compiler **compilers,
                      const char **src_names,
                      int count);

BCResult bc_read_imports   (const char *path, char out_imps[][256], int *out_nimp);
BCResult bc_read_program   (const char *path, Chunk *out_chunk,
                             FunctionObject ***out_funcs, int *out_nf,
                             char out_imps[][256], int *out_nimp);
BCResult bc_read_functions (const char *path, FunctionObject ***out_funcs, int *out_nf);

BCResult bc_read_cco(const char *path,
                     FunctionObject ***out_funcs,
                     int *out_nf);

BCResult bc_read_cco_entry(const char *path,
                            FunctionObject ***out_funcs, int *out_nf,
                            Chunk *out_chunk, bool *out_has_entry);

#endif 

extern bool g_minify;   

BCResult bc_read_cco_entry_imports(const char *path,
                                   char out_imps[][256], int *out_nimp);
