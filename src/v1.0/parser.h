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

#ifndef PARSER_H
#define PARSER_H
#include "common.h"
#include "lexer.h"
#include "ast.h"
#include "error.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    Lexer lexer;
    Token current;
    Token lookahead;
    bool  panic_mode;
} Parser;

void     parser_init (Parser *P, const char *source);
ASTNode *parser_parse(Parser *P);
#endif

/* Parse a single expression from a source string. Used by f-string interpolation. */
ASTNode *parser_parse_expr(const char *source);
