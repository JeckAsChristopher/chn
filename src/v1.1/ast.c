

#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ASTNode *node_alloc(NodeKind kind, int line){
    ASTNode *n=(ASTNode*)calloc(1,sizeof(ASTNode));
    if(!n){fprintf(stderr,"oom\n");exit(1);}
    n->kind=kind; n->line=line; n->col=0; n->tok_len=0;
    return n;
}

void ast_free(ASTNode *n){
    if(!n) return;
    switch(n->kind){
        case NODE_STRING:   free(n->str.value); break;
        case NODE_FSTRING:  free(n->fstring.raw_fmt); break;
        case NODE_ARRAY_LITERAL:
            for(int i=0;i<n->arr_lit.count;i++) ast_free(n->arr_lit.elements[i]);
            free(n->arr_lit.elements); break;
        case NODE_DICT_LITERAL:
            for(int i=0;i<n->dict_lit.count;i++){
                ast_free(n->dict_lit.keys[i]);
                ast_free(n->dict_lit.values[i]);
            }
            free(n->dict_lit.keys);
            free(n->dict_lit.values);
            break;
        case NODE_ASSIGN:   ast_free(n->assign.value); break;
        case NODE_COMPOUND_ASSIGN: ast_free(n->compound_assign.value); break;
        case NODE_BINARY:   ast_free(n->binary.left); ast_free(n->binary.right); break;
        case NODE_TERNARY:  ast_free(n->ternary.cond); ast_free(n->ternary.then_val); ast_free(n->ternary.else_val); break;
        case NODE_NULL_COAL: ast_free(n->null_coal.left); ast_free(n->null_coal.right); break;
        case NODE_TYPEOF:   ast_free(n->typeof_expr.operand); break;
        case NODE_UNARY:    ast_free(n->unary.operand); break;
        case NODE_CALL:
            for(int i=0;i<n->call.arg_count;i++) ast_free(n->call.args[i]);
            free(n->call.args); break;
        case NODE_METHOD_CALL:
            ast_free(n->method_call.object_expr);
            for(int i=0;i<n->method_call.arg_count;i++) ast_free(n->method_call.args[i]);
            free(n->method_call.args); break;
        case NODE_NATIVE_CALL:
            for(int i=0;i<n->native_call.argc;i++) ast_free(n->native_call.args[i]);
            free(n->native_call.args); break;
        case NODE_INDEX:    ast_free(n->index.object_expr); ast_free(n->index.index); break;
        case NODE_INDEX_SET: ast_free(n->index_set.object_expr); ast_free(n->index_set.index); ast_free(n->index_set.value); break;
        
        case NODE_NULL: break;
        case NODE_FIELD_ACCESS: ast_free(n->field_access.object_expr); break;
        case NODE_FIELD_SET:    ast_free(n->field_set.object_expr); ast_free(n->field_set.value); break;
        case NODE_AWAIT:        ast_free(n->await_expr.expr); break;
        case NODE_STRUCT_DECL:
            for(int i=0;i<n->struct_decl.field_count;i++) ast_free(n->struct_decl.defaults[i]);
            break;
        
        case NODE_POST_INC: case NODE_PRE_INC: break;
        case NODE_DO_WHILE:
            ast_free(n->do_while.body);
            ast_free(n->do_while.condition); break;
        
        case NODE_LAMBDA:
            ast_free(n->lambda.body); break;
        case NODE_CALL_EXPR:
            ast_free(n->call_expr.callee);
            for(int i=0;i<n->call_expr.arg_count;i++) ast_free(n->call_expr.args[i]);
            free(n->call_expr.args); break;
        case NODE_VAR_DECL: ast_free(n->var_decl.initializer); break;
        case NODE_ARRAY_DECL: ast_free(n->arr_decl.initializer); break;
        case NODE_DICT_DECL:  ast_free(n->dict_decl.initializer); break;
        case NODE_FUNC_DECL: ast_free(n->func_decl.body); break;
        case NODE_IF:
            ast_free(n->if_stmt.condition);
            ast_free(n->if_stmt.then_branch);
            ast_free(n->if_stmt.else_branch); break;
        case NODE_WHILE:
            ast_free(n->while_stmt.condition);
            ast_free(n->while_stmt.body); break;
        case NODE_FOR:
            ast_free(n->for_stmt.init); ast_free(n->for_stmt.condition);
            ast_free(n->for_stmt.post); ast_free(n->for_stmt.body); break;
        case NODE_FOREACH:
            ast_free(n->foreach_stmt.iterable);
            ast_free(n->foreach_stmt.body); break;
        case NODE_SWITCH:
            ast_free(n->switch_stmt.subject);
            for(int i=0;i<n->switch_stmt.case_count;i++){
                ast_free(n->switch_stmt.cases[i].value);
                ast_free(n->switch_stmt.cases[i].body);
            }
            free(n->switch_stmt.cases);
            ast_free(n->switch_stmt.default_body); break;
        case NODE_TRY_CATCH:
            ast_free(n->try_catch.try_body);
            ast_free(n->try_catch.catch_body); break;
        case NODE_THROW:    ast_free(n->throw_stmt.value); break;
        case NODE_RETURN:   ast_free(n->ret.value); break;
        case NODE_PRINT:
            for(int i=0;i<n->print.arg_count;i++) ast_free(n->print.args[i]);
            free(n->print.args); break;
        case NODE_INPUT:    ast_free(n->input.prompt_expr); break;
        case NODE_EXPORT:   ast_free(n->export_node.func_def); break;
        case NODE_EXPR_STMT: ast_free(n->expr_stmt.expr); break;
        case NODE_BLOCK:
            for(int i=0;i<n->block.count;i++) ast_free(n->block.stmts[i]);
            free(n->block.stmts); break;
        case NODE_PROGRAM:
            for(int i=0;i<n->program.count;i++) ast_free(n->program.stmts[i]);
            free(n->program.stmts); break;
        default: break;
    }
    free(n);
}

static void indent_print(int depth){
    for(int i=0;i<depth*2;i++) putchar(' ');
}

static const char *node_name(NodeKind k){
    switch(k){
        case NODE_PROGRAM:   return "Program";
        case NODE_BLOCK:     return "Block";
        case NODE_NUMBER:    return "Number";
        case NODE_STRING:    return "String";
        case NODE_FSTRING:   return "FString";
        case NODE_BOOL:      return "Bool";
        case NODE_NIL:       return "Nil";
        case NODE_ARRAY_LITERAL: return "ArrayLit";
        case NODE_DICT_LITERAL:  return "DictLit";
        case NODE_IDENT:     return "Ident";
        case NODE_ASSIGN:    return "Assign";
        case NODE_COMPOUND_ASSIGN: return "CompoundAssign";
        case NODE_BINARY:    return "Binary";
        case NODE_UNARY:     return "Unary";
        case NODE_TERNARY:   return "Ternary";
        case NODE_NULL_COAL: return "NullCoal";
        case NODE_TYPEOF:    return "Typeof";
        case NODE_CALL:      return "Call";
        case NODE_METHOD_CALL: return "MethodCall";
        case NODE_NATIVE_CALL: return "NativeCall";
        case NODE_INDEX:     return "Index";
        case NODE_INDEX_SET: return "IndexSet";
        case NODE_NULL:         return "Null";
        case NODE_FIELD_ACCESS: return "FieldAccess";
        case NODE_FIELD_SET:    return "FieldSet";
        case NODE_AWAIT:        return "Await";
        case NODE_STRUCT_DECL:  return "StructDecl";
        case NODE_POST_INC:     return "PostInc";
        case NODE_PRE_INC:      return "PreInc";
        case NODE_DO_WHILE:     return "DoWhile";
        case NODE_LAMBDA:       return "Lambda";
        case NODE_CALL_EXPR:    return "CallExpr";
        case NODE_VAR_DECL:  return "VarDecl";
        case NODE_ARRAY_DECL: return "ArrayDecl";
        case NODE_DICT_DECL: return "DictDecl";
        case NODE_FUNC_DECL: return "FuncDecl";
        case NODE_IF:        return "If";
        case NODE_WHILE:     return "While";
        case NODE_FOR:       return "For";
        case NODE_FOREACH:   return "ForEach";
        case NODE_SWITCH:    return "Switch";
        case NODE_RETURN:    return "Return";
        case NODE_BREAK:     return "Break";
        case NODE_CONTINUE:  return "Continue";
        case NODE_PRINT:     return "Print";
        case NODE_INPUT:     return "Input";
        case NODE_IMPORT:    return "Import";
        case NODE_EXPORT:    return "Export";
        case NODE_TRY_CATCH: return "TryCatch";
        case NODE_THROW:     return "Throw";
        case NODE_EXPR_STMT: return "ExprStmt";
        default:             return "?";
    }
}

void ast_print(ASTNode *n, int depth){
    if(!n){ indent_print(depth); printf("(nil)\n"); return; }
    indent_print(depth);
    printf("[%s:%d]", node_name(n->kind), n->line);
    switch(n->kind){
        case NODE_NUMBER:    printf(" %.14g", n->num.value); break;
        case NODE_STRING:    printf(" \"%s\"", n->str.value?n->str.value:""); break;
        case NODE_FSTRING:   printf(" f\"%s\"", n->fstring.raw_fmt?n->fstring.raw_fmt:""); break;
        case NODE_BOOL:      printf(" %s", n->boolean.value?"true":"false"); break;
        case NODE_IDENT:     printf(" %s", n->ident.name); break;
        case NODE_ASSIGN:    printf(" %s =", n->assign.name); break;
        case NODE_COMPOUND_ASSIGN: printf(" %s %s=", n->compound_assign.name, token_kind_name(n->compound_assign.op)); break;
        case NODE_BINARY:    printf(" op=%s", token_kind_name(n->binary.op)); break;
        case NODE_UNARY:     printf(" op=%s", token_kind_name(n->unary.op)); break;
        case NODE_CALL:      printf(" %s()", n->call.name); break;
        case NODE_METHOD_CALL: printf(" .%s()", n->method_call.method); break;
        case NODE_VAR_DECL:  printf(" %s%s", n->var_decl.is_let?"let ":"var ", n->var_decl.name); break;
        case NODE_FUNC_DECL: printf(" %s %s%s()", visibility_name(n->func_decl.visibility),
                                    n->func_decl.is_entry ? "entry " : "",
                                    n->func_decl.name); break;
        case NODE_FOREACH:   printf(" %s in", n->foreach_stmt.elem_name); break;
        case NODE_IMPORT:    printf(" '%s'%s%s%s", n->import.path,
                                n->import.is_lib?" [CCO]":"",
                                n->import.alias[0]?" as ":"",
                                n->import.alias[0]?n->import.alias:""); break;
        default: break;
    }
    printf("\n");
    
    switch(n->kind){
        case NODE_PROGRAM:
            for(int i=0;i<n->program.count;i++){ ast_print(n->program.stmts[i],depth+1); }
            break;
        case NODE_BLOCK:
            for(int i=0;i<n->block.count;i++){ ast_print(n->block.stmts[i],depth+1); }
            break;
        case NODE_BINARY:
            ast_print(n->binary.left,depth+1);
            ast_print(n->binary.right,depth+1); break;
        case NODE_TERNARY:
            ast_print(n->ternary.cond,depth+1);
            ast_print(n->ternary.then_val,depth+1);
            ast_print(n->ternary.else_val,depth+1); break;
        case NODE_NULL_COAL:
            ast_print(n->null_coal.left,depth+1);
            ast_print(n->null_coal.right,depth+1); break;
        case NODE_TYPEOF: ast_print(n->typeof_expr.operand,depth+1); break;
        case NODE_UNARY:  ast_print(n->unary.operand,depth+1); break;
        case NODE_ASSIGN: ast_print(n->assign.value,depth+1); break;
        case NODE_COMPOUND_ASSIGN: ast_print(n->compound_assign.value,depth+1); break;
        case NODE_IF:
            ast_print(n->if_stmt.condition,depth+1);
            ast_print(n->if_stmt.then_branch,depth+1);
            if(n->if_stmt.else_branch){ ast_print(n->if_stmt.else_branch,depth+1); }
            break;
        case NODE_WHILE:
            ast_print(n->while_stmt.condition,depth+1);
            ast_print(n->while_stmt.body,depth+1); break;
        case NODE_FOR:
            ast_print(n->for_stmt.init,depth+1);
            ast_print(n->for_stmt.condition,depth+1);
            ast_print(n->for_stmt.post,depth+1);
            ast_print(n->for_stmt.body,depth+1); break;
        case NODE_FOREACH:
            ast_print(n->foreach_stmt.iterable,depth+1);
            ast_print(n->foreach_stmt.body,depth+1); break;
        case NODE_TRY_CATCH:
            ast_print(n->try_catch.try_body,depth+1);
            ast_print(n->try_catch.catch_body,depth+1); break;
        case NODE_THROW: ast_print(n->throw_stmt.value,depth+1); break;
        case NODE_RETURN: if(n->ret.value) ast_print(n->ret.value,depth+1); break;
        case NODE_PRINT:
            for(int i=0;i<n->print.arg_count;i++){ ast_print(n->print.args[i],depth+1); }
            break;
        case NODE_FUNC_DECL: ast_print(n->func_decl.body,depth+1); break;
        case NODE_VAR_DECL: if(n->var_decl.initializer) ast_print(n->var_decl.initializer,depth+1); break;
        case NODE_EXPR_STMT: ast_print(n->expr_stmt.expr,depth+1); break;
        case NODE_CALL:
            for(int i=0;i<n->call.arg_count;i++){ ast_print(n->call.args[i],depth+1); }
            break;
        case NODE_METHOD_CALL:
            ast_print(n->method_call.object_expr,depth+1);
            for(int i=0;i<n->method_call.arg_count;i++){ ast_print(n->method_call.args[i],depth+1); }
            break;
        case NODE_INDEX: ast_print(n->index.object_expr,depth+1); ast_print(n->index.index,depth+1); break;
        
        case NODE_FIELD_ACCESS: ast_print(n->field_access.object_expr,depth+1); break;
        case NODE_FIELD_SET:    ast_print(n->field_set.object_expr,depth+1); ast_print(n->field_set.value,depth+1); break;
        case NODE_AWAIT:        ast_print(n->await_expr.expr,depth+1); break;
        case NODE_STRUCT_DECL:
            for(int i=0;i<n->struct_decl.field_count;i++) ast_print(n->struct_decl.defaults[i],depth+1);
            break;
        
        case NODE_DO_WHILE:
            ast_print(n->do_while.body,depth+1);
            ast_print(n->do_while.condition,depth+1); break;
        case NODE_LAMBDA:
            ast_print(n->lambda.body,depth+1); break;
        case NODE_CALL_EXPR:
            ast_print(n->call_expr.callee,depth+1);
            for(int i=0;i<n->call_expr.arg_count;i++) ast_print(n->call_expr.args[i],depth+1);
            break;
        case NODE_INDEX_SET:
            ast_print(n->index_set.object_expr,depth+1);
            ast_print(n->index_set.index,depth+1);
            ast_print(n->index_set.value,depth+1); break;
        case NODE_ARRAY_LITERAL:
            for(int i=0;i<n->arr_lit.count;i++){ ast_print(n->arr_lit.elements[i],depth+1); }
            break;
        case NODE_DICT_LITERAL:
            for(int i=0;i<n->dict_lit.count;i++){
                ast_print(n->dict_lit.keys[i],depth+1);
                ast_print(n->dict_lit.values[i],depth+1);
            } break;
        default: break;
    }
}
