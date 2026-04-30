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
    if (nc)                              { g_color = 0; return 0; }
    if (term && strcmp(term,"dumb")==0)  { g_color = 0; return 0; }
    g_color = isatty(STDERR_FILENO) ? 1 : 0;
    return g_color;
}

void error_disable_color(void) { g_color = 0; }
void error_enable_color (void) { g_color = 1; }

#define C(seq) (colors_enabled() ? (seq) : "")
#define RED    C("\033[1;31m")
#define YELLOW C("\033[1;33m")
#define CYAN   C("\033[1;36m")
#define BOLD   C("\033[1m")
#define DIM    C("\033[2m")
#define RESET  C("\033[0m")

const char *g_source_file = "<unknown>";
const char *g_source_code = "";
bool        g_had_error       = false;
bool        g_suppress_errors = false;

void error_init(const char *file, const char *source) {
    g_source_file = file   ? file   : "<unknown>";
    g_source_code = source ? source : "";
    g_had_error   = false;
}

/* Extract the text of line `lineno` (1-based) into `out`. */
static void get_line(int lineno, char *out, int max) {
    if (!g_source_code || lineno < 1) { out[0] = '\0'; return; }
    const char *p = g_source_code;
    int cur = 1;
    while (*p && cur < lineno) { if (*p++ == '\n') cur++; }
    int i = 0;
    while (*p && *p != '\n' && i < max-1) out[i++] = *p++;
    out[i] = '\0';
}

/*
 * print_diagnostic - unified error/warning printer.
 *
 * Format:
 *
 *   error: <message>
 *
 *     file.chn:12:7
 *     let x = bad_token + 1
 *             ^^^^^^^^^
 *
 *     expected: "expression"
 *     hint: check for missing operand
 */
static void print_diagnostic(
        const char *level_color, const char *level_label,
        int line, int col, int tok_len,
        const char *msg,
        const char *expected, const char *hint)
{
    g_had_error = true;
    if (g_suppress_errors) return;

    char src_line[1024];
    get_line(line, src_line, sizeof(src_line));

    /* Expand tabs to spaces so caret lines up correctly */
    char expanded[1024];
    int ei = 0;
    for (int i = 0; src_line[i] && ei < (int)sizeof(expanded)-1; i++) {
        if (src_line[i] == '\t') {
            int spaces = 4 - (ei % 4);
            for (int s = 0; s < spaces && ei < (int)sizeof(expanded)-1; s++)
                expanded[ei++] = ' ';
        } else {
            expanded[ei++] = src_line[i];
        }
    }
    expanded[ei] = '\0';

    /* Adjust col for tab expansion in the prefix */
    int adj_col = 0;
    if (col > 0) {
        adj_col = 0;
        for (int i = 0; i < col-1 && src_line[i]; i++) {
            if (src_line[i] == '\t') adj_col += 4 - (adj_col % 4);
            else                     adj_col++;
        }
        adj_col++; /* 1-based */
    }

    /* Header line */
    fprintf(stderr, "%s%s:%s %s\n", level_color, level_label, RESET, msg);

    if (line > 0) {
        fprintf(stderr, "\n");

        /* Location tag */
        fprintf(stderr, "  %s%s:%d%s%s\n",
            CYAN, g_source_file, line,
            (col > 0) ? "" : "",
            RESET);

        /* Source line */
        if (expanded[0]) {
            fprintf(stderr, "  %s%s%s\n", DIM, expanded, RESET);

            /* Caret line */
            if (adj_col > 0 && tok_len > 0) {
                char caret[1024];
                int caret_len = tok_len < 512 ? tok_len : 512;
                int pad = adj_col - 1;
                if (pad < 0) pad = 0;
                if (pad > 512) pad = 512;
                memset(caret, ' ', (size_t)pad);
                memset(caret + pad, '^', (size_t)caret_len);
                caret[pad + caret_len] = '\0';
                fprintf(stderr, "  %s%s%s\n", level_color, caret, RESET);
            }
        }
    }

    if (expected && expected[0])
        fprintf(stderr, "\n  expected: %s\"%s\"%s\n", YELLOW, expected, RESET);

    if (hint && hint[0])
        fprintf(stderr, "  hint: %s%s%s\n", CYAN, hint, RESET);

    fprintf(stderr, "\n");
}

void error_lex(int line, int col, int len, const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap,fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(RED, "error", line, col, len, msg, NULL, NULL);
}

void error_parse(int line, int col, int len,
                 const char *expected, const char *hint,
                 const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap,fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(RED, "error", line, col, len, msg, expected, hint);
}

void error_compile(int line, int col, int len, const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap,fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(RED, "error", line, col, len, msg, NULL, NULL);
}

void warn_compile(int line, int col, int len, const char *fmt, ...) {
    bool saved = g_had_error;
    char msg[512]; va_list ap; va_start(ap,fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(YELLOW, "warning", line, col, len, msg, NULL, NULL);
    g_had_error = saved; /* warnings don't set had_error */
}

void error_runtime(int line, const char *fmt, ...) {
    char msg[512]; va_list ap; va_start(ap,fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    print_diagnostic(RED, "error", line, 0, 0, msg, NULL, NULL);
}

static int levenshtein(const char *a, const char *b) {
    int la=(int)strlen(a), lb=(int)strlen(b);
    if(la>64||lb>64) return 99;
    int dp[65][65];
    for(int i=0;i<=la;i++) dp[i][0]=i;
    for(int j=0;j<=lb;j++) dp[0][j]=j;
    for(int i=1;i<=la;i++)
        for(int j=1;j<=lb;j++){
            int cost=(a[i-1]==b[j-1])?0:1;
            int del=dp[i-1][j]+1, ins=dp[i][j-1]+1, sub=dp[i-1][j-1]+cost;
            dp[i][j]=del<ins?(del<sub?del:sub):(ins<sub?ins:sub);
        }
    return dp[la][lb];
}

const char *best_match(const char *word, const char **candidates,
                       int n_cands, int *out_dist) {
    const char *best=NULL;
    int best_d=DID_YOU_MEAN_THRESHOLD+1;
    for(int i=0;i<n_cands;i++){
        int d=levenshtein(word,candidates[i]);
        if(d<best_d){ best_d=d; best=candidates[i]; }
    }
    if(out_dist) *out_dist=best_d;
    return (best_d<=DID_YOU_MEAN_THRESHOLD)?best:NULL;
}
