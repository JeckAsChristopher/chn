

#ifndef LEXER_H
#define LEXER_H

#include "common.h"

typedef enum {
    TK_NUMBER, TK_STRING, TK_FSTRING, TK_IDENT,
    TK_TRUE, TK_FALSE, TK_NIL,
    TK_NULL, TK_ASYNC, TK_AWAIT, TK_STRUCT,   
    TK_DO,                                     
    TK_IN_OP,                                  
    TK_NOT_IN,                                 
    TK_LET, TK_VAR, TK_ARRAY,
    TK_IF, TK_ELSE,
    TK_WHILE, TK_FOR, TK_IN,
    TK_SWITCH, TK_CASE, TK_DEFAULT,
    TK_BREAK, TK_CONTINUE,
    TK_RETURN,
    TK_FUNC, TK_PUBLIC, TK_PRIVATE, TK_PROTECTED,
    TK_ENTRY,   
    TK_IMP, TK_EXPORT,
    TK_STDO, TK_STDI, TK_NATIVE_CALL,
    TK_TRY, TK_CATCH, TK_THROW,
    TK_TYPEOF,
    TK_DICT,

    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT, TK_STARSTAR,
    TK_PLUSPLUS,                           

    TK_PLUS_ASSIGN, TK_MINUS_ASSIGN, TK_STAR_ASSIGN,
    TK_SLASH_ASSIGN, TK_PERCENT_ASSIGN,
    TK_STARSTAR_ASSIGN,                        

    TK_AMP, TK_PIPE, TK_CARET, TK_TILDE,
    TK_LSHIFT, TK_RSHIFT,

    TK_EQ, TK_NEQ, TK_LT, TK_GT, TK_LE, TK_GE,
    TK_ASSIGN,

    TK_AND, TK_OR, TK_BANG,

    TK_QUESTION,
    TK_NULL_COAL,
    TK_ARROW,

    TK_DOT,
    TK_LPAREN, TK_RPAREN,
    TK_LBRACE, TK_RBRACE,
    TK_LBRACKET, TK_RBRACKET,
    TK_COMMA, TK_SEMICOLON, TK_COLON, TK_COLONCOLON, TK_NEWLINE,
    TK_EOF, TK_ERROR
} TokenKind;

typedef struct {
    TokenKind   kind;
    const char *start;
    int         length;
    int         line;
    int         col;
    union {
        double number;
        char   string[MAX_STRING_LEN];
    } value;
} Token;

typedef struct {
    const char *source;
    const char *start;
    const char *current;
    int         line;
    int         col;
} Lexer;

void        lexer_init     (Lexer *L, const char *source);
Token       lexer_next     (Lexer *L);
Token       lexer_peek     (Lexer *L);
const char *token_kind_name(TokenKind k);

#endif
