

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

ASTNode *parser_parse_expr(const char *source);
