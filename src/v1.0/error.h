

#ifndef ERROR_H
#define ERROR_H

#include <stdbool.h>
#include <stdarg.h>

extern const char *g_source_file;
extern const char *g_source_code;
extern bool        g_had_error;
extern bool        g_suppress_errors;

typedef enum {
    ERR_SYNTAX,         
    ERR_REFERENCE,      
    ERR_TYPE,           
    ERR_ACCESS,         
    ERR_IMPORT,         
    ERR_RANGE,          
    ERR_ARITHMETIC,     
    ERR_INDEX,          
    ERR_STACK_OVERFLOW, 
    ERR_MEMORY,         
    ERR_RUNTIME,        
    ERR_WARNING,        
} ChnErrorKind;

void error_init         (const char *file, const char *source);
void error_disable_color(void);
void error_enable_color (void);

void error_lex(int line, int col, int len, const char *fmt, ...);

void error_parse(int line, int col, int len,
                 const char *expected, const char *note,
                 const char *fmt, ...);

void error_compile       (int line, int col, int len, const char *fmt, ...); 
void error_compile_ref   (int line, int col, int len, const char *fmt, ...); 
void error_compile_type  (int line, int col, int len, const char *fmt, ...); 
void error_compile_access(int line, int col, int len, const char *fmt, ...); 
void error_compile_import(int line, int col, int len, const char *fmt, ...); 
void error_compile_range (int line, int col, int len, const char *fmt, ...); 
void warn_compile        (int line, int col, int len, const char *fmt, ...); 

void error_runtime      (int line, const char *fmt, ...); 
void error_runtime_type (int line, const char *fmt, ...); 
void error_runtime_index(int line, const char *fmt, ...); 
void error_runtime_arith(int line, const char *fmt, ...); 
void error_runtime_stack(int line, const char *fmt, ...); 
void error_runtime_mem  (int line, const char *fmt, ...); 

const char *best_match(const char *word,
                       const char **candidates, int n_cands,
                       int *out_dist);

#define DID_YOU_MEAN_THRESHOLD 3

#endif 
