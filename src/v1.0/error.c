

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_color = -1;

static int colors_enabled(void) {
    if (g_color >= 0) return g_color;
    const char *nc   = getenv("NO_COLOR");
    const char *term = getenv("TERM");
    if (nc)                             { g_color = 0; return 0; }
    if (term && strcmp(term,"dumb")==0) { g_color = 0; return 0; }
    g_color = isatty(STDERR_FILENO) ? 1 : 0;
    return g_color;
}

void error_disable_color(void) { g_color = 0; }
void error_enable_color (void) { g_color = 1; }

#define C(seq) (colors_enabled() ? (seq) : "")
#define RED     C("")
#define YELLOW  C("")
#define CYAN    C("")
#define BOLD    C("")
#define DIM     C("")
#define RESET   C("")

const char *g_source_file = "<unknown>";
const char *g_source_code = "";
bool        g_had_error       = false;
bool        g_suppress_errors = false;

void error_init(const char *file, const char *source) {
    g_source_file = file   ? file   : "<unknown>";
    g_source_code = source ? source : "";
    g_had_error   = false;
}

static void get_line(int lineno, char *out, int max) {
    if (!g_source_code || lineno < 1) { out[0] = '\0'; return; }
    const char *p = g_source_code;
    int cur = 1;
    while (*p && cur < lineno) { if (*p++ == '\n') cur++; }
    int i = 0;
    while (*p && *p != '\n' && i < max-1) out[i++] = *p++;
    out[i] = '\0';
}

static int expand_tabs(const char *src_line, char *out, int out_max, int col) {
    int ei = 0;
    for (int i = 0; src_line[i] && ei < out_max-1; i++) {
        if (src_line[i] == '\t') {
            int spaces = 4 - (ei % 4);
            for (int s = 0; s < spaces && ei < out_max-1; s++)
                out[ei++] = ' ';
        } else {
            out[ei++] = src_line[i];
        }
    }
    out[ei] = '\0';

    
    if (col <= 0) return 0;
    int adj = 0;
    for (int i = 0; i < col-1 && src_line[i]; i++) {
        if (src_line[i] == '\t') adj += 4 - (adj % 4);
        else                     adj++;
    }
    return adj + 1; 
}

static const char *kind_label(ChnErrorKind kind) {
    switch (kind) {
        case ERR_SYNTAX:         return "ChnSyntaxError";
        case ERR_REFERENCE:      return "ChnReferenceError";
        case ERR_TYPE:           return "ChnTypeError";
        case ERR_ACCESS:         return "ChnAccessError";
        case ERR_IMPORT:         return "ChnImportError";
        case ERR_RANGE:          return "ChnRangeError";
        case ERR_ARITHMETIC:     return "ChnArithmeticError";
        case ERR_INDEX:          return "ChnIndexError";
        case ERR_STACK_OVERFLOW: return "ChnStackOverflowError";
        case ERR_MEMORY:         return "ChnMemoryError";
        case ERR_RUNTIME:        return "ChnRuntimeError";
        case ERR_WARNING:        return "ChnWarning";
        default:                 return "ChnError";
    }
}

static const char *kind_color(ChnErrorKind kind) {
    if (kind == ERR_WARNING) return YELLOW;
    return RED;
}

static void print_diagnostic(
        ChnErrorKind kind,
        int line, int col, int tok_len,
        const char *msg,
        const char *expected, const char *note)
{
    if (kind != ERR_WARNING) g_had_error = true;
    if (g_suppress_errors) return;

    const char *kcolor = kind_color(kind);
    const char *klabel = kind_label(kind);

    
    fprintf(stderr, "%s%s%s: %s\n", kcolor, klabel, RESET, msg);

    if (line > 0) {
        char src_line[1024];
        get_line(line, src_line, sizeof(src_line));

        char expanded[1024];
        int adj_col = expand_tabs(src_line, expanded, sizeof(expanded), col);

        
        int ln_width = (line >= 1000) ? 4 : (line >= 100) ? 3 : (line >= 10) ? 2 : 2;

        
        if (col > 0)
            fprintf(stderr, "    %sat %s%s%s:%d:%d\n",
                    DIM, RESET, CYAN, g_source_file, line, col);
        else
            fprintf(stderr, "    %sat %s%s%s:%d\n",
                    DIM, RESET, CYAN, g_source_file, line);

        if (expanded[0]) {
            
            fprintf(stderr, "  %*d | %s%s%s\n",
                    ln_width, line, DIM, expanded, RESET);

            
            if (adj_col > 0 && tok_len > 0) {
                int caret_len = tok_len < 512 ? tok_len : 512;
                int pad = adj_col - 1;
                if (pad < 0)   pad = 0;
                if (pad > 512) pad = 512;

                char caret[1024];
                memset(caret, ' ', (size_t)pad);
                memset(caret + pad, '^', (size_t)caret_len);
                caret[pad + caret_len] = '\0';

                
                fprintf(stderr, "  %*s | %s%s%s\n",
                        ln_width, "", kcolor, caret, RESET);
            }
        }
    }

    
    if (expected && expected[0])
        fprintf(stderr, "    %sexpected%s: %s\"%s\"%s\n",
                BOLD, RESET, YELLOW, expected, RESET);

    if (note && note[0])
        fprintf(stderr, "    %snote%s: %s%s%s\n",
                BOLD, RESET, CYAN, note, RESET);

    fprintf(stderr, "\n");
}

void error_lex(int line, int col, int len, const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(ERR_SYNTAX, line, col, len, msg, NULL, NULL);
}

void error_parse(int line, int col, int len,
                 const char *expected, const char *note,
                 const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(ERR_SYNTAX, line, col, len, msg, expected, note);
}

void error_compile(int line, int col, int len, const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(ERR_SYNTAX, line, col, len, msg, NULL, NULL);
}

void error_compile_ref(int line, int col, int len, const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(ERR_REFERENCE, line, col, len, msg, NULL, NULL);
}

void error_compile_type(int line, int col, int len, const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(ERR_TYPE, line, col, len, msg, NULL, NULL);
}

void error_compile_access(int line, int col, int len, const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(ERR_ACCESS, line, col, len, msg, NULL, NULL);
}

void error_compile_import(int line, int col, int len, const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(ERR_IMPORT, line, col, len, msg, NULL, NULL);
}

void error_compile_range(int line, int col, int len, const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(ERR_RANGE, line, col, len, msg, NULL, NULL);
}

void warn_compile(int line, int col, int len, const char *fmt, ...) {
    bool saved = g_had_error;
    char msg[512]; va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(ERR_WARNING, line, col, len, msg, NULL, NULL);
    g_had_error = saved; 
}

void error_runtime(int line, const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(ERR_RUNTIME, line, 0, 0, msg, NULL, NULL);
}

void error_runtime_type(int line, const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(ERR_TYPE, line, 0, 0, msg, NULL, NULL);
}

void error_runtime_index(int line, const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(ERR_INDEX, line, 0, 0, msg, NULL, NULL);
}

void error_runtime_arith(int line, const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(ERR_ARITHMETIC, line, 0, 0, msg, NULL, NULL);
}

void error_runtime_stack(int line, const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(ERR_STACK_OVERFLOW, line, 0, 0, msg, NULL, NULL);
}

void error_runtime_mem(int line, const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(ERR_MEMORY, line, 0, 0, msg, NULL, NULL);
}

static int levenshtein(const char *a, const char *b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    if (la > 64 || lb > 64) return 99;
    int dp[65][65];
    for (int i = 0; i <= la; i++) dp[i][0] = i;
    for (int j = 0; j <= lb; j++) dp[0][j] = j;
    for (int i = 1; i <= la; i++)
        for (int j = 1; j <= lb; j++) {
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            int del = dp[i-1][j]+1, ins = dp[i][j-1]+1, sub = dp[i-1][j-1]+cost;
            dp[i][j] = del < ins ? (del < sub ? del : sub) : (ins < sub ? ins : sub);
        }
    return dp[la][lb];
}

const char *best_match(const char *word, const char **candidates,
                       int n_cands, int *out_dist) {
    const char *best = NULL;
    int best_d = DID_YOU_MEAN_THRESHOLD + 1;
    for (int i = 0; i < n_cands; i++) {
        int d = levenshtein(word, candidates[i]);
        if (d < best_d) { best_d = d; best = candidates[i]; }
    }
    if (out_dist) *out_dist = best_d;
    return (best_d <= DID_YOU_MEAN_THRESHOLD) ? best : NULL;
}
