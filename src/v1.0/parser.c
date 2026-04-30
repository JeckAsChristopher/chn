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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "parser.h"
#include "native.h"   /* NATIVE_RANGE/STR/LEN builtin IDs */

typedef struct { ASTNode **data; int len, cap; } NL;
static void nl_push(NL *nl, ASTNode *n){
    if(nl->len>=nl->cap){
        nl->cap=nl->cap?nl->cap*2:8;
        nl->data=(ASTNode**)realloc(nl->data,(size_t)nl->cap*sizeof(ASTNode*));
        if(!nl->data){fprintf(stderr,"oom\n");exit(1);}
    }
    nl->data[nl->len++]=n;
}
static ASTNode *nl_to_block(NL *nl, int line){
    ASTNode *b=node_alloc(NODE_BLOCK,line);
    b->block.stmts=nl->data; b->block.count=nl->len;
    nl->data=NULL; nl->len=nl->cap=0;
    return b;
}

static void adv(Parser *P){ P->current=P->lookahead; P->lookahead=lexer_next(&P->lexer); }
static bool check(Parser *P, TokenKind k){ return P->current.kind==k; }
static bool check2(Parser *P, TokenKind k){ return P->lookahead.kind==k; }
static bool mat(Parser *P, TokenKind k){ if(!check(P,k)) return false; adv(P); return true; }
static Token consume(Parser *P, TokenKind k, const char *expected, const char *hint){
    if(check(P,k)){ Token t=P->current; adv(P); return t; }
    error_parse(P->current.line, P->current.col, P->current.length,expected,hint,"got %s",token_kind_name(P->current.kind));
    P->panic_mode=true; return P->current;
}
static void skip_nl(Parser *P){ while(check(P,TK_NEWLINE)) adv(P); }

/* multiline expression continuation.
 * After a binary operator, comma, or open bracket/paren, newlines are
 * transparent - the expression continues on the next line.  This lets:
 *
 *   stdo("Hello!" +
 *        "World!")
 *
 * and also:
 *
 *   var x = 1 +
 *            2 +
 *            3
 *
 * work naturally.  Called after consuming an operator or opening delimiter.
/*
 * Regular statement-level newlines still terminate statements as before.   */
static void skip_nl_continuation(Parser *P){ while(check(P,TK_NEWLINE)) adv(P); }

static void consume_stmt_end(Parser *P){
    if(check(P,TK_SEMICOLON)){adv(P);skip_nl(P);}
    else if(check(P,TK_NEWLINE)) skip_nl(P);
}

static ASTNode *parse_stmt(Parser *P);
static ASTNode *parse_var_decl(Parser *P, bool is_let);
static ASTNode *parse_array_decl(Parser *P);
static ASTNode *parse_dict_decl(Parser *P);
static ASTNode *parse_func_decl(Parser *P, FunctionVisibility vis, bool is_async);
static ASTNode *parse_struct_decl(Parser *P);  /* v3.5 */
static ASTNode *parse_if(Parser *P);
static ASTNode *parse_while(Parser *P);
static ASTNode *parse_for(Parser *P);
static ASTNode *parse_switch(Parser *P);
static ASTNode *parse_try_catch(Parser *P);
static ASTNode *parse_block(Parser *P);
static ASTNode *parse_stdo(Parser *P);
static ASTNode *parse_native_call(Parser *P);
static ASTNode *parse_stdi(Parser *P);
static ASTNode *parse_return(Parser *P);
static ASTNode *parse_import(Parser *P);
static ASTNode *parse_export(Parser *P);
static ASTNode *parse_entry_main(Parser *P, FunctionVisibility vis);
static ASTNode *parse_expr(Parser *P);
static ASTNode *parse_assignment(Parser *P);
static ASTNode *parse_null_coal(Parser *P);
static ASTNode *parse_or(Parser *P);
static ASTNode *parse_and(Parser *P);
static ASTNode *parse_equality(Parser *P);
static ASTNode *parse_comparison(Parser *P);
static ASTNode *parse_bitwise_or(Parser *P);
static ASTNode *parse_bitwise_xor(Parser *P);
static ASTNode *parse_bitwise_and(Parser *P);
static ASTNode *parse_shift(Parser *P);
static ASTNode *parse_additive(Parser *P);
static ASTNode *parse_multiply(Parser *P);
static ASTNode *parse_power(Parser *P);
static ASTNode *parse_unary(Parser *P);
static ASTNode *parse_postfix(Parser *P);
static ASTNode *parse_primary(Parser *P);
static ASTNode *parse_lambda(Parser *P);   /* v3.7 */

void parser_init(Parser *P, const char *source){
    lexer_init(&P->lexer,source);
    P->panic_mode=false;
    P->current=lexer_next(&P->lexer);
    P->lookahead=lexer_next(&P->lexer);
}

ASTNode *parser_parse(Parser *P){
    skip_nl(P);
    NL nl={0};
    while(!check(P,TK_EOF)){
        if(P->panic_mode){
            while(!check(P,TK_EOF)&&!check(P,TK_NEWLINE)&&!check(P,TK_SEMICOLON)) adv(P);
            P->panic_mode=false; skip_nl(P); continue;
        }
        ASTNode *s=parse_stmt(P);
        if(s) nl_push(&nl,s);
        skip_nl(P);
    }
    ASTNode *prog=node_alloc(NODE_PROGRAM,1);
    prog->program.stmts=nl.data; prog->program.count=nl.len;
    return prog;
}

static ASTNode *parse_stmt(Parser *P){
    skip_nl(P);
    if(check(P,TK_EOF)) return NULL;

    /* visibility + entry main  e.g. "public entry main() { }" */
    if((check(P,TK_PUBLIC)||check(P,TK_PRIVATE)||check(P,TK_PROTECTED))&&check2(P,TK_ENTRY)){
        FunctionVisibility vis=check(P,TK_PUBLIC)?VIS_PUBLIC:check(P,TK_PROTECTED)?VIS_PROTECTED:VIS_PRIVATE;
        adv(P); adv(P); /* consume visibility + 'entry' */
        return parse_entry_main(P,vis);
    }
    /* bare "entry main() { }" without explicit visibility */
    if(check(P,TK_ENTRY)){ adv(P); return parse_entry_main(P,VIS_PUBLIC); }

    if((check(P,TK_PUBLIC)||check(P,TK_PRIVATE)||check(P,TK_PROTECTED))&&check2(P,TK_FUNC)){
        FunctionVisibility vis=check(P,TK_PUBLIC)?VIS_PUBLIC:check(P,TK_PROTECTED)?VIS_PROTECTED:VIS_PRIVATE;
        adv(P); adv(P);
        return parse_func_decl(P,vis,false);
    }
    if(check(P,TK_FUNC)){ adv(P); return parse_func_decl(P,VIS_PRIVATE,false); }

    /* async func [vis] name(...) { } */
    if(check(P,TK_ASYNC)){
        adv(P);
        FunctionVisibility vis2=VIS_PRIVATE;
        if(check(P,TK_PUBLIC)){ vis2=VIS_PUBLIC; adv(P); }
        else if(check(P,TK_PROTECTED)){ vis2=VIS_PROTECTED; adv(P); }
        else if(check(P,TK_PRIVATE)){ adv(P); }
        if(check(P,TK_FUNC)) adv(P);
        return parse_func_decl(P,vis2,true);
    }

    /* struct Name { field: default, ... } */
    if(check(P,TK_STRUCT)){ adv(P); return parse_struct_decl(P); }

    if(check(P,TK_LET))    { adv(P); return parse_var_decl(P,true);  }
    if(check(P,TK_VAR))    { adv(P); return parse_var_decl(P,false); }
    if(check(P,TK_ARRAY))  { adv(P); return parse_array_decl(P);     }
    if(check(P,TK_DICT))   { adv(P); return parse_dict_decl(P);      }
    if(check(P,TK_IF))     { adv(P); return parse_if(P);             }
    if(check(P,TK_WHILE))  { adv(P); return parse_while(P);          }
    if(check(P,TK_FOR))    { adv(P); return parse_for(P);            }
    /* do { body } while(cond) */
    if(check(P,TK_DO)){
        int line=P->current.line; adv(P); skip_nl(P);
        ASTNode *body=parse_block(P);
        skip_nl(P);
        consume(P,TK_WHILE,"'while'","do { } while(cond)");
        consume(P,TK_LPAREN,"'('","do { } while(cond)");
        ASTNode *cond=parse_expr(P);
        consume(P,TK_RPAREN,"')'","close do-while condition");
        consume_stmt_end(P);
        ASTNode *n=node_alloc(NODE_DO_WHILE,line);
    n->col = P->current.col; n->tok_len = P->current.length;
        n->do_while.body=body; n->do_while.condition=cond;
        return n;
    }
    if(check(P,TK_SWITCH)) { adv(P); return parse_switch(P);         }
    if(check(P,TK_TRY))    { adv(P); return parse_try_catch(P);      }
    if(check(P,TK_RETURN)) { adv(P); return parse_return(P);         }
    if(check(P,TK_LBRACE))          return parse_block(P);
    if(check(P,TK_STDO))   { adv(P); return parse_stdo(P);           }
    if(check(P,TK_STDI))   { adv(P); return parse_stdi(P);           }
    if(check(P,TK_IMP))    { adv(P); return parse_import(P);         }
    if(check(P,TK_EXPORT)) { adv(P); return parse_export(P);         }

    if(check(P,TK_THROW)){
        int line=P->current.line; adv(P);
        ASTNode *val=parse_expr(P);
        consume_stmt_end(P);
        ASTNode *n=node_alloc(NODE_THROW,line);
    n->col = P->current.col; n->tok_len = P->current.length;
        n->throw_stmt.value=val; return n;
    }
    if(check(P,TK_BREAK)){
        int line=P->current.line; adv(P); consume_stmt_end(P);
        return node_alloc(NODE_BREAK,line);
    }
    if(check(P,TK_CONTINUE)){
        int line=P->current.line; adv(P); consume_stmt_end(P);
        return node_alloc(NODE_CONTINUE,line);
    }

    ASTNode *expr=parse_expr(P);
    ASTNode *s=node_alloc(NODE_EXPR_STMT,expr?expr->line:0);
    s->expr_stmt.expr=expr;
    consume_stmt_end(P);
    return s;
}

static ASTNode *parse_func_decl(Parser *P, FunctionVisibility vis, bool is_async){
    int line=P->current.line;
    if(!check(P,TK_IDENT)){
        error_parse(line, P->current.col, P->current.length,"function name","func name(params) { }","expected function name");
        P->panic_mode=true; return NULL;
    }
    char name[MAX_IDENT_LEN];
    int nl2=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
    memcpy(name,P->current.start,nl2); name[nl2]='\0'; adv(P);
    consume(P,TK_LPAREN,"'()'","func name(params) { }");
    if(P->panic_mode) return NULL;
    char params[MAX_PARAMS][MAX_IDENT_LEN]; int pc=0;
    skip_nl(P);
    if(!check(P,TK_RPAREN)){
        do {
            skip_nl(P);
            if(pc>=MAX_PARAMS){ error_parse(P->current.line, P->current.col, P->current.length,NULL,NULL,"too many parameters"); P->panic_mode=true; return NULL; }
            if(!check(P,TK_IDENT)){ error_parse(P->current.line, P->current.col, P->current.length,"param name",NULL,"expected param"); P->panic_mode=true; return NULL; }
            int plen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
            memcpy(params[pc],P->current.start,plen); params[pc][plen]='\0'; pc++;
            adv(P);
        } while(mat(P,TK_COMMA));
    }
    consume(P,TK_RPAREN,"')'","close params");
    skip_nl(P);
    ASTNode *body=parse_block(P);
    ASTNode *n=node_alloc(NODE_FUNC_DECL,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    strncpy(n->func_decl.name,name,MAX_IDENT_LEN-1);
    for(int i=0;i<pc;i++) strncpy(n->func_decl.params[i],params[i],MAX_IDENT_LEN-1);
    n->func_decl.param_count=pc;
    n->func_decl.body=body;
    n->func_decl.visibility=vis;
    n->func_decl.is_async=is_async;
    return n;
}

/* v3.5 ---------------------------------------------------------------------
 * parse_struct_decl - parses:
 *   struct Name {
 *       field: default_expr,
 *       ...
 *   }
 * Compiled as a constructor function Name(...) that returns a dict.
*/

static ASTNode *parse_struct_decl(Parser *P){
    int line=P->current.line;
    if(!check(P,TK_IDENT)){
        error_parse(line, P->current.col, P->current.length,"struct name","struct Point { x: 0, y: 0 }","expected struct name");
        P->panic_mode=true; return NULL;
    }
    char name[MAX_IDENT_LEN];
    int nl2=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
    memcpy(name,P->current.start,(size_t)nl2); name[nl2]='\0'; adv(P);
    skip_nl(P);
    consume(P,TK_LBRACE,"'{'","struct Name { field: default }");
    if(P->panic_mode) return NULL;

    ASTNode *n=node_alloc(NODE_STRUCT_DECL,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    strncpy(n->struct_decl.name,name,MAX_IDENT_LEN-1);
    n->struct_decl.field_count=0;
    memset(n->struct_decl.defaults,0,sizeof(n->struct_decl.defaults));

    skip_nl(P);
    while(!check(P,TK_RBRACE)&&!check(P,TK_EOF)&&!P->panic_mode){
        if(n->struct_decl.field_count>=MAX_PARAMS){
            error_parse(P->current.line, P->current.col, P->current.length,NULL,NULL,"too many struct fields"); P->panic_mode=true; break;
        }
        if(!check(P,TK_IDENT)){
            error_parse(P->current.line, P->current.col, P->current.length,"field name","field: default_value","expected field name");
            P->panic_mode=true; break;
        }
        int fc=n->struct_decl.field_count;
        int fl=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
        memcpy(n->struct_decl.fields[fc],P->current.start,(size_t)fl);
        n->struct_decl.fields[fc][fl]='\0'; adv(P);

        /* default value after ':', otherwise nil */
        ASTNode *def=node_alloc(NODE_NIL,P->current.line);
        if(mat(P,TK_COLON)){ ast_free(def); def=parse_expr(P); }
        n->struct_decl.defaults[fc]=def;
        n->struct_decl.field_count++;
        skip_nl(P);
        if(!mat(P,TK_COMMA)) break;
        skip_nl(P);
    }
    consume(P,TK_RBRACE,"'}'","close struct body");
    consume_stmt_end(P);
    return n;
}

static ASTNode *parse_var_decl(Parser *P, bool is_let){
    int line=P->current.line;
    if(!check(P,TK_IDENT)){
        error_parse(line, P->current.col, P->current.length,"variable name","var name = value","expected name");
        P->panic_mode=true; return NULL;
    }
    char name[MAX_IDENT_LEN];
    int nlen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
    memcpy(name,P->current.start,nlen); name[nlen]='\0'; adv(P);
    ASTNode *init=mat(P,TK_ASSIGN)?parse_expr(P):node_alloc(NODE_NIL,line);
    consume_stmt_end(P);
    ASTNode *n=node_alloc(NODE_VAR_DECL,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    strncpy(n->var_decl.name,name,MAX_IDENT_LEN-1);
    n->var_decl.initializer=init; n->var_decl.is_let=is_let;
    return n;
}

static ASTNode *parse_array_decl(Parser *P){
    int line=P->current.line;
    if(!check(P,TK_IDENT)){ error_parse(line, P->current.col, P->current.length,"array name","array myList = [1,2,3]","expected name"); P->panic_mode=true; return NULL; }
    char name[MAX_IDENT_LEN];
    int nlen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
    memcpy(name,P->current.start,nlen); name[nlen]='\0'; adv(P);
    ASTNode *init=NULL;
    if(mat(P,TK_ASSIGN)){
        if(check(P,TK_LBRACKET)){
            adv(P); NL elems={0}; skip_nl(P);
            if(!check(P,TK_RBRACKET)){
                do{ skip_nl(P); ASTNode *e=parse_expr(P); if(e) nl_push(&elems,e); skip_nl(P); } while(mat(P,TK_COMMA));
            }
            consume(P,TK_RBRACKET,"']'","close array literal");
            ASTNode *al=node_alloc(NODE_ARRAY_LITERAL,line);
            al->arr_lit.elements=elems.data; al->arr_lit.count=elems.len; init=al;
        } else { init=parse_expr(P); }
    } else {
        ASTNode *al=node_alloc(NODE_ARRAY_LITERAL,line);
        al->arr_lit.elements=NULL; al->arr_lit.count=0; init=al;
    }
    consume_stmt_end(P);
    ASTNode *n=node_alloc(NODE_ARRAY_DECL,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    strncpy(n->arr_decl.name,name,MAX_IDENT_LEN-1);
    n->arr_decl.initializer=init; return n;
}

static ASTNode *parse_dict_decl(Parser *P){
    int line=P->current.line;
    if(!check(P,TK_IDENT)){ error_parse(line, P->current.col, P->current.length,"dict name","dict myMap = {}","expected name"); P->panic_mode=true; return NULL; }
    char name[MAX_IDENT_LEN];
    int nlen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
    memcpy(name,P->current.start,nlen); name[nlen]='\0'; adv(P);
    ASTNode *init=NULL;
    if(mat(P,TK_ASSIGN)) init=parse_expr(P);
    else { ASTNode *dl=node_alloc(NODE_DICT_LITERAL,line); dl->dict_lit.keys=NULL; dl->dict_lit.values=NULL; dl->dict_lit.count=0; init=dl; }
    consume_stmt_end(P);
    ASTNode *n=node_alloc(NODE_DICT_DECL,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    strncpy(n->dict_decl.name,name,MAX_IDENT_LEN-1);
    n->dict_decl.initializer=init; return n;
}

static ASTNode *parse_return(Parser *P){
    int line=P->current.line;
    ASTNode *val=NULL;
    if(!check(P,TK_NEWLINE)&&!check(P,TK_SEMICOLON)&&!check(P,TK_EOF)&&!check(P,TK_RBRACE))
        val=parse_expr(P);
    consume_stmt_end(P);
    ASTNode *n=node_alloc(NODE_RETURN,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    n->ret.value=val; return n;
}

static ASTNode *parse_import(Parser *P){
    int line=P->current.line;
    ASTNode *n=node_alloc(NODE_IMPORT,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    n->import.is_lib = false;
    n->import.alias[0] = '\0';

    /* imp::lib my.cco  or  imp::lib my  - CCO bundle import */
    if(check(P,TK_COLONCOLON)){
        adv(P); /* consume :: */
        if(!check(P,TK_IDENT) || (strncmp(P->current.start,"lib",3)!=0 || P->current.length!=3)){
            error_parse(line, P->current.col, P->current.length,"'lib'","imp::lib myfile.cco","expected 'lib' after '::'");
            P->panic_mode=true; free(n); return NULL;
        }
        adv(P); /* consume 'lib' */
        n->import.is_lib = true;
    }

    if(check(P,TK_STRING)){ strncpy(n->import.path,P->current.value.string,1023); adv(P); }
    else if(check(P,TK_IDENT)){
        int len=P->current.length<1023?P->current.length:1023;
        memcpy(n->import.path,P->current.start,len); n->import.path[len]='\0'; adv(P);
        /* For imp::lib, allow a dotted extension:  mylibs.cco */
        if(n->import.is_lib && check(P,TK_DOT)){
            adv(P);
            if(check(P,TK_IDENT)){
                int plen=(int)strlen(n->import.path);
                int elen=P->current.length<(1022-plen)?P->current.length:(1022-plen);
                n->import.path[plen]='.';
                memcpy(n->import.path+plen+1,P->current.start,(size_t)elen);
                n->import.path[plen+1+elen]='\0';
                adv(P);
            } else {
                error_parse(line, P->current.col, P->current.length,"file extension","imp::lib myfile.cco","expected extension after '.'");
                P->panic_mode=true; return NULL;
            }
        }
    } else {
        error_parse(line, P->current.col, P->current.length,"module name","imp os  or  imp \"path.chn\"  or  imp::lib my.cco","expected module name");
        P->panic_mode=true; return NULL;
    }

    /* 1.0: optional  as <alias>  - imp::lib mymod as pkg */
    if(check(P,TK_IDENT) && P->current.length==2 &&
       strncmp(P->current.start,"as",2)==0){
        adv(P); /* consume 'as' */
        if(!check(P,TK_IDENT)){
            error_parse(line, P->current.col, P->current.length,"alias name","imp::lib mymod as pkg","expected alias name after 'as'");
            P->panic_mode=true; return NULL;
        }
        int alen=P->current.length<255?P->current.length:255;
        memcpy(n->import.alias,P->current.start,(size_t)alen);
        n->import.alias[alen]='\0';
        adv(P);
    }

    consume_stmt_end(P); return n;
}

static ASTNode *parse_export(Parser *P){
    int line=P->current.line;
    FunctionVisibility vis=VIS_PUBLIC;
    if(check(P,TK_PUBLIC)||check(P,TK_PRIVATE)||check(P,TK_PROTECTED)){
        vis=check(P,TK_PUBLIC)?VIS_PUBLIC:check(P,TK_PROTECTED)?VIS_PROTECTED:VIS_PRIVATE; adv(P);
    }
    if(check(P,TK_FUNC)){
        adv(P);
        ASTNode *fn=parse_func_decl(P,vis,false);
        if(!fn) return NULL;
        ASTNode *ex=node_alloc(NODE_EXPORT,line);
        strncpy(ex->export_node.name,fn->func_decl.name,MAX_IDENT_LEN-1);
        ex->export_node.func_def=fn; return ex;
    }
    if(!check(P,TK_IDENT)){ error_parse(line, P->current.col, P->current.length,"function name","export funcName","expected name"); P->panic_mode=true; return NULL; }
    ASTNode *n=node_alloc(NODE_EXPORT,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    int nlen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
    memcpy(n->export_node.name,P->current.start,nlen); n->export_node.name[nlen]='\0';
    n->export_node.func_def=NULL; adv(P); consume_stmt_end(P); return n;
}

static ASTNode *parse_native_call(Parser *P){
    int line=P->current.line;
    /* After '@', the lexer returns TK_NATIVE_CALL and parse_primary advances.
     * The word 'native' itself is still sitting as TK_IDENT - skip it. */
    if(check(P,TK_IDENT)) adv(P);
    consume(P,TK_LPAREN,"'('","@native(id, ...)");
    if(!check(P,TK_NUMBER)){ P->panic_mode=true; return NULL; }
    int call_id=(int)P->current.value.number; adv(P);
    ASTNode *n=node_alloc(NODE_NATIVE_CALL,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    n->native_call.call_id=(uint16_t)call_id;
    n->native_call.argc=0; n->native_call.args=NULL;
    NL args={0};
    while(check(P,TK_COMMA)){ adv(P); ASTNode *a=parse_expr(P); if(a) nl_push(&args,a); }
    consume(P,TK_RPAREN,"')'","close @native args");
    n->native_call.args=args.data; n->native_call.argc=args.len;
    return n;
}

static ASTNode *parse_stdo(Parser *P){
    int line=P->current.line;
    consume(P,TK_LPAREN,"'('","stdo(expr)");
    if(P->panic_mode) return NULL;
    NL args={0};
    if(!check(P,TK_RPAREN)){
        do{ skip_nl(P); ASTNode *a=parse_expr(P); if(a) nl_push(&args,a); } while(mat(P,TK_COMMA));
    }
    consume(P,TK_RPAREN,"')'","close stdo with ')'");
    consume_stmt_end(P);
    ASTNode *n=node_alloc(NODE_PRINT,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    n->print.args=args.data; n->print.arg_count=args.len;
    return n;
}

static ASTNode *parse_stdi(Parser *P){
    int line=P->current.line;
    consume(P,TK_LPAREN,"'('","stdi(var)");
    if(P->panic_mode) return NULL;
    if(!check(P,TK_IDENT)){ error_parse(P->current.line, P->current.col, P->current.length,"variable name","stdi(myVar)","expected variable"); P->panic_mode=true; return NULL; }
    char name[MAX_IDENT_LEN];
    int nlen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
    memcpy(name,P->current.start,nlen); name[nlen]='\0'; adv(P);
    ASTNode *prompt=NULL;
    if(mat(P,TK_COMMA)) prompt=parse_expr(P);
    consume(P,TK_RPAREN,"')'","close stdi");
    consume_stmt_end(P);
    ASTNode *n=node_alloc(NODE_INPUT,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    strncpy(n->input.target,name,MAX_IDENT_LEN-1);
    n->input.prompt_expr=prompt; return n;
}

static ASTNode *parse_if(Parser *P){
    int line=P->current.line;
    bool has_paren_if=mat(P,TK_LPAREN);
    ASTNode *cond=parse_expr(P);
    if(has_paren_if) consume(P,TK_RPAREN,"')'","close if condition");
    skip_nl(P);
    ASTNode *then_b=parse_block(P);
    ASTNode *else_b=NULL;
    skip_nl(P);
    if(mat(P,TK_ELSE)){
        skip_nl(P);
        else_b=check(P,TK_IF)?(adv(P),parse_if(P)):parse_block(P);
    }
    ASTNode *n=node_alloc(NODE_IF,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    n->if_stmt.condition=cond; n->if_stmt.then_branch=then_b; n->if_stmt.else_branch=else_b;
    return n;
}

static ASTNode *parse_while(Parser *P){
    int line=P->current.line;
    bool has_paren=mat(P,TK_LPAREN);
    if(P->panic_mode) return NULL;
    ASTNode *cond=parse_expr(P);
    if(has_paren) consume(P,TK_RPAREN,"')'","close while condition");
    skip_nl(P);
    ASTNode *body=parse_block(P);
    ASTNode *n=node_alloc(NODE_WHILE,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    n->while_stmt.condition=cond; n->while_stmt.body=body; return n;
}

static ASTNode *parse_for(Parser *P){
    int line=P->current.line;
    
    
    
    if(check(P,TK_IDENT)){
        
        
        
        char first_name[MAX_IDENT_LEN];
        int flen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
        memcpy(first_name,P->current.start,flen); first_name[flen]='\0';

        
        Token saved_cur=P->current, saved_look=P->lookahead;
        Lexer saved_lex=P->lexer;
        adv(P);

        if(check(P,TK_IN)){
            
            adv(P); 
            ASTNode *iterable=parse_expr(P);
            skip_nl(P);
            ASTNode *body=parse_block(P);
            ASTNode *n=node_alloc(NODE_FOREACH,line);
    n->col = P->current.col; n->tok_len = P->current.length;
            strncpy(n->foreach_stmt.elem_name,first_name,MAX_IDENT_LEN-1);
            n->foreach_stmt.has_index=false;
            n->foreach_stmt.iterable=iterable;
            n->foreach_stmt.body=body;
            return n;
        }
        if(check(P,TK_COMMA)){
            adv(P); 
            if(check(P,TK_IDENT)){
                char second_name[MAX_IDENT_LEN];
                int slen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
                memcpy(second_name,P->current.start,slen); second_name[slen]='\0';
                adv(P);
                if(check(P,TK_IN)){
                    adv(P); 
                    ASTNode *iterable=parse_expr(P);
                    skip_nl(P);
                    ASTNode *body=parse_block(P);
                    ASTNode *n=node_alloc(NODE_FOREACH,line);
    n->col = P->current.col; n->tok_len = P->current.length;
                    strncpy(n->foreach_stmt.idx_name,first_name,MAX_IDENT_LEN-1);
                    strncpy(n->foreach_stmt.elem_name,second_name,MAX_IDENT_LEN-1);
                    n->foreach_stmt.has_index=true;
                    n->foreach_stmt.iterable=iterable;
                    n->foreach_stmt.body=body;
                    return n;
                }
            }
        }
        
        P->current=saved_cur; P->lookahead=saved_look; P->lexer=saved_lex;
    }

    
    consume(P,TK_LPAREN,"'('","for (init; cond; post) { }");
    if(P->panic_mode) return NULL;
    ASTNode *init=NULL;
    if(!check(P,TK_SEMICOLON)){
        if(check(P,TK_VAR)){ adv(P); init=parse_var_decl(P,false); }
        else if(check(P,TK_LET)){ adv(P); init=parse_var_decl(P,true); }
        else {
            ASTNode *e=parse_expr(P);
            ASTNode *es=node_alloc(NODE_EXPR_STMT,e?e->line:line);
            es->expr_stmt.expr=e; init=es;
            if(check(P,TK_SEMICOLON)) adv(P);
        }
        if(check(P,TK_SEMICOLON)) adv(P);
        skip_nl(P);
    } else { adv(P); }
    ASTNode *cond=(!check(P,TK_SEMICOLON))?parse_expr(P):NULL;
    consume(P,TK_SEMICOLON,"';'","separate for clauses");
    skip_nl(P);
    ASTNode *post=(!check(P,TK_RPAREN))?parse_expr(P):NULL;
    consume(P,TK_RPAREN,"')'","close for with ')'");
    skip_nl(P);
    ASTNode *body=parse_block(P);
    ASTNode *n=node_alloc(NODE_FOR,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    n->for_stmt.init=init; n->for_stmt.condition=cond;
    n->for_stmt.post=post; n->for_stmt.body=body;
    return n;
}

static ASTNode *parse_switch(Parser *P){
    int line=P->current.line;
    bool has_paren_sw=mat(P,TK_LPAREN);
    if(P->panic_mode) return NULL;
    ASTNode *subject=parse_expr(P);
    if(has_paren_sw) consume(P,TK_RPAREN,"')'","close switch expr");
    skip_nl(P);
    consume(P,TK_LBRACE,"'{'","switch body");
    if(P->panic_mode) return NULL;
    SwitchCase *cases=NULL; int cap=0,count=0;
    ASTNode *default_body=NULL;
    skip_nl(P);
    while(!check(P,TK_RBRACE)&&!check(P,TK_EOF)){
        if(check(P,TK_CASE)){
            adv(P);
            ASTNode *val=parse_expr(P);
            skip_nl(P); if(check(P,TK_COLON)) adv(P); skip_nl(P);
            NL nl={0};
            while(!check(P,TK_RBRACE)&&!check(P,TK_EOF)&&!check(P,TK_CASE)&&!check(P,TK_DEFAULT)){
                if(P->panic_mode){ while(!check(P,TK_EOF)&&!check(P,TK_NEWLINE)&&!check(P,TK_RBRACE)) adv(P); P->panic_mode=false; skip_nl(P); continue; }
                ASTNode *s=parse_stmt(P); if(s) nl_push(&nl,s); skip_nl(P);
            }
            ASTNode *body=nl_to_block(&nl,line);
            if(count>=cap){ cap=cap?cap*2:4; cases=(SwitchCase*)realloc(cases,(size_t)cap*sizeof(SwitchCase)); }
            cases[count].value=val; cases[count].body=body; count++;
        } else if(check(P,TK_DEFAULT)){
            adv(P); if(check(P,TK_COLON)) adv(P); skip_nl(P);
            NL nl={0};
            while(!check(P,TK_RBRACE)&&!check(P,TK_EOF)&&!check(P,TK_CASE)&&!check(P,TK_DEFAULT)){
                ASTNode *s=parse_stmt(P); if(s) nl_push(&nl,s); skip_nl(P);
            }
            default_body=nl_to_block(&nl,line);
        } else {
            error_parse(P->current.line, P->current.col, P->current.length,"'case' or 'default'",NULL,"expected case or default");
            P->panic_mode=true; break;
        }
        skip_nl(P);
    }
    consume(P,TK_RBRACE,"'}'","close switch");
    ASTNode *n=node_alloc(NODE_SWITCH,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    n->switch_stmt.subject=subject; n->switch_stmt.cases=cases;
    n->switch_stmt.case_count=count; n->switch_stmt.default_body=default_body;
    return n;
}

static ASTNode *parse_try_catch(Parser *P){
    int line=P->current.line;
    ASTNode *try_body=parse_block(P);
    skip_nl(P);
    consume(P,TK_CATCH,"'catch'","try { } catch err { }");
    if(P->panic_mode) return NULL;
    
    bool paren=mat(P,TK_LPAREN);
    if(!check(P,TK_IDENT)){ error_parse(P->current.line, P->current.col, P->current.length,"error variable name",NULL,"expected name"); P->panic_mode=true; return NULL; }
    char err_name[MAX_IDENT_LEN];
    int nlen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
    memcpy(err_name,P->current.start,nlen); err_name[nlen]='\0'; adv(P);
    if(paren) consume(P,TK_RPAREN,"')'","close catch params");
    skip_nl(P);
    ASTNode *catch_body=parse_block(P);
    ASTNode *n=node_alloc(NODE_TRY_CATCH,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    n->try_catch.try_body=try_body;
    strncpy(n->try_catch.err_name,err_name,MAX_IDENT_LEN-1);
    n->try_catch.catch_body=catch_body;
    return n;
}

static ASTNode *parse_block(Parser *P){
    int line=P->current.line;
    consume(P,TK_LBRACE,"'{'","start block with '{'");
    if(P->panic_mode) return NULL;
    NL nl={0}; skip_nl(P);
    while(!check(P,TK_RBRACE)&&!check(P,TK_EOF)){
        if(P->panic_mode){ while(!check(P,TK_EOF)&&!check(P,TK_NEWLINE)&&!check(P,TK_RBRACE)) adv(P); P->panic_mode=false; skip_nl(P); continue; }
        ASTNode *s=parse_stmt(P); if(s) nl_push(&nl,s); skip_nl(P);
    }
    consume(P,TK_RBRACE,"'}'","close block");
    ASTNode *n=node_alloc(NODE_BLOCK,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    n->block.stmts=nl.data; n->block.count=nl.len; return n;
}

static ASTNode *parse_expr(Parser *P){ return parse_assignment(P); }

static bool is_compound_assign(TokenKind k){
    return k==TK_PLUS_ASSIGN||k==TK_MINUS_ASSIGN||k==TK_STAR_ASSIGN||
           k==TK_SLASH_ASSIGN||k==TK_PERCENT_ASSIGN||k==TK_STARSTAR_ASSIGN; /* v3.6 */
}

static ASTNode *parse_assignment(Parser *P){
    
    if(check(P,TK_IDENT)&&is_compound_assign(P->lookahead.kind)){
        char name[MAX_IDENT_LEN];
        int nlen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
        memcpy(name,P->current.start,nlen); name[nlen]='\0';
        int line=P->current.line;
        TokenKind op=P->lookahead.kind;
        adv(P); adv(P);
        ASTNode *val=parse_expr(P);
        ASTNode *n=node_alloc(NODE_COMPOUND_ASSIGN,line);
    n->col = P->current.col; n->tok_len = P->current.length;
        strncpy(n->compound_assign.name,name,MAX_IDENT_LEN-1);
        n->compound_assign.op=op;
        n->compound_assign.value=val; return n;
    }
    
    if(check(P,TK_IDENT)&&check2(P,TK_ASSIGN)){
        char name[MAX_IDENT_LEN];
        int nlen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
        memcpy(name,P->current.start,nlen); name[nlen]='\0';
        int line=P->current.line; adv(P); adv(P);
        ASTNode *val=parse_expr(P);
        ASTNode *n=node_alloc(NODE_ASSIGN,line);
    n->col = P->current.col; n->tok_len = P->current.length;
        strncpy(n->assign.name,name,MAX_IDENT_LEN-1);
        n->assign.value=val; return n;
    }
    ASTNode *lhs=parse_null_coal(P);
    
    if(lhs&&lhs->kind==NODE_INDEX&&check(P,TK_ASSIGN)){
        int line=P->current.line; adv(P);
        ASTNode *val=parse_expr(P);
        ASTNode *n=node_alloc(NODE_INDEX_SET,line);
    n->col = P->current.col; n->tok_len = P->current.length;
        n->index_set.object_expr=lhs->index.object_expr;
        n->index_set.index=lhs->index.index;
        n->index_set.value=val;
        lhs->index.object_expr=NULL; lhs->index.index=NULL; ast_free(lhs);
        return n;
    }
    /* expr.field = val -> NODE_FIELD_SET */
    if(lhs&&lhs->kind==NODE_FIELD_ACCESS&&check(P,TK_ASSIGN)){
        int line=P->current.line; adv(P);
        ASTNode *val=parse_expr(P);
        ASTNode *n=node_alloc(NODE_FIELD_SET,line);
    n->col = P->current.col; n->tok_len = P->current.length;
        n->field_set.object_expr=lhs->field_access.object_expr;
        strncpy(n->field_set.field,lhs->field_access.field,MAX_IDENT_LEN-1);
        n->field_set.value=val;
        lhs->field_access.object_expr=NULL; ast_free(lhs);
        return n;
    }
    
    if(lhs&&check(P,TK_QUESTION)){
        int line=P->current.line; adv(P);
        ASTNode *then_val=parse_expr(P);
        consume(P,TK_COLON,"':'","ternary expr ? a : b");
        ASTNode *else_val=parse_expr(P);
        ASTNode *n=node_alloc(NODE_TERNARY,line);
    n->col = P->current.col; n->tok_len = P->current.length;
        n->ternary.cond=lhs; n->ternary.then_val=then_val; n->ternary.else_val=else_val;
        return n;
    }
    return lhs;
}

static ASTNode *parse_null_coal(Parser *P){
    ASTNode *l=parse_or(P);
    while(check(P,TK_NULL_COAL)){
        int line=P->current.line; adv(P);
        ASTNode *r=parse_or(P);
        ASTNode *n=node_alloc(NODE_NULL_COAL,line);
    n->col = P->current.col; n->tok_len = P->current.length;
        n->null_coal.left=l; n->null_coal.right=r; l=n;
    }
    return l;
}

static ASTNode *parse_or(Parser *P){
    ASTNode *l=parse_and(P);
    while(check(P,TK_OR)){ int op=P->current.kind,line=P->current.line; adv(P); skip_nl_continuation(P);
        ASTNode *n=node_alloc(NODE_BINARY,line);
    n->col = P->current.col; n->tok_len = P->current.length; n->binary.op=op; n->binary.left=l; n->binary.right=parse_and(P); l=n; }
    return l;
}
static ASTNode *parse_and(Parser *P){
    ASTNode *l=parse_equality(P);
    while(check(P,TK_AND)){ int op=P->current.kind,line=P->current.line; adv(P); skip_nl_continuation(P);
        ASTNode *n=node_alloc(NODE_BINARY,line);
    n->col = P->current.col; n->tok_len = P->current.length; n->binary.op=op; n->binary.left=l; n->binary.right=parse_equality(P); l=n; }
    return l;
}
static ASTNode *parse_equality(Parser *P){
    ASTNode *l=parse_comparison(P);
    while(check(P,TK_EQ)||check(P,TK_NEQ)){ int op=P->current.kind,line=P->current.line; adv(P); skip_nl_continuation(P);
        ASTNode *n=node_alloc(NODE_BINARY,line);
    n->col = P->current.col; n->tok_len = P->current.length; n->binary.op=op; n->binary.left=l; n->binary.right=parse_comparison(P); l=n; }
    return l;
}
static ASTNode *parse_comparison(Parser *P){
    ASTNode *l=parse_bitwise_or(P);
    /* also handle 'in' and 'not in' as comparison-level operators */
    while(check(P,TK_LT)||check(P,TK_GT)||check(P,TK_LE)||check(P,TK_GE)||check(P,TK_IN)){
        int line=P->current.line;
        if(check(P,TK_IN)){
            adv(P); skip_nl_continuation(P);
            ASTNode *r=parse_bitwise_or(P);
            ASTNode *n=node_alloc(NODE_BINARY,line);
    n->col = P->current.col; n->tok_len = P->current.length;
            n->binary.op=TK_IN; n->binary.left=l; n->binary.right=r; l=n;
        } else {
            int op=P->current.kind; adv(P); skip_nl_continuation(P);
            ASTNode *n=node_alloc(NODE_BINARY,line);
    n->col = P->current.col; n->tok_len = P->current.length; n->binary.op=op; n->binary.left=l; n->binary.right=parse_bitwise_or(P); l=n;
        }
    }
    return l;
}
static ASTNode *parse_bitwise_or(Parser *P){
    ASTNode *l=parse_bitwise_xor(P);
    while(check(P,TK_PIPE)){ int op=P->current.kind,line=P->current.line; adv(P); skip_nl_continuation(P);
        ASTNode *n=node_alloc(NODE_BINARY,line);
    n->col = P->current.col; n->tok_len = P->current.length; n->binary.op=op; n->binary.left=l; n->binary.right=parse_bitwise_xor(P); l=n; }
    return l;
}
static ASTNode *parse_bitwise_xor(Parser *P){
    ASTNode *l=parse_bitwise_and(P);
    while(check(P,TK_CARET)){ int op=P->current.kind,line=P->current.line; adv(P); skip_nl_continuation(P);
        ASTNode *n=node_alloc(NODE_BINARY,line);
    n->col = P->current.col; n->tok_len = P->current.length; n->binary.op=op; n->binary.left=l; n->binary.right=parse_bitwise_and(P); l=n; }
    return l;
}
static ASTNode *parse_bitwise_and(Parser *P){
    ASTNode *l=parse_shift(P);
    while(check(P,TK_AMP)){ int op=P->current.kind,line=P->current.line; adv(P); skip_nl_continuation(P);
        ASTNode *n=node_alloc(NODE_BINARY,line);
    n->col = P->current.col; n->tok_len = P->current.length; n->binary.op=op; n->binary.left=l; n->binary.right=parse_shift(P); l=n; }
    return l;
}
static ASTNode *parse_shift(Parser *P){
    ASTNode *l=parse_additive(P);
    while(check(P,TK_LSHIFT)||check(P,TK_RSHIFT)){ int op=P->current.kind,line=P->current.line; adv(P); skip_nl_continuation(P);
        ASTNode *n=node_alloc(NODE_BINARY,line);
    n->col = P->current.col; n->tok_len = P->current.length; n->binary.op=op; n->binary.left=l; n->binary.right=parse_additive(P); l=n; }
    return l;
}
static ASTNode *parse_additive(Parser *P){
    ASTNode *l=parse_multiply(P);
    while(check(P,TK_PLUS)||check(P,TK_MINUS)){ int op=P->current.kind,line=P->current.line; adv(P); skip_nl_continuation(P);
        ASTNode *n=node_alloc(NODE_BINARY,line);
    n->col = P->current.col; n->tok_len = P->current.length; n->binary.op=op; n->binary.left=l; n->binary.right=parse_multiply(P); l=n; }
    return l;
}
static ASTNode *parse_multiply(Parser *P){
    ASTNode *l=parse_power(P);
    while(check(P,TK_STAR)||check(P,TK_SLASH)||check(P,TK_PERCENT)){ int op=P->current.kind,line=P->current.line; adv(P); skip_nl_continuation(P);
        ASTNode *n=node_alloc(NODE_BINARY,line);
    n->col = P->current.col; n->tok_len = P->current.length; n->binary.op=op; n->binary.left=l; n->binary.right=parse_power(P); l=n; }
    return l;
}
static ASTNode *parse_power(Parser *P){
    ASTNode *l=parse_unary(P);
    if(check(P,TK_STARSTAR)){ int op=P->current.kind,line=P->current.line; adv(P); skip_nl_continuation(P);
        ASTNode *n=node_alloc(NODE_BINARY,line);
    n->col = P->current.col; n->tok_len = P->current.length; n->binary.op=op; n->binary.left=l; n->binary.right=parse_power(P); return n; }
    return l;
}
static ASTNode *parse_unary(Parser *P){
    /* prefix ++ only (-- conflicts with CHN comment syntax -- use -= 1) */
    if(check(P,TK_PLUSPLUS)){
        int line=P->current.line; adv(P);
        if(!check(P,TK_IDENT)){
            error_parse(line, P->current.col, P->current.length,"variable name","++x","expected identifier after '++'");
            P->panic_mode=true; return NULL;
        }
        ASTNode *n=node_alloc(NODE_PRE_INC,line);
    n->col = P->current.col; n->tok_len = P->current.length;
        int nl=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
        memcpy(n->ident.name,P->current.start,(size_t)nl); n->ident.name[nl]='\0'; adv(P); return n;
    }
    if(check(P,TK_MINUS)||check(P,TK_BANG)||check(P,TK_TILDE)){
        int op=P->current.kind,line=P->current.line; adv(P);
        ASTNode *n=node_alloc(NODE_UNARY,line);
    n->col = P->current.col; n->tok_len = P->current.length; n->unary.op=op; n->unary.operand=parse_unary(P); return n;
    }
    if(check(P,TK_TYPEOF)){
        int line=P->current.line; adv(P);
        ASTNode *n=node_alloc(NODE_TYPEOF,line);
    n->col = P->current.col; n->tok_len = P->current.length;
        n->typeof_expr.operand=parse_unary(P); return n;
    }
    return parse_postfix(P);
}

static ASTNode *parse_postfix(Parser *P){
    ASTNode *base=parse_primary(P);
    if(!base) return base;
    for(;;){
        if(check(P,TK_DOT)){
            int line=P->current.line; adv(P);
            if(!check(P,TK_IDENT)){ error_parse(P->current.line, P->current.col, P->current.length,"field or method name","obj.field","expected identifier"); P->panic_mode=true; return base; }
            char fname[MAX_IDENT_LEN];
            int fl=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
            memcpy(fname,P->current.start,fl); fname[fl]='\0'; adv(P);
            /* if followed by '(' it's a method call, otherwise a field access */
            if(check(P,TK_LPAREN)){
                adv(P); /* consume '(' */
                NL args={0}; skip_nl(P);
                if(!check(P,TK_RPAREN)){
                    do{ skip_nl(P); ASTNode *a=parse_expr(P); if(a) nl_push(&args,a); } while(mat(P,TK_COMMA));
                }
                skip_nl(P); consume(P,TK_RPAREN,"')'","close method args");
                ASTNode *n=node_alloc(NODE_METHOD_CALL,line);
    n->col = P->current.col; n->tok_len = P->current.length;
                n->method_call.object_expr=base;
                strncpy(n->method_call.method,fname,MAX_IDENT_LEN-1);
                n->method_call.args=args.data; n->method_call.arg_count=args.len;
                base=n;
            } else {
                /* field access: expr.field */
                ASTNode *n=node_alloc(NODE_FIELD_ACCESS,line);
    n->col = P->current.col; n->tok_len = P->current.length;
                n->field_access.object_expr=base;
                strncpy(n->field_access.field,fname,MAX_IDENT_LEN-1);
                base=n;
            }
        } else if(check(P,TK_LBRACKET)){
            int line=P->current.line; adv(P);
            ASTNode *idx=parse_expr(P);
            consume(P,TK_RBRACKET,"']'","close index with ']'");
            ASTNode *n=node_alloc(NODE_INDEX,line);
    n->col = P->current.col; n->tok_len = P->current.length;
            n->index.object_expr=base; n->index.index=idx;
            base=n;
        /* postfix x++ on identifiers (x-- removed: -- is CHN comment) */
        } else if(check(P,TK_PLUSPLUS)&&base&&base->kind==NODE_IDENT){
            int line=P->current.line;
            adv(P);
            ASTNode *n=node_alloc(NODE_POST_INC,line);
    n->col = P->current.col; n->tok_len = P->current.length;
            strncpy(n->ident.name,base->ident.name,MAX_IDENT_LEN-1);
            ast_free(base); base=n;
        /* expr(args) -- call a lambda or function value */
        } else if(check(P,TK_LPAREN) && base &&
                  (base->kind==NODE_LAMBDA || base->kind==NODE_IDENT ||
                   base->kind==NODE_FIELD_ACCESS || base->kind==NODE_INDEX)){
            int line=P->current.line; adv(P);
            NL args={0}; skip_nl(P);
            if(!check(P,TK_RPAREN)){
                do{ skip_nl(P); ASTNode *a=parse_expr(P); if(a) nl_push(&args,a); skip_nl(P); } while(mat(P,TK_COMMA));
            }
            skip_nl(P); consume(P,TK_RPAREN,"')'","close call args");
            ASTNode *n=node_alloc(NODE_CALL_EXPR,line);
    n->col = P->current.col; n->tok_len = P->current.length;
            n->call_expr.callee    = base;
            n->call_expr.args      = args.data;
            n->call_expr.arg_count = args.len;
            base=n;
        } else break;
    }
    return base;
}

/* --  parse_lambda ---------------------------------------------------
 * Called when we already know we have a lambda.  The caller has consumed
 * or peeked at the opening token.
 *
 * Handles three forms:
 *   (a)  func(p1,p2) { body }    -- anonymous block lambda
 *   (b)  (p1, p2) -> expr        -- arrow lambda, multi-param (parens consumed by caller)
 *   (c)  p -> expr               -- arrow lambda, single param  (ident consumed by caller)
 *
 * For (a) the caller consumed 'func'; for (b)/(c) the caller consumed the
 * param list; in all cases we receive the param list + body to parse.
 *
 * Internal helper: parse_lambda_with_params(P, params, pc, line)
 * parses the body ('->' expr  OR  '{' block '}') and returns a NODE_LAMBDA.
 */
static ASTNode *parse_lambda_with_params(Parser *P,
                                         char params[][MAX_IDENT_LEN],
                                         int pc, int line){
    ASTNode *n = node_alloc(NODE_LAMBDA, line);
    for(int i=0;i<pc;i++)
        strncpy(n->lambda.params[i], params[i], MAX_IDENT_LEN-1);
    n->lambda.param_count = pc;

    if(mat(P, TK_ARROW)){
        /* Arrow form: body is a single expression (implicit return) */
        skip_nl_continuation(P);
        n->lambda.body     = parse_expr(P);
        n->lambda.is_expr_body = true;
    } else if(check(P, TK_LBRACE)){
        /* Block form: { statements } */
        n->lambda.body     = parse_block(P);
        n->lambda.is_expr_body = false;
    } else {
        error_parse(P->current.line, P->current.col, P->current.length, "'->' or '{'",
                    "func(p) -> expr   or   func(p) { return expr }",
                    "expected lambda body, got %s",
                    token_kind_name(P->current.kind));
        P->panic_mode = true;
        ast_free(n);
        return NULL;
    }
    return n;
}

/* Entry point: call after consuming 'func' keyword (no name follows) */
static ASTNode *parse_lambda(Parser *P){
    int line = P->current.line;
    consume(P, TK_LPAREN, "'('", "lambda: func(params) -> expr");
    if(P->panic_mode) return NULL;

    char params[MAX_PARAMS][MAX_IDENT_LEN];
    int pc = 0;
    skip_nl(P);
    if(!check(P, TK_RPAREN)){
        do {
            skip_nl(P);
            if(pc >= MAX_PARAMS){
                error_parse(P->current.line, P->current.col, P->current.length, NULL, NULL, "too many lambda params");
                P->panic_mode = true; return NULL;
            }
            if(!check(P, TK_IDENT)){
                error_parse(P->current.line, P->current.col, P->current.length, "param name", NULL,
                            "expected parameter name, got %s",
                            token_kind_name(P->current.kind));
                P->panic_mode = true; return NULL;
            }
            int pl = P->current.length < MAX_IDENT_LEN-1
                   ? P->current.length : MAX_IDENT_LEN-1;
            memcpy(params[pc], P->current.start, (size_t)pl);
            params[pc][pl] = '\0';
            pc++;
            adv(P);
        } while(mat(P, TK_COMMA));
    }
    skip_nl(P);
    consume(P, TK_RPAREN, "')'", "close lambda params");
    if(P->panic_mode) return NULL;
    skip_nl_continuation(P);
    return parse_lambda_with_params(P, params, pc, line);
}

static ASTNode *parse_primary(Parser *P){
    int line=P->current.line;
    
    if(check(P,TK_NATIVE_CALL)){ adv(P); return parse_native_call(P); }
    if(check(P,TK_NUMBER)){
        ASTNode *n=node_alloc(NODE_NUMBER,line);
    n->col = P->current.col; n->tok_len = P->current.length;
        n->num.value=P->current.value.number; adv(P); return n;
    }
    if(check(P,TK_STRING)){
        ASTNode *n=node_alloc(NODE_STRING,line);
    n->col = P->current.col; n->tok_len = P->current.length;
        n->str.value=strdup(P->current.value.string); adv(P); return n;
    }
    if(check(P,TK_FSTRING)){
        ASTNode *n=node_alloc(NODE_FSTRING,line);
    n->col = P->current.col; n->tok_len = P->current.length;
        n->fstring.raw_fmt=strdup(P->current.value.string); adv(P); return n;
    }
    if(check(P,TK_TRUE)){ adv(P); ASTNode *n=node_alloc(NODE_BOOL,line);
    n->col = P->current.col; n->tok_len = P->current.length; n->boolean.value=true; return n; }
    if(check(P,TK_FALSE)){ adv(P); ASTNode *n=node_alloc(NODE_BOOL,line);
    n->col = P->current.col; n->tok_len = P->current.length; n->boolean.value=false; return n; }
    if(check(P,TK_NIL)){ adv(P); return node_alloc(NODE_NIL,line); }
    /* null keyword - same as nil but signals intent (empty/error) */
    if(check(P,TK_NULL)){ adv(P); return node_alloc(NODE_NULL,line); }
    /* await expr */
    if(check(P,TK_AWAIT)){
        adv(P);
        ASTNode *n=node_alloc(NODE_AWAIT,line);
    n->col = P->current.col; n->tok_len = P->current.length;
        n->await_expr.expr=parse_unary(P);
        return n;
    }

    
    if(check(P,TK_LBRACKET)){
        adv(P); NL elems={0}; skip_nl(P);
        if(!check(P,TK_RBRACKET)){
            do{ skip_nl(P); ASTNode *e=parse_expr(P); if(e) nl_push(&elems,e); skip_nl(P); } while(mat(P,TK_COMMA));
        }
        consume(P,TK_RBRACKET,"']'","close array");
        ASTNode *al=node_alloc(NODE_ARRAY_LITERAL,line);
        al->arr_lit.elements=elems.data; al->arr_lit.count=elems.len; return al;
    }

    
    if(check(P,TK_LBRACE)){
        adv(P); skip_nl(P);
        ASTNode **keys=NULL,**vals=NULL; int dcap=0,dcount=0;
        if(!check(P,TK_RBRACE)){
            do {
                skip_nl(P);
                
                ASTNode *key=NULL;
                if(check(P,TK_STRING)){ key=node_alloc(NODE_STRING,P->current.line); key->str.value=strdup(P->current.value.string); adv(P); }
                else if(check(P,TK_IDENT)){
                    key=node_alloc(NODE_STRING,P->current.line);
                    char tmp[MAX_IDENT_LEN];
                    int tl=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
                    memcpy(tmp,P->current.start,tl); tmp[tl]='\0';
                    key->str.value=strdup(tmp); adv(P);
                } else break;
                consume(P,TK_COLON,"':'","dict literal key: value");
                ASTNode *val=parse_expr(P);
                if(dcount>=dcap){ dcap=dcap?dcap*2:4;
                    keys=(ASTNode**)realloc(keys,(size_t)dcap*sizeof(ASTNode*));
                    vals=(ASTNode**)realloc(vals,(size_t)dcap*sizeof(ASTNode*)); }
                keys[dcount]=key; vals[dcount]=val; dcount++;
                skip_nl(P);
            } while(mat(P,TK_COMMA));
        }
        consume(P,TK_RBRACE,"'}'","close dict literal");
        ASTNode *dl=node_alloc(NODE_DICT_LITERAL,line);
        dl->dict_lit.keys=keys; dl->dict_lit.values=vals; dl->dict_lit.count=dcount;
        return dl;
    }

    if(check(P,TK_IDENT)){
        char name[MAX_IDENT_LEN];
        int nlen=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
        memcpy(name,P->current.start,nlen); name[nlen]='\0'; adv(P);
        if(check(P,TK_LPAREN)){
            adv(P); NL args={0}; skip_nl(P);
            if(!check(P,TK_RPAREN)){
                do{ skip_nl(P); ASTNode *a=parse_expr(P); if(a) nl_push(&args,a); skip_nl(P); } while(mat(P,TK_COMMA));
            }
            skip_nl(P); consume(P,TK_RPAREN,"')'","close args");
            uint16_t builtin_id=0; bool is_builtin=false;
            if(!strcmp(name,"range")){ builtin_id=NATIVE_RANGE; is_builtin=true; }
            else if(!strcmp(name,"str")){  builtin_id=NATIVE_STR;   is_builtin=true; }
            else if(!strcmp(name,"len")){  builtin_id=NATIVE_LEN;   is_builtin=true; }
            else if(!strcmp(name,"args")){ builtin_id=NATIVE_OS_ARGS; is_builtin=true; }
            if(is_builtin){
                ASTNode *n=node_alloc(NODE_NATIVE_CALL,line);
    n->col = P->current.col; n->tok_len = P->current.length;
                n->native_call.call_id=builtin_id;
                n->native_call.args=args.data;
                n->native_call.argc=args.len;
                return n;
            }
            ASTNode *n=node_alloc(NODE_CALL,line);
    n->col = P->current.col; n->tok_len = P->current.length;
            strncpy(n->call.name,name,MAX_IDENT_LEN-1);
            n->call.args=args.data; n->call.arg_count=args.len; return n;
        }
        /* single-param arrow lambda  x -> expr
         * Detected here after we consumed the ident and see '->' next.     */
        if(check(P,TK_ARROW)){
            adv(P); /* consume '->' */
            skip_nl_continuation(P);
            char params[1][MAX_IDENT_LEN];
            strncpy(params[0], name, MAX_IDENT_LEN-1);
            ASTNode *body = parse_expr(P);
            ASTNode *n = node_alloc(NODE_LAMBDA, line);
            strncpy(n->lambda.params[0], name, MAX_IDENT_LEN-1);
            n->lambda.param_count  = 1;
            n->lambda.body         = body;
            n->lambda.is_expr_body = true;
            return n;
        }
        ASTNode *n=node_alloc(NODE_IDENT,line);
    n->col = P->current.col; n->tok_len = P->current.length;
        strncpy(n->ident.name,name,MAX_IDENT_LEN-1); return n;
    }
    /* anonymous block lambda  func(params) { body }
     * Detected when TK_FUNC is used in expression position (no name follows). */
    if(check(P,TK_FUNC)){
        adv(P); /* consume 'func' */
        /* If the next token is an identifier, this is NOT a lambda - it's a
         * misplaced named function declaration; fall through to error.        */
        if(check(P,TK_IDENT)){
            error_parse(line, P->current.col, P->current.length,"expression","use func name(){} at statement level",
                        "named function declaration not valid in expression context");
            P->panic_mode=true; return NULL;
        }
        return parse_lambda(P);
    }
    /* multi-param arrow lambda  (p1, p2) -> expr
     * A plain paren expression is (expr); if after ')' we see '->' it's a
     * lambda parameter list.  We need lookahead: scan the paren content to
     * check if it's all identifiers separated by commas.                    */
    if(check(P,TK_LPAREN)){
        /* Save lexer state to allow backtracking */
        Lexer saved_lex = P->lexer;
        Token saved_cur = P->current;
        Token saved_la  = P->lookahead;

        adv(P); /* consume '(' */
        /* Try to read a comma-separated ident list followed by ')' '->' */
        char lparams[MAX_PARAMS][MAX_IDENT_LEN];
        int  lpc = 0;
        bool is_lambda_params = true;

        skip_nl(P);
        /* Zero-param lambda: () -> expr */
        if(check(P,TK_RPAREN)){
            adv(P);
            if(check(P,TK_ARROW)){
                adv(P); skip_nl_continuation(P);
                ASTNode *body = parse_expr(P);
                ASTNode *n = node_alloc(NODE_LAMBDA, line);
                n->lambda.param_count  = 0;
                n->lambda.body         = body;
                n->lambda.is_expr_body = true;
                return n;
            }
            /* Not a lambda, but we already consumed '()' - error */
            error_parse(line, P->current.col, P->current.length,"expression",NULL,"unexpected '()'");
            P->panic_mode=true; return NULL;
        }

        while(is_lambda_params && !check(P,TK_RPAREN) && !check(P,TK_EOF)){
            if(!check(P,TK_IDENT)){ is_lambda_params=false; break; }
            int pl=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
            memcpy(lparams[lpc],P->current.start,(size_t)pl);
            lparams[lpc][pl]='\0'; lpc++;
            adv(P);
            skip_nl(P);
            if(check(P,TK_RPAREN)) break;
            if(!mat(P,TK_COMMA)){ is_lambda_params=false; break; }
            skip_nl(P);
        }

        if(is_lambda_params && check(P,TK_RPAREN)){
            adv(P); /* consume ')' */
            if(check(P,TK_ARROW)){
                adv(P); skip_nl_continuation(P);
                ASTNode *body = parse_expr(P);
                ASTNode *n = node_alloc(NODE_LAMBDA, line);
                for(int i=0;i<lpc;i++)
                    strncpy(n->lambda.params[i],lparams[i],MAX_IDENT_LEN-1);
                n->lambda.param_count  = lpc;
                n->lambda.body         = body;
                n->lambda.is_expr_body = true;
                return n;
            }
            /* Not an arrow lambda - restore and re-parse as grouped expr.
             * We can't truly backtrack, so reconstruct:
             * If lpc==1 and it was just one ident, treat as (ident). */
            if(lpc==1){
                ASTNode *n=node_alloc(NODE_IDENT,line);
    n->col = P->current.col; n->tok_len = P->current.length;
                strncpy(n->ident.name,lparams[0],MAX_IDENT_LEN-1);
                return n;
            }
            /* Multi-ident paren list that's not followed by '->':
             * restore and report an error (ambiguous syntax).              */
            P->lexer=saved_lex; P->current=saved_cur; P->lookahead=saved_la;
        } else {
            /* Not an ident-list - restore and parse as grouped expr */
            P->lexer=saved_lex; P->current=saved_cur; P->lookahead=saved_la;
        }
        /* Fall-through: normal parenthesised expression */
        adv(P); ASTNode *inner=parse_expr(P);
        consume(P,TK_RPAREN,"')'","close grouped expression");
        return inner;
    }
    if(!P->panic_mode){
        if(check(P,TK_EOF)) error_parse(line, P->current.col, P->current.length,"expression",NULL,"unexpected end of file");
        else error_parse(line, P->current.col, P->current.length,"expression","check for missing operand","unexpected token %s",token_kind_name(P->current.kind));
        P->panic_mode=true;
    }
    adv(P); return node_alloc(NODE_NIL,line);
}

/*
 * v3.0  parse_entry_main
 * Parses:  [visibility] entry main() { ... }
 * Rules:
 *   - name MUST be exactly "main"
 *   - no parameters allowed
 *   - results in a NODE_FUNC_DECL with is_entry=true
*/
static ASTNode *parse_entry_main(Parser *P, FunctionVisibility vis){
    int line=P->current.line;

    /* The next token must be the identifier "main" - nothing else */
    if(!check(P,TK_IDENT)){
        error_parse(line, P->current.col, P->current.length,"'main'","entry main() { }",
            "expected entry point name after 'entry', got %s",
            token_kind_name(P->current.kind));
        P->panic_mode=true; return NULL;
    }
    if(P->current.length!=4 || memcmp(P->current.start,"main",4)!=0){
        char got[MAX_IDENT_LEN]={0};
        int gl=P->current.length<MAX_IDENT_LEN-1?P->current.length:MAX_IDENT_LEN-1;
        memcpy(got,P->current.start,gl);
        error_parse(line, P->current.col, P->current.length,"'main'","entry main() { }",
            "entry point must be named 'main', not '%s'",got);
        P->panic_mode=true; return NULL;
    }
    adv(P); /* consume 'main' */

    consume(P,TK_LPAREN,"'('","entry main() { }");
    if(P->panic_mode) return NULL;

    /* entry main takes NO parameters */
    skip_nl(P);
    if(!check(P,TK_RPAREN)){
        error_parse(P->current.line, P->current.col, P->current.length,"')'","entry main() { }",
            "entry main takes no parameters");
        P->panic_mode=true; return NULL;
    }
    consume(P,TK_RPAREN,"')'","entry main() { }");
    if(P->panic_mode) return NULL;

    skip_nl(P);
    ASTNode *body=parse_block(P);
    if(!body) return NULL;

    ASTNode *n=node_alloc(NODE_FUNC_DECL,line);
    n->col = P->current.col; n->tok_len = P->current.length;
    strncpy(n->func_decl.name,"main",MAX_IDENT_LEN-1);
    n->func_decl.param_count=0;
    n->func_decl.body=body;
    n->func_decl.visibility=vis;
    n->func_decl.is_entry=true;
    return n;
}


ASTNode *parser_parse_expr(const char *source) {
    Parser P;
    parser_init(&P, source);
    ASTNode *n = parse_expr(&P);
    return P.panic_mode ? NULL : n;
}
