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

#ifndef ERROR_H
#define ERROR_H

#include <stdbool.h>
#include <stdarg.h>

extern const char *g_source_file;
extern const char *g_source_code;
extern bool        g_had_error;
extern bool        g_suppress_errors;

void error_init        (const char *file, const char *source);
void error_disable_color(void);
void error_enable_color (void);

/* col = 1-based column of the offending token (0 = unknown, no caret drawn) */
void error_lex    (int line, int col, int len, const char *fmt, ...);
void error_parse  (int line, int col, int len,
                   const char *expected, const char *hint,
                   const char *fmt, ...);
void error_compile(int line, int col, int len, const char *fmt, ...);
void warn_compile (int line, int col, int len, const char *fmt, ...);
void error_runtime(int line, const char *fmt, ...);

const char *best_match(const char *word,
                       const char **candidates, int n_cands,
                       int *out_dist);

#define DID_YOU_MEAN_THRESHOLD 3

#endif
