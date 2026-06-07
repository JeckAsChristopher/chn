

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include "compiler.h"
#include "parser.h"
#include "native.h"
#include "lexer.h"
#include "gc.h"
#include <string.h>
#include <stdlib.h>

extern bool (*chn_import_handler)(const char *path, Compiler *C);

static void chunk_grow_code(Chunk *ch){
    int nc = ch->code_cap < 256 ? 256 : ch->code_cap * 2;
    ch->code = (uint8_t*)GC_GROW(ch->code, (size_t)ch->code_cap, (size_t)nc);
    ch->code_cap = nc;
}

static void emit_byte(Compiler *C, uint8_t b, int line){
    Chunk *ch = C->current_chunk;
    if(ch->code_len >= ch->code_cap) chunk_grow_code(ch);
    chunk_add_line(ch, ch->code_len, line);
    ch->code[ch->code_len++] = b;
}
static void emit_op (Compiler *C, OpCode op, int line){ emit_byte(C,(uint8_t)op,line); }
static void emit_op1(Compiler *C, OpCode op, uint16_t v, int line){
    emit_byte(C,(uint8_t)op,line);
    emit_byte(C,(uint8_t)(v&0xFF),line);
    emit_byte(C,(uint8_t)((v>>8)&0xFF),line);
}
static int current_offset(Compiler *C){ return C->current_chunk->code_len; }

static int add_constant(Compiler *C, Value val, int line){
    Chunk *ch = C->current_chunk;
    if(ch->const_count >= ch->const_cap){
        int nc = ch->const_cap < 32 ? 32 : ch->const_cap * 2;
        ch->constants = (Value*)GC_GROW(ch->constants,
            (size_t)ch->const_cap*sizeof(Value), (size_t)nc*sizeof(Value));
        ch->const_cap = nc;
    }
    
    for(int i=0;i<ch->const_count;i++){
        Value v=ch->constants[i];
        if(v.type==val.type){
            if(val.type==VAL_NUMBER&&v.as.number==val.as.number) return i;
            if(val.type==VAL_STRING&&v.as.string==val.as.string) return i;
        }
    }
    ch->constants[ch->const_count] = val;
    return ch->const_count++;
}

static void emit_const(Compiler *C, Value val, int line){
    
    if(C->opt_level>=1 && val.type==VAL_NUMBER){
        if(val.as.number==0.0){ emit_op(C,OP_CONST_0,line); return; }
        if(val.as.number==1.0){ emit_op(C,OP_CONST_1,line); return; }
    }
    emit_op1(C, OP_CONST, (uint16_t)add_constant(C,val,line), line);
}

static int emit_jump(Compiler *C, OpCode op, int line){
    emit_byte(C,(uint8_t)op,line);
    int patch = C->current_chunk->code_len;
    emit_byte(C,0xFF,line); emit_byte(C,0xFF,line);
    return patch;
}
static void patch_jump(Compiler *C, int pos){
    uint16_t t = (uint16_t)C->current_chunk->code_len;
    C->current_chunk->code[pos]   = (uint8_t)(t&0xFF);
    C->current_chunk->code[pos+1] = (uint8_t)((t>>8)&0xFF);
}
static void patch_jump_to(Compiler *C, int patch_pos, int target){
    C->current_chunk->code[patch_pos]   = (uint8_t)((uint16_t)target&0xFF);
    C->current_chunk->code[patch_pos+1] = (uint8_t)(((uint16_t)target>>8)&0xFF);
}
static void emit_loop(Compiler *C, int loop_start, int line){
    emit_op(C,OP_GC_SAFEPOINT,line);
    emit_byte(C,(uint8_t)OP_JUMP,line);
    uint16_t t=(uint16_t)loop_start;
    emit_byte(C,(uint8_t)(t&0xFF),line);
    emit_byte(C,(uint8_t)((t>>8)&0xFF),line);
}

static void push_ctx(Compiler *C, CtxKind kind){
    if(C->ctx_depth>=MAX_CTX_DEPTH){ C->had_error=true; return; }
    CtxFrame *f=&C->ctx_stack[C->ctx_depth++];
    f->kind=kind; f->break_count=0; f->continue_count=0; f->continue_target=-1;
}
static void pop_ctx_patch_breaks(Compiler *C){
    if(C->ctx_depth<=0) return;
    CtxFrame *f=&C->ctx_stack[--C->ctx_depth];
    int end=current_offset(C);
    for(int i=0;i<f->break_count;i++) patch_jump_to(C,f->break_patches[i],end);
}
static void emit_break(Compiler *C, int line){
    if(C->ctx_depth<=0){ error_compile(line, 0, 0,"'break' outside loop or switch"); C->had_error=true; return; }
    CtxFrame *f=&C->ctx_stack[C->ctx_depth-1];
    if(f->break_count>=MAX_BREAKS){ error_compile_range(line, 0, 0,"too many breaks"); C->had_error=true; return; }
    int p=emit_jump(C,OP_JUMP,line);
    f->break_patches[f->break_count++]=p;
}
static void emit_continue(Compiler *C, int line){
    if(C->ctx_depth<=0){ error_compile(line, 0, 0,"'continue' outside a loop"); C->had_error=true; return; }
    for(int i=C->ctx_depth-1;i>=0;i--){
        if(C->ctx_stack[i].kind==CTX_LOOP){
            CtxFrame *f=&C->ctx_stack[i];
            if(f->continue_count>=MAX_BREAKS){ C->had_error=true; return; }
            int p=emit_jump(C,OP_JUMP,line);
            f->continue_patches[f->continue_count++]=p;
            return;
        }
    }
    error_compile(line, 0, 0,"'continue' not inside a loop"); C->had_error=true;
}
static void patch_continues(Compiler *C, int target){
    if(C->ctx_depth<=0) return;
    CtxFrame *f=&C->ctx_stack[C->ctx_depth-1];
    for(int i=0;i<f->continue_count;i++) patch_jump_to(C,f->continue_patches[i],target);
}

static int global_find(Compiler *C, const char *name){
    for(int i=0;i<C->global_count;i++) if(!strcmp(C->globals[i].name,name)) return i;
    return -1;
}
static int global_define(Compiler *C, const char *name, bool is_let, int line){
    int ex=global_find(C,name);
    if(ex>=0){
        if(is_let&&!C->globals[ex].is_let){ error_compile_ref(line, 0, 0,"cannot redeclare 'var %s' as 'let'",name); C->had_error=true; return ex; }
        if(!is_let&&C->globals[ex].is_let){ error_compile_ref(line, 0, 0,"cannot redeclare 'let %s' as 'var'",name); C->had_error=true; return ex; }
        C->globals[ex].defined=false; return ex;
    }
    if(C->global_count>=MAX_VARIABLES){ error_compile_range(line, 0, 0,"too many globals"); C->had_error=true; return 0; }
    int idx=C->global_count++;
    strncpy(C->globals[idx].name,name,MAX_IDENT_LEN-1);
    C->globals[idx].index=idx; C->globals[idx].is_let=is_let; C->globals[idx].defined=false;
    
    Chunk *ch=&C->top_chunk;
    if(ch->var_count>=ch->var_cap){
        int nc=ch->var_cap<64?64:ch->var_cap*2;
        ch->var_names=(char**)GC_GROW(ch->var_names,(size_t)ch->var_cap*sizeof(char*),(size_t)nc*sizeof(char*));
        ch->var_cap=nc;
    }
    ch->var_names[ch->var_count++]=strdup(name);
    return idx;
}
static int local_find(Compiler *C, const char *name){
    for(int i=C->local_count-1;i>=0;i--) if(!strcmp(C->locals[i].name,name)) return i;
    return -1;
}
static int local_define(Compiler *C, const char *name, bool is_let, int line){
    if(C->local_count>=MAX_LOCALS){ error_compile_range(line, 0, 0,"too many locals"); C->had_error=true; return 0; }
    int slot=C->local_count++;
    strncpy(C->locals[slot].name,name,MAX_IDENT_LEN-1);
    C->locals[slot].slot=slot; C->locals[slot].is_let=is_let; C->locals[slot].defined=false;
    
    Chunk *ch=C->current_chunk;
    if(ch->varname_count>=ch->varname_cap){
        int nc=ch->varname_cap<8?8:ch->varname_cap*2;
        ch->co_varnames=(char**)GC_GROW(ch->co_varnames,
            (size_t)ch->varname_cap*sizeof(char*),(size_t)nc*sizeof(char*));
        ch->varname_cap=nc;
    }
    ch->co_varnames[ch->varname_count++]=strdup(name);
    return slot;
}
static void pop_scope(Compiler *C, int saved_lc, int line){
    int n=C->local_count-saved_lc;
    for(int i=0;i<n;i++) emit_op(C,OP_POP,line);
    C->local_count=saved_lc;
}

static void suggest_variable(Compiler *C, const char *name, int line){
    const char *cands[MAX_VARIABLES+MAX_LOCALS+4]; int nc=0;
    if(C->in_function) for(int i=0;i<C->local_count;i++) cands[nc++]=C->locals[i].name;
    for(int i=0;i<C->global_count;i++) cands[nc++]=C->globals[i].name;
    int dist; const char *m=best_match(name,(const char**)cands,nc,&dist);
    if(m) error_compile_ref(line, 0, 0,"undefined variable '%s' -- did you mean '%s'?",name,m);
    else  error_compile_ref(line, 0, 0,"undefined variable '%s'",name);
}
static void suggest_function(Compiler *C, const char *name, int line){
    const char *cands[MAX_FUNCS*2+4]; int nc=0;
    for(int i=0;i<C->func_count;i++)   cands[nc++]=C->functions[i]->name;
    for(int i=0;i<C->import_count;i++) cands[nc++]=C->imports[i]->name;
    int dist; const char *m=best_match(name,(const char**)cands,nc,&dist);
    if(m) error_compile_ref(line, 0, 0,"undefined function '%s' -- did you mean '%s'?",name,m);
    else  error_compile_ref(line, 0, 0,"undefined function '%s'",name);
}

typedef struct { FunctionObject *func; } FuncRef;
static FuncRef resolve_function(Compiler *C, const char *name, int line){
    FuncRef ref={NULL};
    for(int i=0;i<C->func_count;i++){
        if(!strcmp(C->functions[i]->name,name)){
            FunctionObject *callee=C->functions[i];
            if(callee->visibility==VIS_PROTECTED&&!func_can_access(C->current_func,callee)){
                error_compile_access(line, 0, 0,"protected function '%s' not accessible here",name);
                C->had_error=true; return ref;
            }
            ref.func=callee; return ref;
        }
    }
    for(int i=0;i<C->import_count;i++){
        FunctionObject *f=C->imports[i];
        if(strcmp(f->name,name)) continue;
        

        if(!C->is_bundle_pass){
            if(f->visibility==VIS_PRIVATE){ error_compile_access(line, 0, 0,"function '%s' is private",name); C->had_error=true; return ref; }
            if(!f->exported){ error_compile_access(line, 0, 0,"function '%s' is not exported",name); C->had_error=true; return ref; }
        }
        ref.func=f; return ref;
    }
    suggest_function(C,name,line); C->had_error=true;
    return ref;
}

static void emit_load(Compiler *C, const char *name, int line){
    if(C->in_function){
        int slot=local_find(C,name);
        if(slot>=0){ emit_op1(C,OP_GET_LOCAL,(uint16_t)slot,line); return; }
    }
    int idx=global_find(C,name);
    if(idx<0){ suggest_variable(C,name,line); C->had_error=true; return; }
    emit_op1(C,OP_GET_VAR,(uint16_t)C->globals[idx].index,line);
}
static void emit_store(Compiler *C, const char *name, int line, bool allow_def){
    if(C->in_function){
        int slot=local_find(C,name);
        if(slot>=0){
            C->locals[slot].defined=true;
            emit_op1(C,OP_SET_LOCAL,(uint16_t)slot,line);
            

            if(C->current_func && C->current_func->has_captures){
                int gidx=global_find(C,name);
                if(gidx>=0){
                    emit_op1(C,OP_GET_LOCAL,(uint16_t)slot,line);
                    emit_op1(C,OP_SET_VAR,(uint16_t)C->globals[gidx].index,line);
                    emit_op(C,OP_POP,line);  
                }
            }
            return;
        }
    }
    int idx=global_find(C,name);
    if(idx<0){
        if(allow_def){ idx=global_define(C,name,false,line); }
        else { suggest_variable(C,name,line); C->had_error=true; return; }
    }
    C->globals[idx].defined=true;
    emit_op1(C,OP_SET_VAR,(uint16_t)C->globals[idx].index,line);
}

static void compile_expr(Compiler *C, ASTNode *node);  

static bool has_nested_func(ASTNode *n){
    if(!n) return false;
    if(n->kind==NODE_FUNC_DECL||n->kind==NODE_LAMBDA) return true;
    
    switch(n->kind){
        case NODE_BLOCK:
        case NODE_PROGRAM:
            for(int i=0;i<n->program.count;i++)
                if(has_nested_func(n->program.stmts[i])) return true;
            break;
        case NODE_IF:
            if(has_nested_func(n->if_stmt.condition)) return true;
            if(has_nested_func(n->if_stmt.then_branch)) return true;
            if(has_nested_func(n->if_stmt.else_branch)) return true;
            break;
        case NODE_WHILE:
            if(has_nested_func(n->while_stmt.condition)) return true;
            if(has_nested_func(n->while_stmt.body)) return true;
            break;
        case NODE_FOREACH:
            if(has_nested_func(n->foreach_stmt.iterable)) return true;
            if(has_nested_func(n->foreach_stmt.body)) return true;
            break;
        case NODE_RETURN:
            if(has_nested_func(n->ret.value)) return true;
            break;
        case NODE_VAR_DECL:
            if(has_nested_func(n->var_decl.initializer)) return true;
            break;
        case NODE_ASSIGN:
            if(has_nested_func(n->assign.value)) return true;
            break;
        case NODE_CALL:
            for(int i=0;i<n->call.arg_count;i++)
                if(has_nested_func(n->call.args[i])) return true;
            break;
        default: break;
    }
    return false;
}

static void compile_fstring(Compiler *C, const char *raw, int line){
    const char *p = raw;
    int parts = 0;
    char seg[MAX_STRING_LEN];
    int slen = 0;

    while(*p){
        if(*p=='{'&&*(p+1)!='{'){
            if(slen>0){
                ObjString *s=gc_string(seg,slen);
                emit_const(C,STRING_VAL(s),line);
                if(parts>0) emit_op(C,OP_ADD,line);
                parts++;
            }
            slen=0;
            p++;
            char expr[MAX_STRING_LEN]; int elen=0;
            int depth=0;
            while(*p&&(*p!='}' || depth>0)){
                if(*p=='('||*p=='['||*p=='{') depth++;
                if(*p==')'||*p==']') depth--;
                if(depth<0) break;
                expr[elen++]=*p++;
            }
            if(*p=='}') p++;
            expr[elen]='\0';

            
            char *end; double nv=strtod(expr,&end);
            if(*end=='\0'&&end!=expr){
                emit_const(C,NUMBER_VAL(nv),line);
            } else {
                

                const char *saved_file=g_source_file;
                const char *saved_code=g_source_code;
                bool saved_err=g_had_error;

                error_init("<fstring>", expr);
                g_had_error=false;

                ASTNode *fnode=parser_parse_expr(expr);
                bool fok = !g_had_error && fnode;

                error_init(saved_file, saved_code);
                g_had_error=saved_err;

                if(fok && fnode){
                    compile_expr(C, fnode);
                    ast_free(fnode);
                } else {
                    if(fnode) ast_free(fnode);
                    
                    emit_load(C,expr,line);
                }
            }

            if(parts>0) emit_op(C,OP_ADD,line);
            parts++;
        } else if(*p=='{'&&*(p+1)=='{'){
            seg[slen++]='{'; p+=2;
        } else if(*p=='}'&&*(p+1)=='}'){
            seg[slen++]='}'; p+=2;
        } else {
            seg[slen++]=*p++;
        }
        if(slen>=MAX_STRING_LEN-4){ seg[slen]='\0'; break; }
    }

    if(slen>0){
        ObjString *s=gc_string(seg,slen);
        emit_const(C,STRING_VAL(s),line);
        if(parts>0) emit_op(C,OP_ADD,line);
        parts++;
    }
    if(parts==0) emit_const(C,STRING_VAL(gc_cstring("")),line);
}

static void compile_stmt(Compiler *C, ASTNode *node);
static const char *compiletime_typeof(ASTNode *node);

static void compile_expr(Compiler *C, ASTNode *node){
    if(!node||C->had_error) return;
    int line=node->line;
    switch(node->kind){

    case NODE_NUMBER:
        emit_const(C,NUMBER_VAL(node->num.value),line); break;

    case NODE_STRING:{
        ObjString *s=gc_cstring(node->str.value);
        emit_const(C,STRING_VAL(s),line); break;
    }

    case NODE_FSTRING:
        compile_fstring(C,node->fstring.raw_fmt,line); break;

    case NODE_BOOL:
        emit_op(C,node->boolean.value?OP_TRUE:OP_FALSE,line); break;

    case NODE_NIL:
        emit_op(C,OP_NIL,line); break;

    case NODE_ARRAY_LITERAL:{
        emit_op(C,OP_ARRAY_NEW,line);
        for(int i=0;i<node->arr_lit.count;i++){
            compile_expr(C,node->arr_lit.elements[i]);
            emit_op(C,OP_ARRAY_PUSH,line);
        }
        break;
    }

    case NODE_DICT_LITERAL:{
        emit_op(C,OP_DICT_NEW,line);
        for(int i=0;i<node->dict_lit.count;i++){
            

            emit_op(C,OP_DUP,line);        
            compile_expr(C,node->dict_lit.keys[i]);
            compile_expr(C,node->dict_lit.values[i]);
            emit_op(C,OP_DICT_SET,line);   
            emit_op(C,OP_POP,line);        
        }
        break;
    }

    case NODE_IDENT:
        emit_load(C,node->ident.name,line); break;

    case NODE_ASSIGN:{
        if(!node->assign.value){
            error_compile(line,0,0,"missing right-hand side in assignment to '%s'",
                node->assign.name);
            C->had_error=true; break;
        }
        compile_expr(C,node->assign.value);
        if(C->had_error) break;
        emit_store(C,node->assign.name,line,false); break;
    }

    case NODE_COMPOUND_ASSIGN:{
        if(!node->compound_assign.value){
            error_compile(line,0,0,"missing right-hand side in compound assignment to '%s'",
                node->compound_assign.name);
            C->had_error=true; break;
        }
        emit_load(C,node->compound_assign.name,line);
        compile_expr(C,node->compound_assign.value);
        switch(node->compound_assign.op){
            case TK_PLUS_ASSIGN:     emit_op(C,OP_ADD,line); break;
            case TK_MINUS_ASSIGN:    emit_op(C,OP_SUB,line); break;
            case TK_STAR_ASSIGN:     emit_op(C,OP_MUL,line); break;
            case TK_SLASH_ASSIGN:    emit_op(C,OP_DIV,line); break;
            case TK_PERCENT_ASSIGN:  emit_op(C,OP_MOD,line); break;
            case TK_STARSTAR_ASSIGN: emit_op(C,OP_POW,line); break; 
            default: error_compile(line, 0, 0,"unknown compound assign"); C->had_error=true;
        }
        emit_store(C,node->compound_assign.name,line,false);
        break;
    }

    

    case NODE_POST_INC:{
        emit_load(C,node->ident.name,line);   
        emit_load(C,node->ident.name,line);   
        emit_op(C,OP_CONST_1,line);
        emit_op(C,OP_ADD,line);
        emit_store(C,node->ident.name,line,false);
        emit_op(C,OP_POP,line);               
        break;
    }
    
    case NODE_PRE_INC:{
        emit_load(C,node->ident.name,line);
        emit_op(C,OP_CONST_1,line);
        emit_op(C,OP_ADD,line);
        emit_store(C,node->ident.name,line,false);
        break;
    }

    
    case NODE_TERNARY:{
        if(!node->ternary.cond||!node->ternary.then_val||!node->ternary.else_val){
            error_compile(line,0,0,"incomplete ternary expression: requires 'cond ? then : else'");
            C->had_error=true; emit_op(C,OP_NIL,line); break;
        }
        compile_expr(C,node->ternary.cond);
        int jf=emit_jump(C,OP_JUMP_IF_FALSE,line);
        emit_op(C,OP_POP,line);
        compile_expr(C,node->ternary.then_val);
        int je=emit_jump(C,OP_JUMP,line);
        patch_jump(C,jf);
        emit_op(C,OP_POP,line);
        compile_expr(C,node->ternary.else_val);
        patch_jump(C,je);
        break;
    }

    case NODE_NULL_COAL:{
        
        
        
        
        
        if(!node->null_coal.left||!node->null_coal.right){
            error_compile(line,0,0,"incomplete null-coalescing expression: requires 'left ?? right'");
            C->had_error=true; emit_op(C,OP_NIL,line); break;
        }
        compile_expr(C,node->null_coal.left);
        int j_nil=emit_jump(C,OP_JUMP_IF_NIL,line);
        int j_end=emit_jump(C,OP_JUMP,line);
        patch_jump(C,j_nil);
        emit_op(C,OP_POP,line); 
        compile_expr(C,node->null_coal.right);
        patch_jump(C,j_end);
        break;
    }

    case NODE_TYPEOF:{
        
        const char *ct=(C->opt_level>=1)?compiletime_typeof(node->typeof_expr.operand):NULL;
        if(ct){
            ObjString *s=gc_cstring(ct);
            emit_const(C,STRING_VAL(s),line);
        } else {
            compile_expr(C,node->typeof_expr.operand);
            emit_op(C,OP_TYPEOF,line);
        }
        break;
    }

    case NODE_UNARY:
        if(!node->unary.operand){
            error_compile(line,0,0,"unary expression is missing its operand");
            C->had_error=true; emit_op(C,OP_NIL,line); break;
        }
        compile_expr(C,node->unary.operand);
        switch(node->unary.op){
            case TK_MINUS:  emit_op(C,OP_NEG,line); break;
            case TK_BANG:   emit_op(C,OP_NOT,line); break;
            case TK_TILDE:  emit_op(C,OP_BNOT,line); break;
            default: error_compile(line, 0, 0,"unknown unary op"); C->had_error=true;
        } break;

    case NODE_BINARY:{
        

        /* Safety: check for NULL operands FIRST before any field access.
           A malformed parse (e.g. "1 +" with missing RHS) can leave
           binary.left or binary.right as NULL, which would segfault the
           constant-folding check below. */
        if(!node->binary.left || !node->binary.right){
            error_compile(line, 0, 0,
                "binary expression is missing an operand (check for incomplete expression)");
            C->had_error=true; emit_op(C,OP_NIL,line); break;
        }

        if(C->opt_level>=1 &&
           node->binary.left->kind==NODE_NUMBER && node->binary.right->kind==NODE_NUMBER){
            double L=node->binary.left->num.value;
            double R=node->binary.right->num.value;
            double result=0.0; bool folded=true;
            switch(node->binary.op){
                case TK_PLUS:     result=L+R;                         break;
                case TK_MINUS:    result=L-R;                         break;
                case TK_STAR:     result=L*R;                         break;
                case TK_SLASH:    if(R!=0){result=L/R;}else folded=false; break;
                case TK_PERCENT:  if(R!=0){result=fmod(L,R);}else folded=false; break;
                case TK_STARSTAR: result=pow(L,R);                    break;
                default:          folded=false;                        break;
            }
            if(folded){ emit_const(C,NUMBER_VAL(result),line); break; }
        }
        if(node->binary.op==TK_AND){
            compile_expr(C,node->binary.left);
            int j=emit_jump(C,OP_JUMP_IF_FALSE,line);
            emit_op(C,OP_POP,line);
            compile_expr(C,node->binary.right);
            patch_jump(C,j); break;
        }
        if(node->binary.op==TK_OR){
            compile_expr(C,node->binary.left);
            int jt=emit_jump(C,OP_JUMP_IF_TRUE,line);
            emit_op(C,OP_POP,line);
            compile_expr(C,node->binary.right);
            patch_jump(C,jt); break;
        }
        /* NULL check already performed at top of NODE_BINARY case */
        compile_expr(C,node->binary.left);
        compile_expr(C,node->binary.right);
        switch(node->binary.op){
            case TK_PLUS:     emit_op(C,OP_ADD,line); break;
            case TK_MINUS:    emit_op(C,OP_SUB,line); break;
            case TK_STAR:     emit_op(C,OP_MUL,line); break;
            case TK_SLASH:    emit_op(C,OP_DIV,line); break;
            case TK_PERCENT:  emit_op(C,OP_MOD,line); break;
            case TK_STARSTAR: emit_op(C,OP_POW,line); break;
            case TK_AMP:      emit_op(C,OP_BAND,line); break;
            case TK_PIPE:     emit_op(C,OP_BOR,line); break;
            case TK_CARET:    emit_op(C,OP_BXOR,line); break;
            case TK_LSHIFT:   emit_op(C,OP_LSHIFT,line); break;
            case TK_RSHIFT:   emit_op(C,OP_RSHIFT,line); break;
            case TK_EQ:       emit_op(C,OP_EQ,line); break;
            case TK_NEQ:      emit_op(C,OP_NEQ,line); break;
            case TK_LT:       emit_op(C,OP_LT,line); break;
            case TK_GT:       emit_op(C,OP_GT,line); break;
            case TK_LE:       emit_op(C,OP_LE,line); break;
            case TK_GE:       emit_op(C,OP_GE,line); break;
            case TK_IN:       emit_op(C,OP_IN,line); break; 
            default: error_compile(line, 0, 0,"unknown binary op %d",node->binary.op); C->had_error=true;
        } break;
    }

    case NODE_NATIVE_CALL:{
        for(int i=0;i<node->native_call.argc;i++) compile_expr(C,node->native_call.args[i]);
        emit_op(C,OP_NATIVE,line);
        uint16_t nid=node->native_call.call_id;
        emit_byte(C,(uint8_t)(nid&0xFF),line);
        emit_byte(C,(uint8_t)(nid>>8),line);
        emit_byte(C,(uint8_t)node->native_call.argc,line);
        break;
    }

    case NODE_CALL:{
        

        bool is_named_func = false;
        for(int i=0;i<C->func_count;i++)
            if(!strcmp(C->functions[i]->name,node->call.name)){ is_named_func=true; break; }
        for(int i=0;!is_named_func&&i<C->import_count;i++)
            if(!strcmp(C->imports[i]->name,node->call.name)){ is_named_func=true; break; }

        if(is_named_func){
            
            FuncRef ref=resolve_function(C,node->call.name,line);
            if(!ref.func||C->had_error) return;
            if(node->call.arg_count!=ref.func->arity){
                error_compile_type(line, 0, 0,"'%s' expects %d arg(s) but got %d",
                    node->call.name,ref.func->arity,node->call.arg_count);
                C->had_error=true; return;
            }
            emit_const(C,FUNC_VAL(ref.func),line);
            for(int i=0;i<node->call.arg_count;i++) compile_expr(C,node->call.args[i]);
            emit_op1(C,OP_CALL,(uint16_t)node->call.arg_count,line);
        } else {
            
            bool found_var = (C->in_function && local_find(C,node->call.name)>=0)
                           || global_find(C,node->call.name)>=0;
            if(!found_var){
                
                suggest_function(C,node->call.name,line);
                C->had_error=true; return;
            }
            emit_load(C,node->call.name,line);
            for(int i=0;i<node->call.arg_count;i++) compile_expr(C,node->call.args[i]);
            emit_op1(C,OP_CALL,(uint16_t)node->call.arg_count,line);
        }
        break;
    }

    case NODE_METHOD_CALL:{
        /* Guard: object_expr must not be NULL */
        if(!node->method_call.object_expr){
            error_compile(line, 0, 0, "method call has no target object (syntax error)");
            C->had_error=true; emit_op(C,OP_NIL,line); break;
        }
        if(node->method_call.object_expr &&
           node->method_call.object_expr->kind == NODE_IDENT &&
           C->alias_count > 0)
        {
            const char *obj_name = node->method_call.object_expr->ident.name;
            const char *fn_name  = node->method_call.method;
            for(int ai = 0; ai < C->alias_count; ai++){
                if(strcmp(C->aliases[ai].alias, obj_name) != 0) continue;
                
                FunctionObject *found_fn = NULL;
                for(int fi = C->aliases[ai].import_start;
                        fi < C->aliases[ai].import_end && fi < C->import_count; fi++){
                    if(strcmp(C->imports[fi]->name, fn_name)==0){
                        found_fn = C->imports[fi]; break;
                    }
                }
                if(!found_fn){
                    error_compile_ref(line, 0, 0,"'%s' has no function '%s'", obj_name, fn_name);
                    C->had_error=true; return;
                }
                if(node->method_call.arg_count != found_fn->arity){
                    error_compile_type(line, 0, 0,"'%s.%s' expects %d arg(s) but got %d",
                        obj_name, fn_name, found_fn->arity, node->method_call.arg_count);
                    C->had_error=true; return;
                }
                
                emit_const(C, FUNC_VAL(found_fn), line);
                for(int i = 0; i < node->method_call.arg_count; i++)
                    compile_expr(C, node->method_call.args[i]);
                emit_op1(C, OP_CALL, (uint16_t)node->method_call.arg_count, line);
                return;
            }
            
        }

        ArrayMethod mid=method_id(node->method_call.method);
        if(mid==METHOD_UNKNOWN){
            const char *ms[]={
                "add","insert","cut","remove","rall","length","sort","reverse",
                "upper","lower","trim","split","contains","starts_with","ends_with",
                "replace","find","slice","to_num",
                "join","map","filter","reduce","flat","unique","pop","shift",
                "count","first","last","sum","min","max","any","all","copy","fill","index_of",
                "pad_left","pad_right","repeat","char_at",
                "keys","values","has","delete","size","merge","get","to_arr"};
            int dist; const char *m=best_match(node->method_call.method,(const char**)ms,48,&dist);
            if(m) error_compile_ref(line, 0, 0,"unknown method '%s' -- did you mean '%s'?",node->method_call.method,m);
            else  error_compile_ref(line, 0, 0,"unknown method '%s'",node->method_call.method);
            C->had_error=true; return;
        }
        
        if(mid==METHOD_LENGTH){
            compile_expr(C,node->method_call.object_expr);
            emit_op(C,OP_ARRAY_LEN,line); break;
        }
        compile_expr(C,node->method_call.object_expr);
        for(int i=0;i<node->method_call.arg_count;i++) compile_expr(C,node->method_call.args[i]);
        emit_byte(C,(uint8_t)OP_METHOD_CALL,line);
        emit_byte(C,(uint8_t)mid,line);
        emit_byte(C,(uint8_t)node->method_call.arg_count,line);
        break;
    }

    case NODE_INDEX:{
        if(!node->index.object_expr || !node->index.index){
            error_compile(line, 0, 0, "index expression is incomplete (syntax error)");
            C->had_error=true; emit_op(C,OP_NIL,line); break;
        }
        compile_expr(C,node->index.object_expr);
        compile_expr(C,node->index.index);
        
        
        emit_op(C,OP_ARRAY_INDEX,line); break;
    }

    case NODE_INDEX_SET:{
        compile_expr(C,node->index_set.object_expr);
        compile_expr(C,node->index_set.index);
        compile_expr(C,node->index_set.value);
        emit_op(C,OP_ARRAY_SET,line); break;
    }

    case NODE_THROW:{
        if(!node->throw_stmt.value){
            error_compile(line,0,0,"'throw' requires a value");
            C->had_error=true; break;
        }
        compile_expr(C,node->throw_stmt.value);
        if(!C->had_error) emit_op(C,OP_THROW,line);
        break;
    }

    
    case NODE_NULL:
        emit_op(C,OP_NIL,line); break;

    

    case NODE_AWAIT:
        compile_expr(C,node->await_expr.expr);
        emit_op(C,OP_AWAIT,line); break;

    

    case NODE_FIELD_ACCESS:{
        compile_expr(C,node->field_access.object_expr);
        ObjString *key=gc_cstring(node->field_access.field);
        emit_const(C,STRING_VAL(key),line);
        emit_op(C,OP_DICT_GET,line); break;
    }

    

    case NODE_FIELD_SET:{
        compile_expr(C,node->field_set.object_expr);
        ObjString *key=gc_cstring(node->field_set.field);
        emit_const(C,STRING_VAL(key),line);
        compile_expr(C,node->field_set.value);
        emit_op(C,OP_DICT_SET,line); break;
    }

    

    case NODE_LAMBDA:{
        
        char lname[64];
        snprintf(lname,sizeof(lname),"__lambda_%d", ++C->lambda_count);

        
        FunctionObject *f = func_new(lname, VIS_PRIVATE, C->source_file);
        f->arity     = node->lambda.param_count;
        f->params    = (char**)malloc((size_t)f->arity * sizeof(char*));
        f->param_cap = f->arity;
        for(int i=0;i<f->arity;i++)
            f->params[i] = strdup(node->lambda.params[i]);
        if(C->func_count >= MAX_FUNCS){
            error_compile(line,0,0,"internal: function table full (limit %d), cannot define lambda",MAX_FUNCS);
            C->had_error=true; return;
        }
        C->functions[C->func_count++] = f;
        if(func_register(f) < 0){
            error_compile(line,0,0,"internal: global registry full (limit %d)",MAX_FUNCS);
            C->had_error=true; return;
        }
        f->parent = C->current_func;

        
        Chunk          *saved_chunk = C->current_chunk;
        Local          *saved_locs  = (Local*)malloc(sizeof(C->locals));
        CtxFrame       *saved_ctx   = (CtxFrame*)malloc(sizeof(C->ctx_stack));
        if(!saved_locs || !saved_ctx){
            free(saved_locs); free(saved_ctx);
            error_compile(line,0,0,"internal: out of memory saving compiler state");
            C->had_error=true; return;
        }
        memcpy(saved_locs, C->locals,    sizeof(C->locals));
        int             saved_lc  = C->local_count;
        bool            saved_if  = C->in_function;
        FunctionObject *saved_cf  = C->current_func;
        memcpy(saved_ctx,  C->ctx_stack, sizeof(C->ctx_stack));
        int             saved_cd  = C->ctx_depth;

        
        if(saved_if){
            for(int ci=0;ci<saved_lc;ci++){
                bool already=false;
                for(int gi=0;gi<C->global_count;gi++){
                    if(!strcmp(C->globals[gi].name,saved_locs[ci].name)){already=true;break;}
                }
                if(!already && C->global_count<MAX_VARIABLES){
                    strncpy(C->globals[C->global_count].name,saved_locs[ci].name,MAX_IDENT_LEN-1);
                    C->globals[C->global_count].index=C->global_count;
                    C->globals[C->global_count].is_let=false;
                    C->globals[C->global_count].defined=true;
                    C->global_count++;
                }
            }
        }

        C->current_chunk = &f->chunk;
        C->local_count   = 0;
        C->in_function   = true;
        C->current_func  = f;
        C->ctx_depth     = 0;

        emit_op(C, OP_GC_SAFEPOINT, line);
        for(int i=0;i<f->arity;i++){
            int slot = local_define(C, f->params[i], false, line);
            C->locals[slot].defined = true;
        }

        if(node->lambda.is_expr_body){
            
            compile_expr(C, node->lambda.body);
            emit_op(C, OP_RETURN, line);
        } else {
            
            compile_stmt(C, node->lambda.body);
            emit_op(C, OP_NIL,    line);
            emit_op(C, OP_RETURN, line);
        }

        
        C->current_chunk = saved_chunk;
        C->local_count   = saved_lc;
        C->in_function   = saved_if;
        C->current_func  = saved_cf;
        C->ctx_depth     = saved_cd;
        memcpy(C->locals,    saved_locs, sizeof(C->locals));
        memcpy(C->ctx_stack, saved_ctx,  sizeof(C->ctx_stack));
        free(saved_locs);
        free(saved_ctx);

        
        emit_const(C, FUNC_VAL(f), line);
        break;
    }

    

    case NODE_CALL_EXPR:{
        
        compile_expr(C, node->call_expr.callee);
        for(int i=0;i<node->call_expr.arg_count;i++)
            compile_expr(C, node->call_expr.args[i]);
        emit_op1(C, OP_CALL, (uint16_t)node->call_expr.arg_count, line);
        break;
    }
    
    case NODE_PROGRAM: case NODE_BLOCK:
    case NODE_VAR_DECL: case NODE_ARRAY_DECL: case NODE_DICT_DECL:
    case NODE_FUNC_DECL: case NODE_STRUCT_DECL:
    case NODE_IF: case NODE_WHILE: case NODE_FOR: case NODE_FOREACH:
    case NODE_DO_WHILE: case NODE_SWITCH:
    case NODE_RETURN: case NODE_BREAK: case NODE_CONTINUE:
    case NODE_PRINT: case NODE_INPUT:
    case NODE_IMPORT: case NODE_EXPORT:
    case NODE_TRY_CATCH: case NODE_EXPR_STMT:
    default:
        error_compile(line, 0, 0,"unexpected expr node %d",node->kind); C->had_error=true;
    }
}

static void compile_stmt(Compiler *C, ASTNode *node){
    if(!node||C->had_error) return;
    int line=node->line;
    switch(node->kind){

    case NODE_VAR_DECL:{
        if(node->var_decl.initializer) compile_expr(C,node->var_decl.initializer);
        else emit_op(C,OP_NIL,line);
        if(C->in_function){
            int slot=local_define(C,node->var_decl.name,node->var_decl.is_let,line);
            C->locals[slot].defined=true;
            

            if(C->current_func && C->current_func->has_captures){
                int gidx=global_define(C,node->var_decl.name,false,line);
                C->globals[gidx].defined=true;
                emit_op(C,OP_DUP,line);
                emit_op1(C,OP_DEF_VAR,(uint16_t)gidx,line);
            }
        } else {
            int idx=global_define(C,node->var_decl.name,node->var_decl.is_let,line);
            C->globals[idx].defined=true;
            emit_op1(C,OP_DEF_VAR,(uint16_t)idx,line);
        }
        break;
    }

    case NODE_ARRAY_DECL:{
        if(node->arr_decl.initializer) compile_expr(C,node->arr_decl.initializer);
        else emit_op(C,OP_ARRAY_NEW,line);
        if(C->in_function){
            int slot=local_define(C,node->arr_decl.name,false,line);
            C->locals[slot].defined=true;
        } else {
            int idx=global_define(C,node->arr_decl.name,false,line);
            C->globals[idx].defined=true;
            emit_op1(C,OP_DEF_VAR,(uint16_t)idx,line);
        }
        break;
    }

    case NODE_DICT_DECL:{
        if(node->dict_decl.initializer) compile_expr(C,node->dict_decl.initializer);
        else emit_op(C,OP_DICT_NEW,line);
        if(C->in_function){
            int slot=local_define(C,node->dict_decl.name,false,line);
            C->locals[slot].defined=true;
        } else {
            int idx=global_define(C,node->dict_decl.name,false,line);
            C->globals[idx].defined=true;
            emit_op1(C,OP_DEF_VAR,(uint16_t)idx,line);
        }
        break;
    }

    case NODE_PRINT:{
        for(int i=0;i<node->print.arg_count;i++) compile_expr(C,node->print.args[i]);
        emit_op1(C,OP_PRINT,(uint16_t)node->print.arg_count,line); break;
    }

    case NODE_INPUT:{
        int var_idx=-1; bool is_local=false;
        if(C->in_function){
            int slot=local_find(C,node->input.target);
            if(slot>=0){ var_idx=slot; is_local=true; }
        }
        if(!is_local){
            int idx=global_find(C,node->input.target);
            if(idx<0){ suggest_variable(C,node->input.target,line); C->had_error=true; return; }
            var_idx=C->globals[idx].index;
        }
        if(node->input.prompt_expr){ compile_expr(C,node->input.prompt_expr); emit_op(C,OP_PROMPT,line); }
        emit_byte(C,(uint8_t)OP_INPUT,line);
        emit_byte(C,(uint8_t)(var_idx&0xFF),line);
        emit_byte(C,(uint8_t)((var_idx>>8)&0xFF),line);
        emit_byte(C,(uint8_t)(is_local?1:0),line);
        break;
    }

    case NODE_IF:{
        compile_expr(C,node->if_stmt.condition);
        int jf=emit_jump(C,OP_JUMP_IF_FALSE,line);
        emit_op(C,OP_POP,line);
        { int slc=C->local_count;
          compile_stmt(C,node->if_stmt.then_branch);
          pop_scope(C,slc,line); }
        int je=emit_jump(C,OP_JUMP,line);
        patch_jump(C,jf);
        emit_op(C,OP_POP,line);
        if(node->if_stmt.else_branch){
            int slc=C->local_count;
            compile_stmt(C,node->if_stmt.else_branch);
            pop_scope(C,slc,line);
        }
        patch_jump(C,je); break;
    }

    case NODE_WHILE:{
        push_ctx(C,CTX_LOOP);
        int loop_start=current_offset(C);
        compile_expr(C,node->while_stmt.condition);
        int jo=emit_jump(C,OP_JUMP_IF_FALSE,line);
        emit_op(C,OP_POP,line);
        { int slc=C->local_count;
          compile_stmt(C,node->while_stmt.body);
          pop_scope(C,slc,line); }
        patch_continues(C,loop_start);
        emit_loop(C,loop_start,line);
        patch_jump(C,jo);
        emit_op(C,OP_POP,line);
        pop_ctx_patch_breaks(C); break;
    }

    

    case NODE_DO_WHILE:{
        push_ctx(C,CTX_LOOP);
        int loop_start=current_offset(C);
        { int slc=C->local_count;
          compile_stmt(C,node->do_while.body);
          pop_scope(C,slc,line); }
        int cont_target=current_offset(C);
        patch_continues(C,cont_target);
        emit_op(C,OP_GC_SAFEPOINT,line);
        compile_expr(C,node->do_while.condition);
        int jt=emit_jump(C,OP_JUMP_IF_TRUE,line);
        emit_op(C,OP_POP,line);  
        int jend=emit_jump(C,OP_JUMP,line); 
        patch_jump(C,jt);
        emit_op(C,OP_POP,line);  
        emit_loop(C,loop_start,line);
        patch_jump(C,jend);
        pop_ctx_patch_breaks(C); break;
    }

    case NODE_FOR:{
        push_ctx(C,CTX_LOOP);
        if(node->for_stmt.init) compile_stmt(C,node->for_stmt.init);
        int loop_start=current_offset(C);
        int jo=-1;
        if(node->for_stmt.condition){
            compile_expr(C,node->for_stmt.condition);
            jo=emit_jump(C,OP_JUMP_IF_FALSE,line);
            emit_op(C,OP_POP,line);
        }
        { int slc=C->local_count;
          compile_stmt(C,node->for_stmt.body);
          pop_scope(C,slc,line); }
        int post_start=current_offset(C);
        patch_continues(C,post_start);
        if(node->for_stmt.post){ compile_expr(C,node->for_stmt.post); emit_op(C,OP_POP,line); }
        emit_loop(C,loop_start,line);
        if(jo>=0){ patch_jump(C,jo); emit_op(C,OP_POP,line); }
        pop_ctx_patch_breaks(C); break;
    }

    case NODE_FOREACH:{
        
        
        push_ctx(C,CTX_LOOP);

        bool fe_in_func = C->in_function;
        int  fe_saved_lc = fe_in_func ? C->local_count : -1;

        
        
        
        #define FE_DEFINE(namestr)             (fe_in_func                 ? (local_define(C,(namestr),false,line))                 : (global_define(C,(namestr),false,line)))
        #define FE_MARK_DEF(slot)             do{ if(fe_in_func) C->locals[slot].defined=true;                 else           C->globals[slot].defined=true; }while(0)
        #define FE_SET(slot)             do{ if(fe_in_func) emit_op1(C,OP_SET_LOCAL,(uint16_t)(slot),line);                 else           emit_op1(C,OP_SET_VAR,  (uint16_t)(slot),line); }while(0)
        #define FE_GET(slot)             do{ if(fe_in_func) emit_op1(C,OP_GET_LOCAL,(uint16_t)(slot),line);                 else           emit_op1(C,OP_GET_VAR,  (uint16_t)(slot),line); }while(0)

        
        
        int arr_slot = FE_DEFINE("__fe_arr");
        FE_MARK_DEF(arr_slot);
        compile_expr(C,node->foreach_stmt.iterable); 
        FE_SET(arr_slot); 
        if(!fe_in_func) emit_op(C,OP_POP,line); 
        

        
        int idx_slot = FE_DEFINE("__fe_idx");
        FE_MARK_DEF(idx_slot);
        emit_const(C,NUMBER_VAL(0),line); 
        FE_SET(idx_slot); 
        if(!fe_in_func) emit_op(C,OP_POP,line); 

        
        
        int body_iv_slot = -1, body_ev_slot = -1;

        if(fe_in_func){
            
            if(node->foreach_stmt.has_index){
                body_iv_slot = local_define(C, node->foreach_stmt.idx_name, false, line);
                C->locals[body_iv_slot].defined = true;
                emit_op(C, OP_NIL, line); 
            }
            
            body_ev_slot = local_define(C, node->foreach_stmt.elem_name, false, line);
            C->locals[body_ev_slot].defined = true;
            emit_op(C, OP_NIL, line); 
        } else {
            
            if(node->foreach_stmt.has_index){
                body_iv_slot = global_define(C, node->foreach_stmt.idx_name, false, line);
                C->globals[body_iv_slot].defined = true;
            }
            body_ev_slot = global_define(C, node->foreach_stmt.elem_name, false, line);
            C->globals[body_ev_slot].defined = true;
        }

        int loop_start = current_offset(C);

        
        FE_GET(idx_slot);
        FE_GET(arr_slot);
        emit_op(C,OP_ARRAY_LEN,line);
        emit_op(C,OP_LT,line);
        int jo = emit_jump(C,OP_JUMP_IF_FALSE,line);
        emit_op(C,OP_POP,line);

        
        if(node->foreach_stmt.has_index){
            FE_GET(idx_slot);
            if(fe_in_func){
                emit_op1(C,OP_SET_LOCAL,(uint16_t)body_iv_slot,line);
                emit_op(C,OP_POP,line);
            } else {
                emit_op1(C,OP_SET_VAR,(uint16_t)body_iv_slot,line);
                emit_op(C,OP_POP,line);
            }
        }
        
        FE_GET(arr_slot);
        FE_GET(idx_slot);
        emit_op(C,OP_ARRAY_INDEX,line);
        if(fe_in_func){
            emit_op1(C,OP_SET_LOCAL,(uint16_t)body_ev_slot,line);
            emit_op(C,OP_POP,line);
        } else {
            emit_op1(C,OP_SET_VAR,(uint16_t)body_ev_slot,line);
            emit_op(C,OP_POP,line);
        }

        
        FE_GET(idx_slot);
        emit_const(C,NUMBER_VAL(1),line);
        emit_op(C,OP_ADD,line);
        FE_SET(idx_slot);
        emit_op(C,OP_POP,line); 

        emit_op(C,OP_GC_SAFEPOINT,line);
        compile_stmt(C,node->foreach_stmt.body);
        

        patch_continues(C,loop_start);
        emit_loop(C,loop_start,line);
        patch_jump(C,jo);
        emit_op(C,OP_POP,line);

        pop_ctx_patch_breaks(C);

        
        
        if(fe_in_func) pop_scope(C,fe_saved_lc,line);

        #undef FE_DEFINE
        #undef FE_MARK_DEF
        #undef FE_SET
        #undef FE_GET
        break;
    }

    case NODE_SWITCH:{
        push_ctx(C,CTX_SWITCH);
        compile_expr(C,node->switch_stmt.subject);
        int n_cases=node->switch_stmt.case_count;
        for(int i=0;i<n_cases;i++){
            SwitchCase *sc=&node->switch_stmt.cases[i];
            emit_op(C,OP_DUP,line);
            compile_expr(C,sc->value);
            emit_op(C,OP_EQ,line);
            int skip=emit_jump(C,OP_JUMP_IF_FALSE,line);
            emit_op(C,OP_POP,line);
            emit_op(C,OP_POP,line);
            compile_stmt(C,sc->body);
            emit_break(C,line);
            patch_jump(C,skip);
            emit_op(C,OP_POP,line);
        }
        if(node->switch_stmt.default_body) compile_stmt(C,node->switch_stmt.default_body);
        emit_op(C,OP_POP,line);
        pop_ctx_patch_breaks(C); break;
    }

    case NODE_TRY_CATCH:{
        
        
        int handler_patch=emit_jump(C,OP_PUSH_HANDLER,line);
        compile_stmt(C,node->try_catch.try_body);
        emit_op(C,OP_POP_HANDLER,line);
        int skip_catch=emit_jump(C,OP_JUMP,line);
        patch_jump(C,handler_patch);
        
        
        bool tc_local = C->in_function;
        int saved_lc = tc_local ? C->local_count : -1;
        if(tc_local){
            int err_slot=local_define(C,node->try_catch.err_name,false,line);
            C->locals[err_slot].defined=true;
            emit_op1(C,OP_SET_LOCAL,(uint16_t)err_slot,line);
            compile_stmt(C,node->try_catch.catch_body);
            pop_scope(C,saved_lc,line);
        } else {
            int err_slot=global_define(C,node->try_catch.err_name,false,line);
            C->globals[err_slot].defined=true;
            emit_op1(C,OP_SET_VAR,(uint16_t)err_slot,line);
            compile_stmt(C,node->try_catch.catch_body);
        }
        patch_jump(C,skip_catch);
        break;
    }

    case NODE_THROW:{
        if(!node->throw_stmt.value){
            error_compile(line,0,0,"'throw' requires a value");
            C->had_error=true; break;
        }
        compile_expr(C,node->throw_stmt.value);
        if(!C->had_error) emit_op(C,OP_THROW,line);
        break;
    }

    case NODE_BREAK:    emit_break(C,line);    break;
    case NODE_CONTINUE: emit_continue(C,line); break;

    case NODE_BLOCK:{
        

        bool dead=false;
        for(int i=0;i<node->block.count;i++){
            ASTNode *s=node->block.stmts[i];
            if(!s) continue;   /* defensive: parser filters NULLs but guard anyway */
            if(dead){
                
                if(s->kind==NODE_EXPR_STMT && !s->expr_stmt.expr) continue;
                error_compile(s->line, 0, 0, "unreachable code after unconditional return/break/continue");
                C->had_error=true;
                break;
            }
            compile_stmt(C,s);
            if(s->kind==NODE_RETURN || s->kind==NODE_BREAK || s->kind==NODE_CONTINUE)
                dead=true;
        }
        break;
    }

    case NODE_EXPR_STMT:
        compile_expr(C,node->expr_stmt.expr);
        emit_op(C,OP_POP,line); break;

    case NODE_RETURN:{
        if(!C->in_function){ error_compile(line, 0, 0,"'return' outside a function"); C->had_error=true; return; }
        if(node->ret.value){
            

            ASTNode *rv=node->ret.value;
            if(C->opt_level>=2 && rv->kind==NODE_CALL){
                
                bool is_static = false;
                for(int i=0;i<C->func_count;i++)
                    if(!strcmp(C->functions[i]->name,rv->call.name)){is_static=true;break;}
                for(int i=0;!is_static&&i<C->import_count;i++)
                    if(!strcmp(C->imports[i]->name,rv->call.name)){is_static=true;break;}
                if(is_static){
                    FuncRef ref=resolve_function(C,rv->call.name,line);
                    if(ref.func && !C->had_error &&
                       rv->call.arg_count==ref.func->arity){
                        emit_const(C,FUNC_VAL(ref.func),line);
                        for(int i=0;i<rv->call.arg_count;i++) compile_expr(C,rv->call.args[i]);
                        emit_op1(C,OP_CALL_TAIL,(uint16_t)rv->call.arg_count,line);
                        break;
                    }
                }
            }
            compile_expr(C,node->ret.value);
        } else {
            emit_op(C,OP_NIL,line);
        }
        emit_op(C,OP_RETURN,line); break;
    }

    case NODE_FUNC_DECL:{
        FunctionObject *f=NULL;
        for(int i=0;i<C->func_count;i++){
            if(!strcmp(C->functions[i]->name,node->func_decl.name)){ f=C->functions[i]; break; }
        }
        if(!f){
            f=func_new(node->func_decl.name,node->func_decl.visibility,C->source_file);
            f->arity=node->func_decl.param_count;
            
            f->params=(char**)malloc((size_t)node->func_decl.param_count*sizeof(char*));
            f->param_cap=node->func_decl.param_count;
            for(int i=0;i<f->arity;i++) f->params[i]=strdup(node->func_decl.params[i]);
            if(C->func_count >= MAX_FUNCS){
                error_compile(line,0,0,"internal: function table full (limit %d), cannot define '%s'",
                              MAX_FUNCS, node->func_decl.name);
                C->had_error=true; return;
            }
            C->functions[C->func_count++]=f;
        } else {
            f->visibility=node->func_decl.visibility;
            strncpy(f->source_file,C->source_file,sizeof(f->source_file)-1);
        }
        f->parent=C->current_func;
        if(C->current_func){
            if(C->current_func->nested_count>=C->current_func->nested_cap){
                int nc=C->current_func->nested_cap<8?8:C->current_func->nested_cap*2;
                C->current_func->nested=(FunctionObject**)realloc(C->current_func->nested,(size_t)nc*sizeof(FunctionObject*));
                C->current_func->nested_cap=nc;
            }
            C->current_func->nested[C->current_func->nested_count++]=f;
        }
        if(func_register(f) < 0){
            error_compile(line,0,0,"internal: global registry full (limit %d)",MAX_FUNCS);
            C->had_error=true; return;
        }

        
        f->has_captures = node->func_decl.body && has_nested_func(node->func_decl.body);

        Chunk          *saved_chunk=C->current_chunk;
        Local          *saved_locs=(Local*)malloc(sizeof(C->locals));
        CtxFrame       *saved_ctx=(CtxFrame*)malloc(sizeof(C->ctx_stack));
        Symbol         *saved_globals=(Symbol*)malloc(sizeof(C->globals));
        if(!saved_locs||!saved_ctx||!saved_globals){
            free(saved_locs); free(saved_ctx); free(saved_globals);
            error_compile(line,0,0,"internal: out of memory saving compiler state");
            C->had_error=true; return;
        }
        memcpy(saved_locs,C->locals,sizeof(C->locals));
        int             saved_lc=C->local_count;
        bool            saved_if=C->in_function;
        FunctionObject *saved_cf=C->current_func;
        memcpy(saved_ctx,C->ctx_stack,sizeof(C->ctx_stack));
        int             saved_cd=C->ctx_depth;

        
        int saved_gc = C->global_count;
        memcpy(saved_globals, C->globals, sizeof(C->globals));

        

        if(saved_if) {
            for(int ci = 0; ci < saved_lc; ci++){
                bool already = false;
                for(int gi = 0; gi < C->global_count; gi++){
                    if(!strcmp(C->globals[gi].name, saved_locs[ci].name)){
                        already = true; break;
                    }
                }
                if(!already && C->global_count < MAX_VARIABLES){
                    strncpy(C->globals[C->global_count].name,
                            saved_locs[ci].name, MAX_IDENT_LEN-1);
                    C->globals[C->global_count].index   = C->global_count;
                    C->globals[C->global_count].is_let  = saved_locs[ci].is_let;
                    C->globals[C->global_count].defined = true;
                    C->global_count++;
                }
            }
        }

        C->current_chunk=&f->chunk;
        C->local_count=0; C->in_function=true; C->current_func=f; C->ctx_depth=0;

        emit_op(C,OP_GC_SAFEPOINT,line);
        for(int i=0;i<f->arity;i++){
            int slot=local_define(C,f->params[i],false,line);
            C->locals[slot].defined=true;
            
            if(f->has_captures){
                int gidx=global_define(C,f->params[i],false,line);
                C->globals[gidx].defined=true;
                emit_op1(C,OP_GET_LOCAL,(uint16_t)slot,line);
                emit_op(C,OP_DUP,line);
                emit_op1(C,OP_DEF_VAR,(uint16_t)gidx,line);
                emit_op(C,OP_POP,line);
            }
        }
        compile_stmt(C,node->func_decl.body);
        emit_op(C,OP_NIL,line);
        emit_op(C,OP_RETURN,line);

        
        C->current_chunk=saved_chunk; C->local_count=saved_lc;
        C->in_function=saved_if; C->current_func=saved_cf; C->ctx_depth=saved_cd;
        memcpy(C->locals,saved_locs,sizeof(C->locals));
        memcpy(C->ctx_stack,saved_ctx,sizeof(C->ctx_stack));
        free(saved_ctx);
        for(int gi = saved_gc; gi < C->global_count; gi++){
            bool outer = false;
            for(int ci = 0; ci < saved_lc; ci++){
                if(!strcmp(C->globals[gi].name, saved_locs[ci].name)){ outer=true; break; }
            }
            if(!outer){ 
                saved_globals[gi] = C->globals[gi];
            }
        }
        free(saved_locs);
        memcpy(C->globals, saved_globals, sizeof(C->globals));
        free(saved_globals);
        C->global_count = saved_gc > C->global_count ? saved_gc : C->global_count;
        break;
    }

    case NODE_IMPORT:{
        
        int imp_before = C->import_count;
        bool import_ok = false;

        if(node->import.is_lib){
            bool (*lib_handler)(const char*,Compiler*)=C->import_handler?C->import_handler:chn_import_handler;
            char cco_key[1032];
            snprintf(cco_key, sizeof(cco_key), "::lib:%s", node->import.path);
            if(lib_handler){
                import_ok = lib_handler(cco_key, C);
                if(!import_ok){ error_compile_import(line, 0, 0,"failed to load CCO bundle '%s'",node->import.path); C->had_error=true; }
            } else { error_compile_import(line, 0, 0,"import not supported"); C->had_error=true; }
        } else {
            bool (*handler)(const char*,Compiler*)=C->import_handler?C->import_handler:chn_import_handler;
            if(handler){
                import_ok = handler(node->import.path,C);
                if(!import_ok){ error_compile_import(line, 0, 0,"failed to import '%s'",node->import.path); C->had_error=true; }
            } else { error_compile_import(line, 0, 0,"import not supported"); C->had_error=true; }
        }

        
        if(import_ok && node->import.alias[0]){
            if(C->alias_count >= 64){
                error_compile_import(line,0,0,"too many module aliases (limit 64)");
                C->had_error=true;
            } else {
                C->aliases[C->alias_count].import_start = imp_before;
                C->aliases[C->alias_count].import_end   = C->import_count;
                strncpy(C->aliases[C->alias_count].alias, node->import.alias, 255);
                C->alias_count++;
            }
        }
        break;
    }

    case NODE_EXPORT:{
        if(node->export_node.func_def){ compile_stmt(C,node->export_node.func_def); if(C->had_error) break; }
        bool found=false;
        for(int i=0;i<C->func_count;i++){
            if(!strcmp(C->functions[i]->name,node->export_node.name)){
                if(C->functions[i]->visibility==VIS_PRIVATE){
                    error_compile_access(line, 0, 0,"cannot export private function '%s'",node->export_node.name);
                    C->had_error=true;
                } else { C->functions[i]->exported=true; }
                found=true; break;
            }
        }
        if(!found){ error_compile_ref(line, 0, 0,"cannot export '%s': no such function",node->export_node.name); C->had_error=true; }
        break;
    }

    case NODE_NATIVE_CALL:{
        for(int i=0;i<node->native_call.argc;i++) compile_expr(C,node->native_call.args[i]);
        emit_op(C,OP_NATIVE,line);
        uint16_t nid=node->native_call.call_id;
        emit_byte(C,(uint8_t)(nid&0xFF),line);
        emit_byte(C,(uint8_t)(nid>>8),line);
        emit_byte(C,(uint8_t)node->native_call.argc,line);
        break;
    }

    

    case NODE_STRUCT_DECL:{
        int fc = node->struct_decl.field_count;
        const char *sname = node->struct_decl.name;

        

        FunctionObject *f = NULL;
        for(int i=0;i<C->func_count;i++){
            if(!strcmp(C->functions[i]->name,sname)){ f=C->functions[i]; break; }
        }
        if(!f){
            f = func_new(sname, VIS_PUBLIC, C->source_file);
            f->arity = fc;
            f->params = (char**)malloc((size_t)fc * sizeof(char*));
            f->param_cap = fc;
            for(int i=0;i<fc;i++) f->params[i] = strdup(node->struct_decl.fields[i]);
            if(C->func_count >= MAX_FUNCS){
                error_compile(line,0,0,"internal: function table full (limit %d), cannot define struct '%s'",
                              MAX_FUNCS, sname);
                C->had_error=true; return;
            }
            C->functions[C->func_count++] = f;
            if(func_register(f) < 0){
                error_compile(line,0,0,"internal: global registry full (limit %d)",MAX_FUNCS);
                C->had_error=true; return;
            }
        } else {
            
            f->visibility = VIS_PUBLIC;
            strncpy(f->source_file, C->source_file, sizeof(f->source_file)-1);
        }

        
        Chunk          *saved_chunk = C->current_chunk;
        Local          *saved_locs  = (Local*)malloc(sizeof(C->locals));
        CtxFrame       *saved_ctx   = (CtxFrame*)malloc(sizeof(C->ctx_stack));
        if(!saved_locs || !saved_ctx){
            free(saved_locs); free(saved_ctx);
            error_compile(line,0,0,"internal: out of memory saving compiler state");
            C->had_error=true; return;
        }
        memcpy(saved_locs, C->locals,    sizeof(C->locals));
        int             saved_lc    = C->local_count;
        bool            saved_if    = C->in_function;
        FunctionObject *saved_cf    = C->current_func;
        memcpy(saved_ctx,  C->ctx_stack, sizeof(C->ctx_stack));
        int             saved_cd    = C->ctx_depth;

        C->current_chunk = &f->chunk;
        C->local_count   = 0;
        C->in_function   = true;
        C->current_func  = f;
        C->ctx_depth     = 0;

        emit_op(C, OP_GC_SAFEPOINT, line);

        
        for(int i=0;i<fc;i++){
            int slot = local_define(C, node->struct_decl.fields[i], false, line);
            C->locals[slot].defined = true;
        }

        
        emit_op(C, OP_DICT_NEW, line);
        int dict_slot = local_define(C, "__struct_d", false, line);
        C->locals[dict_slot].defined = true;

        
        for(int i=0;i<fc;i++){
            
            emit_op1(C, OP_GET_LOCAL, (uint16_t)dict_slot, line);

            
            ObjString *fkey = gc_cstring(node->struct_decl.fields[i]);
            emit_const(C, STRING_VAL(fkey), line);

            
            int param_slot = i; 
            emit_op1(C, OP_GET_LOCAL, (uint16_t)param_slot, line);

            if(node->struct_decl.defaults[i] &&
               node->struct_decl.defaults[i]->kind != NODE_NULL &&
               node->struct_decl.defaults[i]->kind != NODE_NIL){
                
                int j_notnull = emit_jump(C, OP_JUMP_IF_NIL, line);
                int j_end     = emit_jump(C, OP_JUMP, line);
                patch_jump(C, j_notnull);
                emit_op(C, OP_POP, line);
                compile_expr(C, node->struct_decl.defaults[i]);
                patch_jump(C, j_end);
            }

            
            emit_op(C, OP_DICT_SET, line);
            
            emit_op1(C, OP_SET_LOCAL, (uint16_t)dict_slot, line);
            emit_op(C, OP_POP, line);
        }

        
        emit_op1(C, OP_GET_LOCAL, (uint16_t)dict_slot, line);
        emit_op(C, OP_RETURN, line);

        
        C->current_chunk = saved_chunk;
        C->local_count   = saved_lc;
        C->in_function   = saved_if;
        C->current_func  = saved_cf;
        C->ctx_depth     = saved_cd;
        memcpy(C->locals,   saved_locs, sizeof(C->locals));
        memcpy(C->ctx_stack,saved_ctx,  sizeof(C->ctx_stack));
        free(saved_locs);
        free(saved_ctx);
        break;
    }

    
    case NODE_NULL:
        emit_op(C,OP_NIL,line);
        emit_op(C,OP_POP,line); break;

    default:
        error_compile(line, 0, 0,"unexpected statement node %d",node->kind); C->had_error=true;
    }
}

static void prescan_stmt(Compiler *C, ASTNode *node){
    if(!node) return;
    switch(node->kind){
        case NODE_FUNC_DECL:{
            const char *fname=node->func_decl.name;
            bool already=false;
            for(int i=0;i<C->func_count;i++) if(!strcmp(C->functions[i]->name,fname)){already=true;break;}
            if(!already&&C->func_count<MAX_FUNCS){
                FunctionObject *f=func_new(fname,node->func_decl.visibility,C->source_file);
                f->arity=node->func_decl.param_count;
                f->params=(char**)malloc((size_t)node->func_decl.param_count*sizeof(char*));
                f->param_cap=node->func_decl.param_count;
                for(int i=0;i<f->arity;i++) f->params[i]=strdup(node->func_decl.params[i]);
                C->functions[C->func_count++]=f;
            }
            break;
        }
        case NODE_EXPORT:
            if(node->export_node.func_def){ prescan_stmt(C,node->export_node.func_def); }
            break;
        

        case NODE_STRUCT_DECL:{
            const char *sname=node->struct_decl.name;
            bool already=false;
            for(int i=0;i<C->func_count;i++) if(!strcmp(C->functions[i]->name,sname)){already=true;break;}
            if(!already&&C->func_count<MAX_FUNCS){
                FunctionObject *sf=func_new(sname,VIS_PUBLIC,C->source_file);
                sf->arity=node->struct_decl.field_count;
                sf->params=(char**)malloc((size_t)sf->arity*sizeof(char*));
                sf->param_cap=sf->arity;
                for(int i=0;i<sf->arity;i++) sf->params[i]=strdup(node->struct_decl.fields[i]);
                C->functions[C->func_count++]=sf;
            }
            break;
        }
        case NODE_PROGRAM:
            for(int i=0;i<node->program.count;i++) prescan_stmt(C,node->program.stmts[i]);
            break;
        default: break;
    }
}

static bool ast_fold_constants(ASTNode *node, int opt_level){
    if(!node || opt_level < 1) return false;
    if(node->kind==NODE_NUMBER) return true;  

    if(node->kind==NODE_BINARY){
        bool l=ast_fold_constants(node->binary.left,  opt_level);
        bool r=ast_fold_constants(node->binary.right, opt_level);
        if(l && r &&
           node->binary.left->kind==NODE_NUMBER &&
           node->binary.right->kind==NODE_NUMBER){
            double L=node->binary.left->num.value;
            double R=node->binary.right->num.value;
            double result=0.0; bool ok=true;
            switch(node->binary.op){
                case TK_PLUS:     result=L+R;                          break;
                case TK_MINUS:    result=L-R;                          break;
                case TK_STAR:     result=L*R;                          break;
                case TK_SLASH:    if(R!=0){result=L/R;}else ok=false;  break;
                case TK_PERCENT:  if(R!=0){result=fmod(L,R);}else ok=false; break;
                case TK_STARSTAR: result=pow(L,R);                     break;
                default:          ok=false;                             break;
            }
            if(ok){
                
                node->kind=NODE_NUMBER;
                node->num.value=result;
                

                return true;
            }
        }
    }

    if(node->kind==NODE_UNARY &&
       node->unary.op==TK_MINUS){
        if(ast_fold_constants(node->unary.operand,opt_level) &&
           node->unary.operand->kind==NODE_NUMBER){
            node->kind=NODE_NUMBER;
            node->num.value=-node->unary.operand->num.value;
            return true;
        }
    }
    return false;
}

static void fold_all(ASTNode *node, int opt_level);
static void fold_node(ASTNode *node, int opt_level){
    if(!node) return;
    ast_fold_constants(node, opt_level);  
    fold_all(node, opt_level);            
}
static void fold_all(ASTNode *node, int opt_level){
    if(!node || opt_level < 1) return;
    switch(node->kind){
        case NODE_BLOCK:
            for(int i=0;i<node->block.count;i++){ fold_node(node->block.stmts[i],opt_level); } break;
        case NODE_PROGRAM:
            for(int i=0;i<node->program.count;i++){ fold_node(node->program.stmts[i],opt_level); } break;
        case NODE_VAR_DECL:  fold_node(node->var_decl.initializer, opt_level); break;
        case NODE_FUNC_DECL: fold_node(node->func_decl.body, opt_level); break;
        case NODE_LAMBDA:    fold_node(node->lambda.body,    opt_level); break;
        case NODE_CALL_EXPR:
            fold_node(node->call_expr.callee, opt_level);
            for(int i=0;i<node->call_expr.arg_count;i++)
                fold_node(node->call_expr.args[i], opt_level);
            break;
        case NODE_IF:
            fold_node(node->if_stmt.condition,   opt_level);
            fold_node(node->if_stmt.then_branch, opt_level);
            fold_node(node->if_stmt.else_branch, opt_level); break;
        case NODE_WHILE:
            fold_node(node->while_stmt.condition, opt_level);
            fold_node(node->while_stmt.body,      opt_level); break;
        case NODE_DO_WHILE:             
            fold_node(node->do_while.body,      opt_level);
            fold_node(node->do_while.condition, opt_level); break;
        case NODE_FOR:
            fold_node(node->for_stmt.init,      opt_level);
            fold_node(node->for_stmt.condition,  opt_level);
            fold_node(node->for_stmt.post,       opt_level);
            fold_node(node->for_stmt.body,       opt_level); break;
        case NODE_RETURN:    fold_node(node->ret.value, opt_level); break;
        case NODE_EXPR_STMT: fold_node(node->expr_stmt.expr, opt_level); break;
        case NODE_ASSIGN:    fold_node(node->assign.value, opt_level); break;
        case NODE_PRINT:
            for(int i=0;i<node->print.arg_count;i++){ fold_node(node->print.args[i],opt_level); } break;
        case NODE_CALL:
            for(int i=0;i<node->call.arg_count;i++){ fold_node(node->call.args[i],opt_level); } break;
        default: break;
    }
}

static int instr_width(uint8_t op){
    switch((OpCode)op){
        
        case OP_NIL: case OP_TRUE: case OP_FALSE:
        case OP_POP: case OP_DUP:
        case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
        case OP_MOD: case OP_NEG: case OP_POW:
        case OP_BAND: case OP_BOR: case OP_BXOR: case OP_BNOT:
        case OP_LSHIFT: case OP_RSHIFT:
        case OP_EQ: case OP_NEQ: case OP_LT: case OP_GT:
        case OP_LE: case OP_GE: case OP_NOT:
        case OP_ARRAY_LEN: case OP_TYPEOF:
        case OP_GC_SAFEPOINT: case OP_RETURN:
        case OP_THROW: case OP_GET_ERROR:
        case OP_CONST_0: case OP_CONST_1:
        case OP_AWAIT:          
        case OP_IN:             
        case OP_HALT:
            return 1;
        
        case OP_CONST: case OP_GET_VAR: case OP_SET_VAR: case OP_DEF_VAR:
        case OP_GET_LOCAL: case OP_SET_LOCAL:
        case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: case OP_JUMP_IF_NIL:
        case OP_CALL: case OP_CALL_TAIL:
        case OP_METHOD_CALL:         
        case OP_FOREACH_STEP:        
        case OP_PUSH_HANDLER:        
        case OP_INC_LOCAL: case OP_DEC_LOCAL:
        case OP_PRINT:
            return 3;   /* opcode(1) + uint16 argc(2) */
        case OP_PROMPT:
            return 1;   /* opcode only, no operands */
        case OP_INPUT:
            return 4;   /* opcode(1) + uint16 idx(2) + is_local(1) */
        

        case OP_ARRAY_NEW: case OP_ARRAY_PUSH:
        case OP_ARRAY_INDEX: case OP_ARRAY_SET:
        case OP_DICT_NEW: case OP_DICT_SET: case OP_DICT_GET:
        case OP_FOREACH_INIT:   
        case OP_POP_HANDLER:    
            return 1;
        
        case OP_NATIVE:
            return 4;
        default:
            return 1;
    }
}

static uint16_t read_u16_at(uint8_t *code, int pos){
    return (uint16_t)(code[pos] | (code[pos+1]<<8));
}

static void peephole_optimize(Chunk *ch){
    uint8_t *code = ch->code;
    int len = ch->code_len;
    int i = 0;
    while(i < len){
        OpCode op = (OpCode)code[i];
        int w = instr_width(code[i]);
        int next = i + w;
        if(next >= len) { i += w; continue; }

        OpCode op2 = (OpCode)code[next];
        int w2 = instr_width(code[next]);

        
        if(op2==OP_POP &&
           (op==OP_NIL||op==OP_TRUE||op==OP_FALSE||
            op==OP_CONST||op==OP_CONST_0||op==OP_CONST_1)){
            

            for(int k=i;k<next+w2;k++) code[k]=(uint8_t)OP_GC_SAFEPOINT;
            i = next + w2;
            continue;
        }

        
        if(op==OP_NOT && op2==OP_NOT){
            code[i]=(uint8_t)OP_GC_SAFEPOINT;
            code[next]=(uint8_t)OP_GC_SAFEPOINT;
            i = next + w2;
            continue;
        }

        
        if((op==OP_JUMP||op==OP_JUMP_IF_FALSE||op==OP_JUMP_IF_TRUE||op==OP_JUMP_IF_NIL)
            && w==3){
            uint16_t target = read_u16_at(code, i+1);
            int visited = 0;
            while(visited < 32 && target < (uint16_t)len &&
                  code[target]==OP_JUMP){
                uint16_t next_target = read_u16_at(code, target+1);
                if(next_target == target) break; 
                target = next_target;
                visited++;
            }
            code[i+1] = (uint8_t)(target & 0xFF);
            code[i+2] = (uint8_t)((target >> 8) & 0xFF);
        }

        i += w;
    }
}

static const char *compiletime_typeof(ASTNode *node){
    if(!node) return NULL;
    switch(node->kind){
        case NODE_NUMBER:   return "number";
        case NODE_STRING:
        case NODE_FSTRING:  return "string";
        case NODE_BOOL:     return "bool";
        case NODE_NIL:
        case NODE_NULL:     return "nil";   
        default:            return NULL;
    }
}

static void compute_stack_size(Chunk *ch){
    if(!ch||ch->code_len==0){ ch->stack_size=0; return; }
    int depth=0, max_depth=0;
    uint8_t *code=ch->code;
    int i=0, len=ch->code_len;
    while(i<len){
        OpCode op=(OpCode)code[i];
        
        int delta=0;
        switch(op){
            case OP_CONST: case OP_CONST_0: case OP_CONST_1:
            case OP_NIL: case OP_TRUE: case OP_FALSE: delta=+1; break;
            case OP_POP:  delta=-1; break;
            case OP_DUP:  delta=+1; break;
            case OP_GET_VAR: case OP_GET_LOCAL: delta=+1; break;
            case OP_SET_VAR: case OP_SET_LOCAL: delta= 0; break; 
            case OP_DEF_VAR: delta=-1; break;
            case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
            case OP_MOD: case OP_POW:
            case OP_BAND: case OP_BOR: case OP_BXOR:
            case OP_LSHIFT: case OP_RSHIFT:
            case OP_EQ: case OP_NEQ: case OP_LT: case OP_GT:
            case OP_LE: case OP_GE: delta=-1; break; 
            case OP_NEG: case OP_NOT: case OP_BNOT: delta=0; break;
            case OP_TYPEOF: delta=0; break;
            case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE:
            case OP_JUMP_IF_NIL: delta=0; break;
            case OP_CALL:{ int argc=(int)(code[i+1]|(code[i+2]<<8));
                           delta=-(argc); break; } 
            case OP_CALL_TAIL:{ int argc=(int)(code[i+1]|(code[i+2]<<8));
                                 delta=-(argc); break; }
            case OP_RETURN: delta=-1; break;
            case OP_ARRAY_NEW: delta=+1; break;
            case OP_ARRAY_PUSH: delta=-1; break;
            case OP_ARRAY_INDEX: delta=-1; break;
            case OP_ARRAY_SET: delta=-2; break;
            case OP_ARRAY_LEN: delta=0; break;
            case OP_DICT_NEW: delta=+1; break;
            case OP_DICT_SET: delta=-2; break;
            case OP_DICT_GET: delta=-1; break;
            case OP_METHOD_CALL:{ 
                                   int argc=(int)(code[i+2]);
                                   delta=-(argc); break; }
            case OP_THROW: delta=-1; break;
            case OP_GET_ERROR: delta=+1; break;
            case OP_PUSH_HANDLER: delta=0; break;
            case OP_POP_HANDLER: delta=0; break;
            case OP_PRINT:{ int n=(int)(code[i+1]|(code[i+2]<<8)); delta=-n; break; }
            case OP_FOREACH_INIT: delta=+2; break; 
            case OP_FOREACH_STEP: delta=-1; break;
            case OP_INC_LOCAL: case OP_DEC_LOCAL: delta=0; break;
            case OP_NATIVE: delta=0; break; 
            case OP_GC_SAFEPOINT: delta=0; break;
            case OP_AWAIT: delta=0; break;  
            case OP_IN:   delta=-1; break;  
            case OP_HALT: goto done;
            default: delta=0; break;
        }
        depth+=delta;
        if(depth<0) depth=0; 
        if(depth>max_depth) max_depth=depth;
        
        switch(op){
            case OP_CONST: case OP_GET_VAR: case OP_SET_VAR: case OP_DEF_VAR:
            case OP_GET_LOCAL: case OP_SET_LOCAL:
            case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: case OP_JUMP_IF_NIL:
            case OP_CALL: case OP_CALL_TAIL: case OP_METHOD_CALL:
            case OP_FOREACH_STEP:   
            case OP_PUSH_HANDLER:   
            case OP_INC_LOCAL: case OP_DEC_LOCAL:
            case OP_PRINT: case OP_INPUT: case OP_PROMPT:
                i+=3; break;
            
            case OP_ARRAY_NEW: case OP_ARRAY_PUSH:
            case OP_ARRAY_INDEX: case OP_ARRAY_SET:
            case OP_DICT_NEW: case OP_DICT_SET: case OP_DICT_GET:
            case OP_FOREACH_INIT: case OP_POP_HANDLER:
                i+=1; break;
            case OP_NATIVE: i+=4; break;
            default: i+=1; break;
        }
        continue;
    }
done:
    ch->stack_size=max_depth+4; 
}
void compiler_init(Compiler *C, const char *source_file){
    memset(C,0,sizeof(Compiler));
    chunk_init(&C->top_chunk);
    C->current_chunk=&C->top_chunk;
    strncpy(C->source_file,source_file?source_file:"<unknown>",1023);
    C->opt_level=2; 
}

void compiler_free(Compiler *C){
    chunk_free(&C->top_chunk);
    
}

bool compiler_compile(Compiler *C, ASTNode *ast){
    if(!ast) return false;

    
    if(C->opt_level>=1) fold_all(ast, C->opt_level);

    prescan_stmt(C,ast);

    if(ast->kind==NODE_PROGRAM){
        int entry_count=0;
        ASTNode *entry_node=NULL;
        for(int i=0;i<ast->program.count;i++){
            ASTNode *s=ast->program.stmts[i];
            if(s->kind==NODE_FUNC_DECL && s->func_decl.is_entry){
                entry_count++;
                if(entry_count==1) entry_node=s;
                else{
                    error_compile(s->line, 0, 0, "'entry main' is already defined -- it can only appear once in a file");
                    C->had_error=true;
                }
            }
        }

        

        if(entry_count>0 && !C->had_error){
            for(int i=0;i<ast->program.count;i++){
                ASTNode *s=ast->program.stmts[i];
                if(s->kind==NODE_FUNC_DECL)   continue;
                if(s->kind==NODE_STRUCT_DECL)  continue;
                if(s->kind==NODE_IMPORT)       continue;
                if(s->kind==NODE_EXPORT)       continue;
                error_compile(s->line, 0, 0, "top-level code outside 'entry main' is not allowed");
                C->had_error=true;
            }
        }

        if(!C->had_error){
            if(entry_node){
                

                for(int i=0;i<ast->program.count&&!C->had_error;i++){
                    ASTNode *s=ast->program.stmts[i];
                    if(s->kind==NODE_IMPORT)     { compile_stmt(C,s); continue; }
                    if(s->kind==NODE_EXPORT)     { compile_stmt(C,s); continue; }
                    if(s->kind==NODE_STRUCT_DECL){ compile_stmt(C,s); continue; }
                    if(s->kind==NODE_FUNC_DECL && !s->func_decl.is_entry){
                        compile_stmt(C,s); continue;
                    }
                }
                
                if(!C->had_error && entry_node->func_decl.body){
                    C->has_entry_main = true;   
                    ASTNode *body=entry_node->func_decl.body;
                    for(int i=0;i<body->block.count&&!C->had_error;i++)
                        compile_stmt(C,body->block.stmts[i]);
                }
            } else {
                
                for(int i=0;i<ast->program.count&&!C->had_error;i++)
                    compile_stmt(C,ast->program.stmts[i]);
            }
        }
    } else {
        compile_stmt(C,ast);
    }
    emit_op(C,OP_HALT,0);

    
    if(!C->had_error && C->opt_level>=1){
        peephole_optimize(C->current_chunk);
        for(int i=0;i<C->func_count;i++)
            peephole_optimize(&C->functions[i]->chunk);
    }

    
    if(!C->had_error){
        compute_stack_size(C->current_chunk);
        for(int i=0;i<C->func_count;i++)
            compute_stack_size(&C->functions[i]->chunk);
    }

    return !C->had_error;
}

static void print_val(Value v){
    switch(v.type){
        case VAL_NUMBER:
            if(v.as.number==(long long)v.as.number) printf("%lld",(long long)v.as.number);
            else                              printf("%.14g",v.as.number);
            break;
        case VAL_STRING:   printf("\"%s\"",v.as.string->chars); break;
        case VAL_BOOL:     printf("%s",v.as.boolean?"true":"false"); break;
        case VAL_NIL:      printf("nil"); break;
        case VAL_FUNCTION: printf("<func %s>",v.as.function->name); break;
        case VAL_ARRAY:    printf("<array>"); break;
        case VAL_DICT:     printf("<dict>"); break;
    }
}

#define RU16(code,ip) ((uint16_t)(code)[(ip)]|((uint16_t)(code)[(ip)+1]<<8))

void chunk_disasm(Chunk *ch, const char *name){
    printf("\n+== %s ==\n",name);
    printf("|  OFFSET  LINE  OPCODE                  OPERAND\n");
    printf("+==========================================================\n");
    int ip=0;
    while(ip<ch->code_len){
        int off=ip;
        int ln=chunk_line_at(ch,ip);
        OpCode op=(OpCode)ch->code[ip++];
        printf("|  %05d   %3d   %-24s",off,ln,opcode_name(op));
        switch(op){
            case OP_CONST:{
                uint16_t i=RU16(ch->code,ip); ip+=2;
                printf(" [%d] ",i);
                if(i<(uint16_t)ch->const_count) print_val(ch->constants[i]);
                break;}
            case OP_GET_VAR: case OP_SET_VAR: case OP_DEF_VAR:{
                uint16_t i=RU16(ch->code,ip); ip+=2;
                const char *vn=(i<(uint16_t)ch->var_count)?ch->var_names[i]:"?";
                printf(" [%d] %s",i,vn); break;}
            case OP_GET_LOCAL: case OP_SET_LOCAL:{
                uint16_t i=RU16(ch->code,ip); ip+=2;
                printf(" slot[%d]",i); break;}
            case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_JUMP_IF_TRUE: case OP_JUMP_IF_NIL:{
                uint16_t t=RU16(ch->code,ip); ip+=2;
                printf(" -> %d",t); break;}
            case OP_PUSH_HANDLER:{
                uint16_t t=RU16(ch->code,ip); ip+=2;
                printf(" catch@%d",t); break;}
            case OP_FOREACH_STEP:{
                uint16_t t=RU16(ch->code,ip); ip+=2;
                printf(" end@%d",t); break;}
            case OP_CALL: case OP_PRINT:{
                uint16_t n=RU16(ch->code,ip); ip+=2;
                printf(" (%d args)",n); break;}
            case OP_METHOD_CALL:{
                uint8_t mid=ch->code[ip++],argc=ch->code[ip++];
                printf(" .%s(%d args)",method_name((ArrayMethod)mid),argc); break;}
            case OP_INPUT:{
                uint16_t idx=RU16(ch->code,ip); uint8_t il=ch->code[ip+2]; ip+=3;
                printf(" -> %s[%d]",il?"local":"global",idx); break;}
            case OP_NATIVE:{
                uint16_t nid=RU16(ch->code,ip); uint8_t nac=ch->code[ip+2]; ip+=3;
                printf(" native#%d (%d args)",nid,nac); break;}
            default: break;
        }
        printf("\n");
    }
    printf("+== end %s (%d bytes, %d constants)\n\n",name,ch->code_len,ch->const_count);
}
#undef RU16
