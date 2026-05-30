

#ifndef AST_H
#define AST_H

#include "common.h"
#include "lexer.h"

typedef enum {
    NODE_PROGRAM, NODE_BLOCK,
    NODE_NUMBER, NODE_STRING, NODE_FSTRING, NODE_BOOL, NODE_NIL,
    NODE_NULL,
    NODE_ARRAY_LITERAL, NODE_DICT_LITERAL,
    NODE_IDENT, NODE_ASSIGN, NODE_COMPOUND_ASSIGN,
    NODE_UNARY, NODE_BINARY,
    NODE_TERNARY, NODE_NULL_COAL, NODE_TYPEOF,
    NODE_CALL, NODE_METHOD_CALL, NODE_NATIVE_CALL,
    NODE_INDEX, NODE_INDEX_SET,
    NODE_FIELD_ACCESS, NODE_FIELD_SET,
    NODE_AWAIT,
    NODE_STRUCT_DECL,
    NODE_POST_INC, NODE_PRE_INC,
    NODE_DO_WHILE,
    
    NODE_LAMBDA,        
    NODE_CALL_EXPR,     
    NODE_VAR_DECL, NODE_ARRAY_DECL, NODE_DICT_DECL,
    NODE_FUNC_DECL,
    NODE_IF, NODE_WHILE, NODE_FOR, NODE_FOREACH,
    NODE_SWITCH,
    NODE_RETURN, NODE_BREAK, NODE_CONTINUE,
    NODE_PRINT, NODE_INPUT,
    NODE_IMPORT, NODE_EXPORT,
    NODE_TRY_CATCH, NODE_THROW,
    NODE_EXPR_STMT,
} NodeKind;

typedef struct SwitchCase {
    struct ASTNode *value;
    struct ASTNode *body;
} SwitchCase;

typedef struct ASTNode {
    NodeKind kind;
    int      line;
    int      col;
    int      tok_len;
    union {
        struct { double value; }            num;
        struct { char *value; }             str;
        struct { bool value; }              boolean;
        struct { char name[MAX_IDENT_LEN]; } ident;
        struct { char name[MAX_IDENT_LEN]; struct ASTNode *value; } assign;
        struct { char name[MAX_IDENT_LEN]; TokenKind op; struct ASTNode *value; } compound_assign;
        struct { TokenKind op; struct ASTNode *left, *right; } binary;
        struct { TokenKind op; struct ASTNode *operand; } unary;
        struct { struct ASTNode *cond, *then_val, *else_val; } ternary;
        struct { struct ASTNode *left, *right; } null_coal;
        struct { struct ASTNode *operand; } typeof_expr;
        struct { struct ASTNode *object_expr; char field[MAX_IDENT_LEN]; } field_access; 
        struct { struct ASTNode *object_expr; char field[MAX_IDENT_LEN]; struct ASTNode *value; } field_set; 
        struct { struct ASTNode *expr; } await_expr; 
        struct {                                     
            char name[MAX_IDENT_LEN];
            char  fields[MAX_PARAMS][MAX_IDENT_LEN];
            struct ASTNode *defaults[MAX_PARAMS];   
            int   field_count;
        } struct_decl;
        struct { struct ASTNode *callee; struct ASTNode **args; int arg_count; } call_expr; 
        struct { struct ASTNode *body; struct ASTNode *condition; } do_while; 
        

        struct {
            char   params[MAX_PARAMS][MAX_IDENT_LEN];
            int    param_count;
            struct ASTNode *body;   
            bool   is_expr_body;    
        } lambda;
        struct { struct ASTNode **elements; int count; } arr_lit;
        struct { struct ASTNode **keys; struct ASTNode **values; int count; } dict_lit;
        struct { char name[MAX_IDENT_LEN]; struct ASTNode **args; int arg_count; } call;
        struct {
            struct ASTNode *object_expr;
            char method[MAX_IDENT_LEN];
            struct ASTNode **args; int arg_count;
        } method_call;
        struct { uint16_t call_id; struct ASTNode **args; int argc; } native_call;
        struct { struct ASTNode *object_expr, *index; } index;
        struct { struct ASTNode *object_expr, *index, *value; } index_set;
        struct { char name[MAX_IDENT_LEN]; struct ASTNode *initializer; bool is_let; } var_decl;
        struct { char name[MAX_IDENT_LEN]; struct ASTNode *initializer; } arr_decl;
        struct { char name[MAX_IDENT_LEN]; struct ASTNode *initializer; } dict_decl;
        struct {
            char name[MAX_IDENT_LEN];
            char params[MAX_PARAMS][MAX_IDENT_LEN];
            int  param_count;
            struct ASTNode *body;
            FunctionVisibility visibility;
            bool is_entry;   
            bool is_async;   
        } func_decl;
        struct { struct ASTNode *condition, *then_branch, *else_branch; } if_stmt;
        struct { struct ASTNode *condition, *body; } while_stmt;
        struct { struct ASTNode *init, *condition, *post, *body; } for_stmt;
        struct {
            char elem_name[MAX_IDENT_LEN];
            char idx_name[MAX_IDENT_LEN];
            bool has_index;
            struct ASTNode *iterable, *body;
        } foreach_stmt;
        struct {
            struct ASTNode *subject;
            SwitchCase *cases; int case_count;
            struct ASTNode *default_body;
        } switch_stmt;
        struct {
            struct ASTNode *try_body;
            char err_name[MAX_IDENT_LEN];
            struct ASTNode *catch_body;
        } try_catch;
        struct { struct ASTNode *value; } throw_stmt;
        struct { struct ASTNode **args; int arg_count; } print;
        struct { char target[MAX_IDENT_LEN]; struct ASTNode *prompt_expr; } input;
        struct { char path[1024]; bool is_lib; char alias[256]; } import;
        struct { char name[MAX_IDENT_LEN]; struct ASTNode *func_def; } export_node;
        struct { struct ASTNode *expr; } expr_stmt;
        struct { struct ASTNode **stmts; int count; } block;
        struct { struct ASTNode **stmts; int count; } program;
        struct { struct ASTNode *value; } ret;
        struct { char *raw_fmt; } fstring;
    };
} ASTNode;

ASTNode *node_alloc(NodeKind kind, int line);
void     ast_free  (ASTNode *node);
void     ast_print (ASTNode *node, int indent);

#endif
