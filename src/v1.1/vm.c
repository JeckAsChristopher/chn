

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "vm.h"
#include "gc.h"
#include "error.h"
#include "native.h"
#include <math.h>
#include <ctype.h>
#include <string.h>

#define FRAME        (vm->frames[vm->frame_count-1])
#define FIP          (FRAME.ip)
static inline uint8_t  _v21_read_byte(uint8_t **ip){ return *(*ip)++; }
static inline uint16_t _v21_read_u16 (uint8_t **ip){
    uint16_t lo=(uint16_t)(*ip)[0], hi=(uint16_t)(*ip)[1];
    *ip+=2; return lo|(hi<<8);
}
#define READ_BYTE()  _v21_read_byte(&FIP)
#define READ_U16()   _v21_read_u16(&FIP)

static inline void _v21_push(VM *vm, Value v){ vm->stack[vm->stack_top]=v; vm->stack_top++; }
#define PUSH(v) do{ \
    if(vm->stack_top>=MAX_STACK){RT_ERROR_STACK("stack overflow");} \
    _v21_push(vm,(v)); }while(0)
#define POP()       (vm->stack[--vm->stack_top])
#define PEEK(n)     (vm->stack[vm->stack_top-1-(n)])
#define TOP()        PEEK(0)

static int vm_line(VM *vm){
    if(vm->frame_count==0) return 0;
    Chunk *ch=FRAME.function?&FRAME.function->chunk:vm->top_chunk;
    int off=(int)(FRAME.ip-ch->code)-1;
    return chunk_line_at(ch,off<0?0:off);
}

#define RT_ERROR_IMPL_(errfn, fmt, ...) do { \
    if(vm->try_top>0){ \
        char _ebuf[512]; \
        snprintf(_ebuf,sizeof(_ebuf),fmt,##__VA_ARGS__); \
        vm_throw_error(vm,_ebuf); \
        if(vm->has_error) return VM_RUNTIME_ERROR; \
        goto dispatch_top; \
    } \
    errfn(vm_line(vm),fmt,##__VA_ARGS__); \
    return VM_RUNTIME_ERROR; \
}while(0)

#define RT_ERROR(fmt,...)       RT_ERROR_IMPL_(error_runtime,       fmt, ##__VA_ARGS__)
#define RT_ERROR_TYPE(fmt,...)  RT_ERROR_IMPL_(error_runtime_type,  fmt, ##__VA_ARGS__)
#define RT_ERROR_IDX(fmt,...)   RT_ERROR_IMPL_(error_runtime_index, fmt, ##__VA_ARGS__)
#define RT_ERROR_ARITH(fmt,...) RT_ERROR_IMPL_(error_runtime_arith, fmt, ##__VA_ARGS__)
#define RT_ERROR_STACK(fmt,...) RT_ERROR_IMPL_(error_runtime_stack, fmt, ##__VA_ARGS__)
#define RT_ERROR_MEM(fmt,...)   RT_ERROR_IMPL_(error_runtime_mem,   fmt, ##__VA_ARGS__)

static void vm_throw_error(VM *vm, const char *msg){
    if(vm->try_top<=0){ vm->has_error=true; return; }
    TryFrame *tf=&vm->try_stack[--vm->try_top];
    
    vm->stack_top=tf->stack_top;
    vm->frame_count=tf->frame_count;
    
    vm->error_value=STRING_VAL(gc_cstring(msg));
    vm->stack[vm->stack_top++]=vm->error_value;
    
    

    vm->frames[vm->frame_count-1].ip=tf->handler_ip;
    vm->has_error=false;
}

static const char *vtype(Value v){
    switch(v.type){
        case VAL_NUMBER:   return "number";
        case VAL_STRING:   return "string";
        case VAL_BOOL:     return "bool";
        case VAL_NIL:      return "nil";
        case VAL_FUNCTION: return "function";
        case VAL_ARRAY:    return "array";
        case VAL_DICT:     return "dict";
        default:           return "?";
    }
}

static bool val_eq(Value a, Value b){
    if(a.type!=b.type) return false;
    switch(a.type){
        case VAL_NUMBER:   return a.as.number==b.as.number;
        case VAL_BOOL:     return a.as.boolean==b.as.boolean;
        case VAL_NIL:      return true;
        
        case VAL_STRING:   return a.as.string==b.as.string ||
                               strcmp(a.as.string->chars,b.as.string->chars)==0;
        case VAL_ARRAY:    return a.as.array==b.as.array;
        case VAL_DICT:     return a.as.dict==b.as.dict;
        default:           return a.as.function==b.as.function;
    }
}

static bool is_truthy(Value v){
    switch(v.type){
        case VAL_NIL:    return false;
        case VAL_BOOL:   return v.as.boolean;
        case VAL_NUMBER: return v.as.number!=0.0;
        case VAL_STRING: return v.as.string->len>0;
        case VAL_ARRAY:  return v.as.array->len>0;
        case VAL_DICT:   return v.as.dict->count>0;
        default:         return true;
    }
}

static void print_value(Value v){
    switch(v.type){
        case VAL_NUMBER:
            if(v.as.number==(long long)v.as.number) printf("%lld",(long long)v.as.number);
            else                                     printf("%.14g",v.as.number);
            break;
        case VAL_STRING:   printf("%s",v.as.string->chars); break;
        case VAL_BOOL:     printf("%s",v.as.boolean?"true":"false"); break;
        case VAL_NIL:      printf("nil"); break;
        case VAL_FUNCTION: printf("<func %s>",v.as.function->name); break;
        case VAL_ARRAY:{
            ObjArray *a=v.as.array;
            printf("[");
            for(int i=0;i<a->len;i++){ if(i) printf(", "); print_value(a->items[i]); }
            printf("]"); break;
        }
        case VAL_DICT:{
            ObjDict *d=v.as.dict;
            printf("{");
            bool first=true;
            for(int i=0;i<d->cap;i++){
                if(!d->entries[i].key) continue;
                if(!first) printf(", ");
                printf("\"%s\": ",d->entries[i].key->chars);
                print_value(d->entries[i].value);
                first=false;
            }
            printf("}"); break;
        }
    }
}

static void append_val_buf(Value v, char *buf, int *pos, int cap){
    int rem=cap-*pos-1;
    if(rem<=0) return;
    switch(v.type){
        case VAL_STRING:{
            int n=v.as.string->len<rem?v.as.string->len:rem;
            memcpy(buf+*pos,v.as.string->chars,(size_t)n);
            *pos+=n; break;
        }
        case VAL_NUMBER:
            if(v.as.number==(long long)v.as.number)
                *pos+=snprintf(buf+*pos,(size_t)(rem+1),"%lld",(long long)v.as.number);
            else
                *pos+=snprintf(buf+*pos,(size_t)(rem+1),"%.14g",v.as.number);
            break;
        case VAL_BOOL:
            *pos+=snprintf(buf+*pos,(size_t)(rem+1),"%s",v.as.boolean?"true":"false"); break;
        case VAL_NIL:
            *pos+=snprintf(buf+*pos,(size_t)(rem+1),"nil"); break;
        case VAL_ARRAY:{
            ObjArray *a=v.as.array;
            if(*pos<cap-1) buf[(*pos)++]='[';
            for(int i=0;i<a->len;i++){
                if(i&&*pos<cap-2){buf[(*pos)++]=',';buf[(*pos)++]=' ';}
                append_val_buf(a->items[i],buf,pos,cap);
            }
            if(*pos<cap-1) buf[(*pos)++]=']';
            break;
        }
        default:
            *pos+=snprintf(buf+*pos,(size_t)(rem+1),"<%s>",vtype(v)); break;
    }
    buf[*pos<cap?*pos:cap-1]='\0';
}

static ObjString *val_to_str(Value v){
    if(v.type==VAL_STRING) return v.as.string;
    char buf[65536]; int pos=0;
    append_val_buf(v,buf,&pos,(int)sizeof(buf));
    buf[pos]='\0';
    return gc_cstring(buf);
}

static void arr_grow(ObjArray *a){
    int nc=a->cap<8?8:a->cap*2;
    a->items=(Value*)GC_GROW(a->items,(size_t)a->cap*sizeof(Value),(size_t)nc*sizeof(Value));
    a->cap=nc;
}
static void arr_push(ObjArray *a, Value v){
    if(a->len>=a->cap) arr_grow(a);
    a->items[a->len++]=v;
}
static void arr_insert(ObjArray *a, int i, Value v){
    
    if(i<0) i=0;
    if(i>a->len) i=a->len;
    if(a->len>=a->cap) arr_grow(a);
    if(a->items && a->len>i)
        memmove(&a->items[i+1],&a->items[i],(size_t)(a->len-i)*sizeof(Value));
    a->items[i]=v; a->len++;
}
static void arr_cut(ObjArray *a, int i){
    
    if(!a->items || a->len==0 || i<0 || i>=a->len) return;
    memmove(&a->items[i],&a->items[i+1],(size_t)(a->len-i-1)*sizeof(Value));
    a->len--;
}
static void arr_remove(ObjArray *a, Value v){
    if(!a->items) return;
    for(int i=0;i<a->len;i++) if(val_eq(a->items[i],v)){ arr_cut(a,i); return; }
}
static void arr_rall(ObjArray *a, Value v){
    if(!a->items) return;
    int w=0;
    for(int r=0;r<a->len;r++) if(!val_eq(a->items[r],v)) a->items[w++]=a->items[r];
    a->len=w;
}
static int val_cmp(const void *ap, const void *bp){
    Value a=*(Value*)ap, b=*(Value*)bp;
    if(a.type==VAL_NUMBER&&b.type==VAL_NUMBER){
        double d=a.as.number-b.as.number;
        return d<0?-1:d>0?1:0;
    }
    if(a.type==VAL_STRING&&b.type==VAL_STRING)
        return strcmp(a.as.string->chars,b.as.string->chars);
    return 0;
}

static ObjString *str_upper(const char *s, int len){
    char *buf=(char*)malloc((size_t)len+1);
    for(int i=0;i<len;i++) buf[i]=(char)toupper((unsigned char)s[i]);
    buf[len]='\0';
    return gc_string_own(buf,len);
}
static ObjString *str_lower(const char *s, int len){
    char *buf=(char*)malloc((size_t)len+1);
    for(int i=0;i<len;i++) buf[i]=(char)tolower((unsigned char)s[i]);
    buf[len]='\0';
    return gc_string_own(buf,len);
}
static ObjString *str_trim(const char *s, int len){
    const char *start=s, *end=s+len-1;
    while(start<=end&&isspace((unsigned char)*start)) start++;
    while(end>=start&&isspace((unsigned char)*end)) end--;
    int nlen=(int)(end-start+1);
    if(nlen<0) nlen=0;
    return gc_string(start,nlen);
}
static ObjString *str_reverse(const char *s, int len){
    char *buf=(char*)malloc((size_t)len+1);
    for(int i=0;i<len;i++) buf[i]=s[len-1-i];
    buf[len]='\0';
    return gc_string_own(buf,len);
}
static ObjArray *str_split(const char *s, int slen, const char *delim, int dlen){
    ObjArray *arr=gc_array();
    if(dlen==0){
        
        for(int i=0;i<slen;i++){
            ObjString *ch=gc_string(s+i,1);
            arr_push(arr,STRING_VAL(ch));
        }
        return arr;
    }
    const char *p=s, *end=s+slen;
    while(p<=end){
        const char *found=(p<end)?(const char*)memmem(p,(size_t)(end-p),delim,(size_t)dlen):NULL;
        const char *seg_end=found?found:end;
        ObjString *seg=gc_string(p,(int)(seg_end-p));
        arr_push(arr,STRING_VAL(seg));
        if(!found) break;
        p=found+dlen;
    }
    return arr;
}
static ObjString *str_replace(const char *s, int slen, const char *from, int flen, const char *to, int tlen){
    if(!s||!from||!to||slen<0||flen<=0||tlen<0) return gc_string(s?s:"", s?slen:0);
    if(flen<=0||slen<0||tlen<0) return gc_string(s,slen<0?0:slen);
    const char *end=s+slen;
    
    int count=0;
    const char *p=s;
    while(p<end){
        const char *f=(const char*)memmem(p,(size_t)(end-p),from,(size_t)flen);
        if(!f) break;
        count++;
        p=f+flen;
    }
    
    long newlen=(long)slen + (long)count*((long)tlen-(long)flen);
    if(newlen<0) newlen=0;
    char *buf=(char*)malloc((size_t)newlen+1);
    if(!buf) return gc_string(s,slen); 
    char *out=buf;
    p=s;
    while(p<end){
        const char *f=(const char*)memmem(p,(size_t)(end-p),from,(size_t)flen);
        if(!f){ memcpy(out,p,(size_t)(end-p)); out+=(end-p); break; }
        memcpy(out,p,(size_t)(f-p)); out+=f-p;
        memcpy(out,to,(size_t)tlen); out+=tlen;
        p=f+flen;
    }
    *out='\0';
    return gc_string_own(buf,(int)newlen);
}

static inline Value get_local(VM *vm, int slot){ return vm->stack[FRAME.base_idx+slot]; }
static inline void  set_local(VM *vm, int slot, Value v){ vm->stack[FRAME.base_idx+slot]=v; }

void vm_init(VM *vm){
    memset(vm,0,sizeof(VM));
    for(int i=0;i<MAX_VARIABLES;i++) vm->globals[i]=NIL_VAL;
    vm->error_value=NIL_VAL;
    gc_init();
}

void vm_init_no_gc(VM *vm){
    memset(vm,0,sizeof(VM));
    for(int i=0;i<MAX_VARIABLES;i++) vm->globals[i]=NIL_VAL;
    vm->error_value=NIL_VAL;
    
}
void vm_free(VM *vm){ (void)vm; gc_free_all(); }

VMResult vm_run(VM *vm, Chunk *top_chunk){
    vm->top_chunk=top_chunk;
    vm->frame_count=1;
    vm->frames[0].function=NULL;
    vm->frames[0].ip=top_chunk->code;
    vm->frames[0].base_idx=0;

    
#ifdef __GNUC__
    static const void *dt[] = {
        [OP_CONST]         =&&lbl_OP_CONST,
        [OP_NIL]           =&&lbl_OP_NIL,
        [OP_TRUE]          =&&lbl_OP_TRUE,
        [OP_FALSE]         =&&lbl_OP_FALSE,
        [OP_POP]           =&&lbl_OP_POP,
        [OP_DUP]           =&&lbl_OP_DUP,
        [OP_GET_VAR]       =&&lbl_OP_GET_VAR,
        [OP_SET_VAR]       =&&lbl_OP_SET_VAR,
        [OP_DEF_VAR]       =&&lbl_OP_DEF_VAR,
        [OP_GET_LOCAL]     =&&lbl_OP_GET_LOCAL,
        [OP_SET_LOCAL]     =&&lbl_OP_SET_LOCAL,
        [OP_ADD]           =&&lbl_OP_ADD,
        [OP_SUB]           =&&lbl_OP_SUB,
        [OP_MUL]           =&&lbl_OP_MUL,
        [OP_DIV]           =&&lbl_OP_DIV,
        [OP_MOD]           =&&lbl_OP_MOD,
        [OP_NEG]           =&&lbl_OP_NEG,
        [OP_POW]           =&&lbl_OP_POW,
        [OP_BAND]          =&&lbl_OP_BAND,
        [OP_BOR]           =&&lbl_OP_BOR,
        [OP_BXOR]          =&&lbl_OP_BXOR,
        [OP_BNOT]          =&&lbl_OP_BNOT,
        [OP_LSHIFT]        =&&lbl_OP_LSHIFT,
        [OP_RSHIFT]        =&&lbl_OP_RSHIFT,
        [OP_EQ]            =&&lbl_OP_EQ,
        [OP_NEQ]           =&&lbl_OP_NEQ,
        [OP_LT]            =&&lbl_OP_LT,
        [OP_GT]            =&&lbl_OP_GT,
        [OP_LE]            =&&lbl_OP_LE,
        [OP_GE]            =&&lbl_OP_GE,
        [OP_NOT]           =&&lbl_OP_NOT,
        [OP_JUMP]          =&&lbl_OP_JUMP,
        [OP_JUMP_IF_FALSE] =&&lbl_OP_JUMP_IF_FALSE,
        [OP_JUMP_IF_TRUE]  =&&lbl_OP_JUMP_IF_TRUE,
        [OP_JUMP_IF_NIL]   =&&lbl_OP_JUMP_IF_NIL,
        [OP_CALL]          =&&lbl_OP_CALL,
        [OP_RETURN]        =&&lbl_OP_RETURN,
        [OP_ARRAY_NEW]     =&&lbl_OP_ARRAY_NEW,
        [OP_ARRAY_PUSH]    =&&lbl_OP_ARRAY_PUSH,
        [OP_ARRAY_INDEX]   =&&lbl_OP_ARRAY_INDEX,
        [OP_ARRAY_SET]     =&&lbl_OP_ARRAY_SET,
        [OP_ARRAY_LEN]     =&&lbl_OP_ARRAY_LEN,
        [OP_METHOD_CALL]   =&&lbl_OP_METHOD_CALL,
        [OP_DICT_NEW]      =&&lbl_OP_DICT_NEW,
        [OP_DICT_SET]      =&&lbl_OP_DICT_SET,
        [OP_DICT_GET]      =&&lbl_OP_DICT_GET,
        [OP_FOREACH_INIT]  =&&lbl_OP_FOREACH_INIT,
        [OP_FOREACH_STEP]  =&&lbl_OP_FOREACH_STEP,
        [OP_PUSH_HANDLER]  =&&lbl_OP_PUSH_HANDLER,
        [OP_POP_HANDLER]   =&&lbl_OP_POP_HANDLER,
        [OP_THROW]         =&&lbl_OP_THROW,
        [OP_GET_ERROR]     =&&lbl_OP_GET_ERROR,
        [OP_TYPEOF]        =&&lbl_OP_TYPEOF,
        [OP_GC_SAFEPOINT]  =&&lbl_OP_GC_SAFEPOINT,
        [OP_PRINT]         =&&lbl_OP_PRINT,
        [OP_PROMPT]        =&&lbl_OP_PROMPT,
        [OP_INPUT]         =&&lbl_OP_INPUT,
        [OP_NATIVE]        =&&lbl_OP_NATIVE,
        [OP_CONST_0]       =&&lbl_OP_CONST_0,
        [OP_CONST_1]       =&&lbl_OP_CONST_1,
        [OP_INC_LOCAL]     =&&lbl_OP_INC_LOCAL,
        [OP_DEC_LOCAL]     =&&lbl_OP_DEC_LOCAL,
        [OP_CALL_TAIL]     =&&lbl_OP_CALL_TAIL,
        [OP_AWAIT]         =&&lbl_OP_AWAIT,
        [OP_IN]            =&&lbl_OP_IN,
        [OP_HALT]          =&&lbl_OP_HALT,
    };
#   define DISPATCH()  goto *dt[READ_BYTE()]
#   define CASE(op)    lbl_##op
#else
#   define DISPATCH()  goto dispatch_switch
#   define CASE(op)    case op
#endif

dispatch_top:
#ifdef __GNUC__
    DISPATCH();
#else
dispatch_switch:;
    uint8_t _op=READ_BYTE();
    switch((OpCode)_op){
#endif

    CASE(OP_CONST):{
        uint16_t idx=READ_U16();
        Chunk *ch=FRAME.function?&FRAME.function->chunk:vm->top_chunk;
        PUSH(ch->constants[idx]); DISPATCH();
    }
    CASE(OP_NIL):   PUSH(NIL_VAL);        DISPATCH();
    CASE(OP_TRUE):  PUSH(BOOL_VAL(true)); DISPATCH();
    CASE(OP_FALSE): PUSH(BOOL_VAL(false));DISPATCH();
    CASE(OP_POP):
        if(vm->stack_top==0) RT_ERROR("internal: POP on empty value stack");
        (void)POP(); DISPATCH();
    CASE(OP_DUP):   PUSH(TOP());           DISPATCH();

    CASE(OP_GET_VAR):{ uint16_t i=READ_U16(); PUSH(vm->globals[i]); DISPATCH(); }
    CASE(OP_DEF_VAR):{ uint16_t i=READ_U16(); vm->globals[i]=POP(); DISPATCH(); }
    CASE(OP_SET_VAR):{ uint16_t i=READ_U16(); vm->globals[i]=TOP(); DISPATCH(); }
    CASE(OP_GET_LOCAL):{ uint16_t s=READ_U16(); PUSH(get_local(vm,(int)s)); DISPATCH(); }
    CASE(OP_SET_LOCAL):{ uint16_t s=READ_U16(); set_local(vm,(int)s,TOP());  DISPATCH(); }

    
    CASE(OP_ADD):{
        if(vm->stack_top < 2)
            RT_ERROR("internal: '+' requires 2 values on the stack, only %d present",
                     vm->stack_top);
        Value b=POP(),a=POP();
        if(IS_STRING(a)||IS_STRING(b)){
            ObjString *sa=val_to_str(a),*sb=val_to_str(b);
            int la=sa->len,lb=sb->len;
            char *tmp=(char*)malloc((size_t)(la+lb+1));
            if(!tmp) RT_ERROR_MEM("OOM in string concatenation");
            memcpy(tmp,sa->chars,(size_t)la);
            memcpy(tmp+la,sb->chars,(size_t)lb);
            tmp[la+lb]='\0';
            ObjString *res=gc_string(tmp,la+lb);
            free(tmp);
            PUSH(STRING_VAL(res));
        } else if(IS_NUMBER(a)&&IS_NUMBER(b)){
            PUSH(NUMBER_VAL(AS_NUMBER(a)+AS_NUMBER(b)));
        } else {
            RT_ERROR_TYPE("'+' requires numbers or strings, got %s and %s",vtype(a),vtype(b));
        }
        DISPATCH();
    }
    CASE(OP_SUB):{ Value b=POP(),a=POP();
        if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR_TYPE("'-' requires numbers");
        PUSH(NUMBER_VAL(AS_NUMBER(a)-AS_NUMBER(b))); DISPATCH(); }
    CASE(OP_MUL):{ Value b=POP(),a=POP();
        if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR_TYPE("'*' requires numbers");
        PUSH(NUMBER_VAL(AS_NUMBER(a)*AS_NUMBER(b))); DISPATCH(); }
    CASE(OP_DIV):{ Value b=POP(),a=POP();
        if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR_TYPE("'/' requires numbers");
        if(AS_NUMBER(b)==0.0) RT_ERROR_ARITH("division by zero");
        PUSH(NUMBER_VAL(AS_NUMBER(a)/AS_NUMBER(b))); DISPATCH(); }
    CASE(OP_MOD):{ Value b=POP(),a=POP();
        if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR_TYPE("'%%' requires numbers");
        if(AS_NUMBER(b)==0.0) RT_ERROR_ARITH("modulo by zero");
        PUSH(NUMBER_VAL(fmod(AS_NUMBER(a),AS_NUMBER(b)))); DISPATCH(); }
    CASE(OP_NEG):{ Value a=POP();
        if(!IS_NUMBER(a)) RT_ERROR_TYPE("unary '-' requires a number");
        PUSH(NUMBER_VAL(-AS_NUMBER(a))); DISPATCH(); }
    CASE(OP_POW):{ Value b=POP(),a=POP();
        if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR_TYPE("'**' requires numbers");
        PUSH(NUMBER_VAL(pow(AS_NUMBER(a),AS_NUMBER(b)))); DISPATCH(); }

    
    CASE(OP_BAND):{ Value b=POP(),a=POP();
        if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR_TYPE("'&' requires numbers");
        PUSH(NUMBER_VAL((double)((long long)AS_NUMBER(a)&(long long)AS_NUMBER(b)))); DISPATCH(); }
    CASE(OP_BOR):{ Value b=POP(),a=POP();
        if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR_TYPE("'|' requires numbers");
        PUSH(NUMBER_VAL((double)((long long)AS_NUMBER(a)|(long long)AS_NUMBER(b)))); DISPATCH(); }
    CASE(OP_BXOR):{ Value b=POP(),a=POP();
        if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR_TYPE("'^' requires numbers");
        PUSH(NUMBER_VAL((double)((long long)AS_NUMBER(a)^(long long)AS_NUMBER(b)))); DISPATCH(); }
    CASE(OP_BNOT):{ Value a=POP();
        if(!IS_NUMBER(a)) RT_ERROR_TYPE("'~' requires a number");
        PUSH(NUMBER_VAL((double)(~(long long)AS_NUMBER(a)))); DISPATCH(); }
    CASE(OP_LSHIFT):{ Value b=POP(),a=POP();
        if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR_TYPE("'<<' requires numbers");
        PUSH(NUMBER_VAL((double)((long long)AS_NUMBER(a)<<(int)AS_NUMBER(b)))); DISPATCH(); }
    CASE(OP_RSHIFT):{ Value b=POP(),a=POP();
        if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR_TYPE("'>>' requires numbers");
        PUSH(NUMBER_VAL((double)((long long)AS_NUMBER(a)>>(int)AS_NUMBER(b)))); DISPATCH(); }

    
    CASE(OP_EQ): { Value b=POP(),a=POP(); PUSH(BOOL_VAL(val_eq(a,b)));  DISPATCH(); }
    CASE(OP_NEQ):{ Value b=POP(),a=POP(); PUSH(BOOL_VAL(!val_eq(a,b))); DISPATCH(); }
    CASE(OP_LT): { Value b=POP(),a=POP();
        if(IS_NUMBER(a)&&IS_NUMBER(b)){ PUSH(BOOL_VAL(AS_NUMBER(a)<AS_NUMBER(b))); }
        else if(IS_STRING(a)&&IS_STRING(b)){ PUSH(BOOL_VAL(strcmp(a.as.string->chars,b.as.string->chars)<0)); }
        else RT_ERROR_TYPE("'<' requires numbers or strings");
        DISPATCH(); }
    CASE(OP_GT): { Value b=POP(),a=POP();
        if(IS_NUMBER(a)&&IS_NUMBER(b)){ PUSH(BOOL_VAL(AS_NUMBER(a)>AS_NUMBER(b))); }
        else if(IS_STRING(a)&&IS_STRING(b)){ PUSH(BOOL_VAL(strcmp(a.as.string->chars,b.as.string->chars)>0)); }
        else RT_ERROR_TYPE("'>' requires numbers or strings");
        DISPATCH(); }
    CASE(OP_LE): { Value b=POP(),a=POP();
        if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR_TYPE("'<=' requires numbers");
        PUSH(BOOL_VAL(AS_NUMBER(a)<=AS_NUMBER(b))); DISPATCH(); }
    CASE(OP_GE): { Value b=POP(),a=POP();
        if(!IS_NUMBER(a)||!IS_NUMBER(b)) RT_ERROR_TYPE("'>=' requires numbers");
        PUSH(BOOL_VAL(AS_NUMBER(a)>=AS_NUMBER(b))); DISPATCH(); }
    CASE(OP_NOT):{ Value a=POP(); PUSH(BOOL_VAL(!is_truthy(a))); DISPATCH(); }

    
    CASE(OP_JUMP):{
        uint16_t t=READ_U16();
        Chunk *ch=FRAME.function?&FRAME.function->chunk:vm->top_chunk;
        FRAME.ip=ch->code+t; DISPATCH(); }
    CASE(OP_JUMP_IF_FALSE):{
        uint16_t t=READ_U16();
        if(!is_truthy(TOP())){
            Chunk *ch=FRAME.function?&FRAME.function->chunk:vm->top_chunk;
            FRAME.ip=ch->code+t;
        }
        DISPATCH(); }
    CASE(OP_JUMP_IF_TRUE):{
        uint16_t t=READ_U16();
        if(is_truthy(TOP())){
            Chunk *ch=FRAME.function?&FRAME.function->chunk:vm->top_chunk;
            FRAME.ip=ch->code+t;
        }
        DISPATCH(); }
    CASE(OP_JUMP_IF_NIL):{
        uint16_t t=READ_U16();
        if(IS_NIL(TOP())){
            Chunk *ch=FRAME.function?&FRAME.function->chunk:vm->top_chunk;
            FRAME.ip=ch->code+t;
        }
        DISPATCH(); }

    
    CASE(OP_CALL):{
        uint16_t argc=READ_U16();
        if((int)argc+1 > vm->stack_top)
            RT_ERROR("internal: call with %u argument(s) but stack only has %d value(s)",
                     (unsigned)argc, vm->stack_top);
        Value fv=vm->stack[vm->stack_top-argc-1];
        if(!IS_FUNCTION(fv)) RT_ERROR_TYPE("attempt to call a %s (not a function)",vtype(fv));
        FunctionObject *f=AS_FUNCTION(fv);
        if((int)argc!=f->arity)
            RT_ERROR_TYPE("'%s' expects %d arg(s) but got %d",f->name,f->arity,(int)argc);
        if(vm->frame_count>=MAX_CALL_DEPTH) RT_ERROR_STACK("call stack overflow");
        int base=vm->stack_top-argc;
        CallFrame *fr=&vm->frames[vm->frame_count++];
        fr->function=f; fr->ip=f->chunk.code; fr->base_idx=base;
        DISPATCH(); }

    CASE(OP_RETURN):{
        if(vm->stack_top == 0)
            RT_ERROR("internal: 'return' with an empty stack");
        Value ret=POP();
        int base=FRAME.base_idx;
        vm->frame_count--;
        while(vm->stack_top>base) (void)POP();
        if(base>0) (void)POP(); 
        PUSH(ret);
        if(vm->frame_count==0) return VM_OK;
        DISPATCH(); }

    
    CASE(OP_ARRAY_NEW):{ ObjArray *a=gc_array(); PUSH(ARRAY_VAL(a)); DISPATCH(); }
    CASE(OP_ARRAY_PUSH):{
        Value val=POP();
        if(!IS_ARRAY(TOP())) RT_ERROR_TYPE("ARRAY_PUSH on non-array");
        arr_push(AS_ARRAY(TOP()),val); DISPATCH(); }
    CASE(OP_ARRAY_INDEX):{
        Value iv=POP(),av=POP();
        if(IS_NIL(av)) RT_ERROR_TYPE("Attempted to index nil value.");
        if(IS_DICT(av)){
            if(!IS_STRING(iv)) RT_ERROR_TYPE("dict key must be a string, got %s",vtype(iv));
            PUSH(gc_dict_get(AS_DICT(av),AS_STRING(iv))); DISPATCH();
        }
        if(IS_STRING(av)){
            if(!IS_NUMBER(iv)) RT_ERROR_TYPE("string index must be a number, got %s",vtype(iv));
            const char *s=AS_STRING(av)->chars;
            int slen=AS_STRING(av)->len;
            int idx=(int)AS_NUMBER(iv);
            if(idx<0) idx=slen+idx;
            if(idx<0||idx>=slen) RT_ERROR_IDX("string index %d out of bounds (len %d)",idx,slen);
            char buf[2]={s[idx],'\0'};
            PUSH(STRING_VAL(gc_cstring(buf))); DISPATCH();
        }
        if(!IS_ARRAY(av)) RT_ERROR_TYPE("cannot index a %s",vtype(av));
        if(!IS_NUMBER(iv)) RT_ERROR_TYPE("array index must be a number, got %s",vtype(iv));
        ObjArray *a=AS_ARRAY(av);
        
        if(!a->items || a->len==0) RT_ERROR_IDX("index %d out of bounds (array is empty)",(int)AS_NUMBER(iv));
        int idx=(int)AS_NUMBER(iv);
        if(idx<0) idx=a->len+idx;
        if(idx<0||idx>=a->len)
            RT_ERROR_IDX("index %d out of bounds (array length %d)",(int)AS_NUMBER(iv),a->len);
        PUSH(a->items[idx]); DISPATCH(); }

    CASE(OP_ARRAY_SET):{
        Value val=POP(),iv=POP(),av=POP();
        if(IS_NIL(av)) RT_ERROR_TYPE("Attempted to index nil value.");
        if(IS_DICT(av)){
            if(!IS_STRING(iv)) RT_ERROR_TYPE("dict key must be a string, got %s",vtype(iv));
            gc_dict_set(AS_DICT(av),AS_STRING(iv),val);
            PUSH(val); DISPATCH();
        }
        if(!IS_ARRAY(av)) RT_ERROR_TYPE("cannot index-assign a %s",vtype(av));
        if(!IS_NUMBER(iv)) RT_ERROR_TYPE("array index must be a number");
        ObjArray *a=AS_ARRAY(av);
        
        if(!a->items || a->len==0) RT_ERROR_IDX("index %d out of bounds (array is empty)",(int)AS_NUMBER(iv));
        int idx=(int)AS_NUMBER(iv);
        if(idx<0) idx=a->len+idx;
        if(idx<0||idx>=a->len)
            RT_ERROR_IDX("index %d out of bounds (array length %d)",(int)AS_NUMBER(iv),a->len);
        GC_WRITE_BARRIER(&a->header,val);
        a->items[idx]=val;
        PUSH(val); DISPATCH(); }

    CASE(OP_ARRAY_LEN):{
        Value v=POP();
        if(IS_NIL(v))         RT_ERROR_TYPE("Attempted to index nil value.");
        if(IS_ARRAY(v))       PUSH(NUMBER_VAL(AS_ARRAY(v)->len));
        else if(IS_STRING(v)) PUSH(NUMBER_VAL(AS_STRING(v)->len));
        else if(IS_DICT(v))   PUSH(NUMBER_VAL(AS_DICT(v)->count));
        else RT_ERROR_TYPE(".length() requires array, string, or dict, got %s",vtype(v));
        DISPATCH(); }

    CASE(OP_METHOD_CALL):{
        uint8_t mid=READ_BYTE(), argc=READ_BYTE();
        if((int)argc+1 > vm->stack_top)
            RT_ERROR("internal: method call with %u argument(s) but stack only has %d value(s)",
                     (unsigned)argc, vm->stack_top);
        Value av=vm->stack[vm->stack_top-argc-1];

        if(IS_NIL(av)) RT_ERROR_TYPE("Attempted to index nil value.");

        
        if(mid==METHOD_LENGTH){
            int len=0;
            if(IS_ARRAY(av))       len=AS_ARRAY(av)->len;
            else if(IS_STRING(av)) len=AS_STRING(av)->len;
            else if(IS_DICT(av))   len=AS_DICT(av)->count;
            else RT_ERROR_TYPE("method 'length' on %s",vtype(av));
            vm->stack_top-=(int)argc+1;
            PUSH(NUMBER_VAL(len)); DISPATCH();
        }

        
        if(IS_STRING(av)){
            ObjString *s=AS_STRING(av);
            Value *args=&vm->stack[vm->stack_top-argc];
            Value result=NIL_VAL;
            switch((ArrayMethod)mid){
                case METHOD_UPPER:
                    result=STRING_VAL(str_upper(s->chars,s->len)); break;
                case METHOD_LOWER:
                    result=STRING_VAL(str_lower(s->chars,s->len)); break;
                case METHOD_TRIM:
                    result=STRING_VAL(str_trim(s->chars,s->len)); break;
                case METHOD_STR_REVERSE:
                case METHOD_REVERSE_ARR:
                    result=STRING_VAL(str_reverse(s->chars,s->len)); break;
                case METHOD_SPLIT:{
                    const char *delim="";int dlen=0;
                    if(argc>=1&&IS_STRING(args[0])){ delim=AS_CSTR(args[0]); dlen=AS_STRING(args[0])->len; }
                    result=ARRAY_VAL(str_split(s->chars,s->len,delim,dlen)); break;
                }
                case METHOD_CONTAINS:{
                    if(argc<1||!IS_STRING(args[0])) RT_ERROR_TYPE("contains: expects string arg");
                    const char *needle=AS_CSTR(args[0]);
                    result=BOOL_VAL(memmem(s->chars,(size_t)s->len,needle,(size_t)AS_STRING(args[0])->len)!=NULL); break;
                }
                case METHOD_STARTS_WITH:{
                    if(argc<1||!IS_STRING(args[0])) RT_ERROR_TYPE("starts_with: expects string arg");
                    int pl=AS_STRING(args[0])->len;
                    result=BOOL_VAL(s->len>=pl&&memcmp(s->chars,AS_CSTR(args[0]),(size_t)pl)==0); break;
                }
                case METHOD_ENDS_WITH:{
                    if(argc<1||!IS_STRING(args[0])) RT_ERROR_TYPE("ends_with: expects string arg");
                    int pl=AS_STRING(args[0])->len;
                    result=BOOL_VAL(s->len>=pl&&memcmp(s->chars+s->len-pl,AS_CSTR(args[0]),(size_t)pl)==0); break;
                }
                case METHOD_REPLACE:{
                    if(argc<2||!IS_STRING(args[0])||!IS_STRING(args[1])) RT_ERROR_TYPE("replace: expects (from, to)");
                    result=STRING_VAL(str_replace(s->chars,s->len,
                        AS_CSTR(args[0]),AS_STRING(args[0])->len,
                        AS_CSTR(args[1]),AS_STRING(args[1])->len)); break;
                }
                case METHOD_FIND:{
                    if(argc<1||!IS_STRING(args[0])) RT_ERROR_TYPE("find: expects string");
                    const char *found=(const char*)memmem(s->chars,(size_t)s->len,
                        AS_CSTR(args[0]),(size_t)AS_STRING(args[0])->len);
                    result=found?NUMBER_VAL((double)(found-s->chars)):NUMBER_VAL(-1); break;
                }
                case METHOD_SLICE:{
                    int start=argc>=1&&IS_NUMBER(args[0])?(int)AS_NUMBER(args[0]):0;
                    int stop =argc>=2&&IS_NUMBER(args[1])?(int)AS_NUMBER(args[1]):s->len;
                    if(start<0) start=s->len+start;
                    if(stop<0)  stop=s->len+stop;
                    if(start<0) start=0;
                    if(stop>s->len) stop=s->len;
                    if(start>=stop){ result=STRING_VAL(gc_cstring("")); }
                    else           { result=STRING_VAL(gc_string(s->chars+start,stop-start)); }
                    break;
                }
                case METHOD_TO_NUM:{
                    char *end; double n=strtod(s->chars,&end);
                    result=(*end=='\0')?NUMBER_VAL(n):NIL_VAL; break;
                }
                case METHOD_COUNT_VAL:
                case METHOD_COUNT_STR:{
                    if(argc<1||!IS_STRING(args[0])){ result=NUMBER_VAL(0); break; }
                    const char *needle=AS_CSTR(args[0]); int nlen=AS_STRING(args[0])->len;
                    if(nlen==0){ result=NUMBER_VAL(0); break; }
                    int cnt=0; const char *p=s->chars;
                    while((p=(const char*)memmem(p,(size_t)(s->chars+s->len-p),needle,(size_t)nlen))!=NULL){ cnt++; p+=nlen; }
                    result=NUMBER_VAL((double)cnt); break;
                }
                case METHOD_PAD_LEFT:{
                    int total=argc>=1&&IS_NUMBER(args[0])?(int)AS_NUMBER(args[0]):s->len;
                    const char *ch=argc>=2&&IS_STRING(args[1])?AS_CSTR(args[1]):" ";
                    int chlen=argc>=2&&IS_STRING(args[1])?AS_STRING(args[1])->len:1;
                    if(total<=s->len||chlen==0){ result=STRING_VAL(s); break; }
                    int pad=total-s->len;
                    char *buf=(char*)malloc((size_t)total+1);
                    int pos=0; while(pos<pad){ buf[pos]=ch[pos%chlen]; pos++; }
                    memcpy(buf+pad,s->chars,(size_t)s->len); buf[total]='\0';
                    result=STRING_VAL(gc_string(buf,total)); free(buf); break;
                }
                case METHOD_PAD_RIGHT:{
                    int total=argc>=1&&IS_NUMBER(args[0])?(int)AS_NUMBER(args[0]):s->len;
                    const char *ch=argc>=2&&IS_STRING(args[1])?AS_CSTR(args[1]):" ";
                    int chlen=argc>=2&&IS_STRING(args[1])?AS_STRING(args[1])->len:1;
                    if(total<=s->len||chlen==0){ result=STRING_VAL(s); break; }
                    int pad=total-s->len;
                    char *buf=(char*)malloc((size_t)total+1);
                    memcpy(buf,s->chars,(size_t)s->len);
                    for(int pi=0;pi<pad;pi++) buf[s->len+pi]=ch[pi%chlen];
                    buf[total]='\0';
                    result=STRING_VAL(gc_string(buf,total)); free(buf); break;
                }
                case METHOD_REPEAT:{
                    int n=argc>=1&&IS_NUMBER(args[0])?(int)AS_NUMBER(args[0]):0;
                    if(n<=0||s->len==0){ result=STRING_VAL(gc_cstring("")); break; }
                    int total=s->len*n;
                    char *buf=(char*)malloc((size_t)total+1);
                    for(int ri=0;ri<n;ri++) memcpy(buf+ri*s->len,s->chars,(size_t)s->len);
                    buf[total]='\0';
                    result=STRING_VAL(gc_string(buf,total)); free(buf); break;
                }
                case METHOD_CHAR_AT:{
                    int idx=argc>=1&&IS_NUMBER(args[0])?(int)AS_NUMBER(args[0]):0;
                    if(idx<0) idx=s->len+idx;
                    if(idx<0||idx>=s->len){ result=NIL_VAL; break; }
                    result=STRING_VAL(gc_string(s->chars+idx,1)); break;
                }
                default: RT_ERROR("unknown string method '%s'",method_name((ArrayMethod)mid));
            }
            vm->stack_top-=(int)argc+1;
            PUSH(result); DISPATCH();
        }

        
        if(IS_DICT(av)){
            ObjDict *d=AS_DICT(av);
            Value *args=&vm->stack[vm->stack_top-argc];
            switch((ArrayMethod)mid){
                case METHOD_DICT_SIZE:
                case METHOD_LENGTH:{
                    vm->stack_top-=(int)argc+1; PUSH(NUMBER_VAL((double)d->count)); DISPATCH();
                }
                case METHOD_DICT_HAS:
                case METHOD_CONTAINS:{
                    if(argc<1||!IS_STRING(args[0])){ vm->stack_top-=(int)argc+1; PUSH(BOOL_VAL(false)); DISPATCH(); }
                    Value dv=gc_dict_get(d,AS_STRING(args[0]));
                    vm->stack_top-=(int)argc+1; PUSH(BOOL_VAL(!IS_NIL(dv))); DISPATCH();
                }
                case METHOD_DICT_GET:
                case METHOD_FIND:{
                    if(argc<1||!IS_STRING(args[0])){ vm->stack_top-=(int)argc+1; PUSH(NIL_VAL); DISPATCH(); }
                    Value dv=gc_dict_get(d,AS_STRING(args[0]));
                    Value def=argc>=2?args[1]:NIL_VAL;
                    vm->stack_top-=(int)argc+1; PUSH(IS_NIL(dv)?def:dv); DISPATCH();
                }
                case METHOD_DICT_DELETE:{
                    if(argc<1||!IS_STRING(args[0])){ vm->stack_top-=(int)argc+1; PUSH(BOOL_VAL(false)); DISPATCH(); }
                    bool dok=gc_dict_delete(d,AS_STRING(args[0]));
                    vm->stack_top-=(int)argc+1; PUSH(BOOL_VAL(dok)); DISPATCH();
                }
                case METHOD_DICT_KEYS:{
                    ObjArray *ka=gc_array();
                    for(int di=0;di<d->cap;di++) if(d->entries[di].key) gc_arr_push(ka,STRING_VAL(d->entries[di].key));
                    vm->stack_top-=(int)argc+1; PUSH(ARRAY_VAL(ka)); DISPATCH();
                }
                case METHOD_DICT_VALUES:{
                    ObjArray *va=gc_array();
                    for(int di=0;di<d->cap;di++) if(d->entries[di].key) gc_arr_push(va,d->entries[di].value);
                    vm->stack_top-=(int)argc+1; PUSH(ARRAY_VAL(va)); DISPATCH();
                }
                case METHOD_DICT_MERGE:{
                    ObjDict *out=gc_dict();
                    for(int di=0;di<d->cap;di++) if(d->entries[di].key) gc_dict_set(out,d->entries[di].key,d->entries[di].value);
                    if(argc>=1&&IS_DICT(args[0])){ ObjDict *od=AS_DICT(args[0]); for(int di=0;di<od->cap;di++) if(od->entries[di].key) gc_dict_set(out,od->entries[di].key,od->entries[di].value); }
                    vm->stack_top-=(int)argc+1; PUSH(DICT_VAL(out)); DISPATCH();
                }
                case METHOD_DICT_TO_ARR:{
                    ObjArray *pairs=gc_array();
                    for(int di=0;di<d->cap;di++) if(d->entries[di].key){ ObjArray *pair=gc_array(); gc_arr_push(pair,STRING_VAL(d->entries[di].key)); gc_arr_push(pair,d->entries[di].value); gc_arr_push(pairs,ARRAY_VAL(pair)); }
                    vm->stack_top-=(int)argc+1; PUSH(ARRAY_VAL(pairs)); DISPATCH();
                }
                default: RT_ERROR_TYPE("method '%s' not supported on dict",method_name((ArrayMethod)mid));
            }
        }

        if(!IS_ARRAY(av))
            RT_ERROR_TYPE("method '%s' on %s (not array or string)",method_name((ArrayMethod)mid),vtype(av));
        ObjArray *a=AS_ARRAY(av);
        Value *args=&vm->stack[vm->stack_top-argc];
        switch((ArrayMethod)mid){
            case METHOD_ADD:    arr_push(a,args[0]); break;
            case METHOD_INSERT:{
                if(!IS_NUMBER(args[0])) RT_ERROR_TYPE("insert: index must be number");
                int i=(int)AS_NUMBER(args[0]);
                if(i<0) i=a->len+i+1;
                arr_insert(a,i,args[1]); break;
            }
            case METHOD_CUT:{
                if(!IS_NUMBER(args[0])) RT_ERROR_TYPE("cut: index must be number");
                int i=(int)AS_NUMBER(args[0]);
                if(i<0) i=a->len+i;
                if(i<0||i>=a->len) RT_ERROR_IDX("cut: index %d out of bounds",i);
                arr_cut(a,i); break;
            }
            case METHOD_REMOVE: arr_remove(a,args[0]); break;
            case METHOD_RALL:   arr_rall(a,args[0]);   break;
            case METHOD_SORT:
                
                if(a->items && a->len>1)
                    qsort(a->items,(size_t)a->len,sizeof(Value),val_cmp);
                break;
            case METHOD_REVERSE_ARR:{
                if(a->items){
                    for(int i=0,j=a->len-1;i<j;i++,j--){
                        Value tmp=a->items[i]; a->items[i]=a->items[j]; a->items[j]=tmp;
                    }
                } break;
            }
            case METHOD_FIND:{
                int found=-1;
                if(a->items){
                    for(int i=0;i<a->len;i++) if(val_eq(a->items[i],args[0])){ found=i; break; }
                }
                vm->stack_top-=(int)argc+1;
                PUSH(NUMBER_VAL(found)); DISPATCH();
            }
            case METHOD_CONTAINS:{
                bool has=false;
                if(a->items){
                    for(int i=0;i<a->len;i++) if(val_eq(a->items[i],args[0])){ has=true; break; }
                }
                vm->stack_top-=(int)argc+1;
                PUSH(BOOL_VAL(has)); DISPATCH();
            }
            
            case METHOD_JOIN:{
                const char *sep=""; int seplen=0;
                if(argc>=1&&IS_STRING(args[0])){ sep=AS_CSTR(args[0]); seplen=AS_STRING(args[0])->len; }
                
                size_t total=0;
                for(int i=0;i<a->len;i++){
                    if(i>0) total+=(size_t)seplen;
                    if(IS_STRING(a->items[i])) total+=(size_t)AS_STRING(a->items[i])->len;
                    else total+=8; 
                }
                char *buf=(char*)malloc(total+64); size_t pos=0;
                for(int i=0;i<a->len;i++){
                    if(i>0){ memcpy(buf+pos,sep,(size_t)seplen); pos+=(size_t)seplen; }
                    if(IS_STRING(a->items[i])){ ObjString *si=AS_STRING(a->items[i]); memcpy(buf+pos,si->chars,(size_t)si->len); pos+=(size_t)si->len; }
                    else if(IS_NUMBER(a->items[i])){ double dv=AS_NUMBER(a->items[i]); int n; if(dv==(long long)dv) n=snprintf(buf+pos,32,"%lld",(long long)dv); else n=snprintf(buf+pos,32,"%.14g",dv); pos+=(size_t)n; }
                    else if(IS_BOOL(a->items[i])){ const char *bv=a->items[i].as.boolean?"true":"false"; size_t bl=strlen(bv); memcpy(buf+pos,bv,bl); pos+=bl; }
                    else if(IS_NIL(a->items[i])){ memcpy(buf+pos,"nil",3); pos+=3; }
                }
                buf[pos]='\0';
                vm->stack_top-=(int)argc+1; PUSH(STRING_VAL(gc_string(buf,(int)pos))); free(buf); DISPATCH();
            }
            case METHOD_POP:{
                if(a->len==0){ vm->stack_top-=(int)argc+1; PUSH(NIL_VAL); DISPATCH(); }
                Value v=a->items[a->len-1]; a->len--;
                vm->stack_top-=(int)argc+1; PUSH(v); DISPATCH();
            }
            case METHOD_SHIFT:{
                if(a->len==0){ vm->stack_top-=(int)argc+1; PUSH(NIL_VAL); DISPATCH(); }
                Value v=a->items[0];
                memmove(a->items,a->items+1,(size_t)(a->len-1)*sizeof(Value)); a->len--;
                vm->stack_top-=(int)argc+1; PUSH(v); DISPATCH();
            }
            case METHOD_FIRST:{
                Value v=a->len>0?a->items[0]:NIL_VAL;
                vm->stack_top-=(int)argc+1; PUSH(v); DISPATCH();
            }
            case METHOD_LAST:{
                Value v=a->len>0?a->items[a->len-1]:NIL_VAL;
                vm->stack_top-=(int)argc+1; PUSH(v); DISPATCH();
            }
            case METHOD_SUM:{
                double s2=0;
                for(int i=0;i<a->len;i++) if(IS_NUMBER(a->items[i])) s2+=AS_NUMBER(a->items[i]);
                vm->stack_top-=(int)argc+1; PUSH(NUMBER_VAL(s2)); DISPATCH();
            }
            case METHOD_ARR_MIN:{
                if(a->len==0){ vm->stack_top-=(int)argc+1; PUSH(NIL_VAL); DISPATCH(); }
                Value mn=a->items[0];
                for(int i=1;i<a->len;i++) if(IS_NUMBER(a->items[i])&&IS_NUMBER(mn)&&AS_NUMBER(a->items[i])<AS_NUMBER(mn)) mn=a->items[i];
                vm->stack_top-=(int)argc+1; PUSH(mn); DISPATCH();
            }
            case METHOD_ARR_MAX:{
                if(a->len==0){ vm->stack_top-=(int)argc+1; PUSH(NIL_VAL); DISPATCH(); }
                Value mx=a->items[0];
                for(int i=1;i<a->len;i++) if(IS_NUMBER(a->items[i])&&IS_NUMBER(mx)&&AS_NUMBER(a->items[i])>AS_NUMBER(mx)) mx=a->items[i];
                vm->stack_top-=(int)argc+1; PUSH(mx); DISPATCH();
            }
            case METHOD_COUNT_VAL:{
                int cnt=0;
                if(argc>=1 && a->items) for(int i=0;i<a->len;i++) if(val_eq(a->items[i],args[0])) cnt++;
                vm->stack_top-=(int)argc+1; PUSH(NUMBER_VAL((double)cnt)); DISPATCH();
            }
            case METHOD_INDEX_OF:{
                int idx=-1;
                if(argc>=1 && a->items) for(int i=0;i<a->len;i++) if(val_eq(a->items[i],args[0])){ idx=i; break; }
                vm->stack_top-=(int)argc+1; PUSH(NUMBER_VAL((double)idx)); DISPATCH();
            }
            case METHOD_COPY:{
                ObjArray *cp=gc_array();
                if(a->items) for(int i=0;i<a->len;i++) gc_arr_push(cp,a->items[i]);
                vm->stack_top-=(int)argc+1; PUSH(ARRAY_VAL(cp)); DISPATCH();
            }
            case METHOD_FILL:{
                Value fv=argc>=1?args[0]:NIL_VAL;
                int n2=argc>=2&&IS_NUMBER(args[1])?(int)AS_NUMBER(args[1]):0;
                ObjArray *fa=gc_array();
                for(int i=0;i<n2;i++) gc_arr_push(fa,fv);
                vm->stack_top-=(int)argc+1; PUSH(ARRAY_VAL(fa)); DISPATCH();
            }
            case METHOD_UNIQUE:{
                ObjArray *u=gc_array();
                if(a->items){ for(int i=0;i<a->len;i++){ bool dup=false; for(int j=0;j<u->len&&!dup;j++) dup=val_eq(u->items[j],a->items[i]); if(!dup) gc_arr_push(u,a->items[i]); } }
                vm->stack_top-=(int)argc+1; PUSH(ARRAY_VAL(u)); DISPATCH();
            }
            case METHOD_FLAT:{
                ObjArray *fl=gc_array();
                if(a->items){ for(int i=0;i<a->len;i++){ if(IS_ARRAY(a->items[i])){ ObjArray *inner=AS_ARRAY(a->items[i]); if(inner->items) for(int j=0;j<inner->len;j++) gc_arr_push(fl,inner->items[j]); } else gc_arr_push(fl,a->items[i]); } }
                vm->stack_top-=(int)argc+1; PUSH(ARRAY_VAL(fl)); DISPATCH();
            }
            
            case METHOD_MAP:{
                
                if(argc<1) RT_ERROR_TYPE("map: expects a function argument");
                if(!IS_FUNCTION(args[0])) RT_ERROR_TYPE("map: argument must be a function");
                
                RT_ERROR("map: callbacks need lambda support (v4.4)");
            }
            case METHOD_FILTER:
            case METHOD_ANY:
            case METHOD_ALL:
            case METHOD_REDUCE:
                RT_ERROR("'%s' needs lambda support (v4.4) - use a for loop",method_name((ArrayMethod)mid));
            default: RT_ERROR("unknown array method '%s'",method_name((ArrayMethod)mid));
        }
        vm->stack_top-=(int)argc+1;
        PUSH(NIL_VAL); DISPATCH(); }

    CASE(OP_DICT_NEW):{ ObjDict *d=gc_dict(); PUSH(DICT_VAL(d)); DISPATCH(); }
    CASE(OP_DICT_SET):{
        
        Value val=POP(),key=POP(),dv=POP();
        if(IS_NIL(dv)) RT_ERROR_TYPE("Attempted to index nil value.");
        if(!IS_DICT(dv)) RT_ERROR_TYPE("DICT_SET on non-dict");
        if(!IS_STRING(key)) RT_ERROR_TYPE("dict key must be a string");
        gc_dict_set(AS_DICT(dv),AS_STRING(key),val);
        PUSH(dv); DISPATCH(); }
    CASE(OP_DICT_GET):{
        Value key=POP(),dv=POP();
        if(IS_NIL(dv)) RT_ERROR_TYPE("Attempted to index nil value.");
        if(!IS_DICT(dv)) RT_ERROR_TYPE("DICT_GET on non-dict");
        if(!IS_STRING(key)) RT_ERROR_TYPE("dict key must be a string");
        PUSH(gc_dict_get(AS_DICT(dv),AS_STRING(key))); DISPATCH(); }

    
    CASE(OP_FOREACH_INIT):{
        
        Value iterable=POP();
        if(IS_NIL(iterable)) RT_ERROR_TYPE("Attempted to iterate nil value.");
        if(!IS_ARRAY(iterable)) RT_ERROR_TYPE("for-in requires an array, got %s",vtype(iterable));
        PUSH(iterable);
        PUSH(NUMBER_VAL(0)); DISPATCH(); }
    CASE(OP_FOREACH_STEP):{
        
        
        
        uint16_t end_offset=READ_U16();
        Value counter=POP();
        Value iterable=TOP(); 
        ObjArray *arr=AS_ARRAY(iterable);
        int idx=(int)AS_NUMBER(counter);
        if(idx>=arr->len){
            (void)POP(); 
            Chunk *ch=FRAME.function?&FRAME.function->chunk:vm->top_chunk;
            FRAME.ip=ch->code+end_offset;
        } else {
            if(!arr->items)
                RT_ERROR("internal: for-in array has length %d but no items buffer",
                         arr->len);
            PUSH(NUMBER_VAL(idx+1)); 
            PUSH(arr->items[idx]);   
        }
        DISPATCH(); }

    
    CASE(OP_PUSH_HANDLER):{
        uint16_t off=READ_U16();
        if(vm->try_top>=MAX_TRY_DEPTH) RT_ERROR_STACK("too many nested try blocks");
        Chunk *ch=FRAME.function?&FRAME.function->chunk:vm->top_chunk;
        TryFrame *tf=&vm->try_stack[vm->try_top++];
        tf->handler_ip=ch->code+off;
        tf->stack_top=vm->stack_top;
        tf->frame_count=vm->frame_count;
        DISPATCH(); }
    CASE(OP_POP_HANDLER):{
        if(vm->try_top>0) vm->try_top--;
        DISPATCH(); }
    CASE(OP_THROW):{
        Value v=POP();
        ObjString *msg=val_to_str(v);
        if(vm->try_top>0){
            vm_throw_error(vm,msg->chars);
            if(vm->has_error) return VM_RUNTIME_ERROR;
        } else {
            error_runtime(vm_line(vm),"%s",msg->chars);
            return VM_RUNTIME_ERROR;
        }
        DISPATCH(); }
    CASE(OP_GET_ERROR):
        PUSH(vm->error_value); DISPATCH();

    
    CASE(OP_TYPEOF):{
        Value v=POP();
        PUSH(STRING_VAL(gc_cstring(vtype(v)))); DISPATCH(); }

    
    CASE(OP_GC_SAFEPOINT):
        gc_step(vm); DISPATCH();

    
    CASE(OP_PROMPT):{ Value v=POP(); print_value(v); fflush(stdout); DISPATCH(); }

    CASE(OP_INPUT):{
        uint16_t idx=READ_U16(); uint8_t iloc=READ_BYTE();
        char buf[MAX_STRING_LEN];
        if(fgets(buf,sizeof(buf),stdin)){
            size_t l=strlen(buf);
            if(l>0&&buf[l-1]=='\n') buf[--l]='\0';
            if(l>0&&buf[l-1]=='\r') buf[--l]='\0';
        } else buf[0]='\0';
        char *end; double n=strtod(buf,&end);
        Value v=(end!=buf&&*end=='\0')?NUMBER_VAL(n):STRING_VAL(gc_cstring(buf));
        if(iloc) set_local(vm,(int)idx,v);
        else     vm->globals[idx]=v;
        DISPATCH(); }

    CASE(OP_PRINT):{
        uint16_t n=READ_U16();
        
        int base=vm->stack_top-(int)n;
        for(int i=0;i<(int)n;i++){ if(i) printf(" "); print_value(vm->stack[base+i]); }
        printf("\n"); fflush(stdout);
        vm->stack_top=base;
        DISPATCH(); }

    CASE(OP_NATIVE):{
        uint16_t nid=(uint16_t)READ_BYTE()|((uint16_t)READ_BYTE()<<8);
        uint8_t  nac=READ_BYTE();
        native_dispatch(vm,nid,nac);
        DISPATCH(); }

    CASE(OP_CONST_0): PUSH(NUMBER_VAL(0.0)); DISPATCH();
    CASE(OP_CONST_1): PUSH(NUMBER_VAL(1.0)); DISPATCH();

    CASE(OP_INC_LOCAL):{
        uint16_t s=READ_U16();
        Value v=get_local(vm,(int)s);
        if(!IS_NUMBER(v)) RT_ERROR_TYPE("INC_LOCAL: expected number, got %s",vtype(v));
        set_local(vm,(int)s,NUMBER_VAL(AS_NUMBER(v)+1.0));
        DISPATCH(); }

    CASE(OP_DEC_LOCAL):{
        uint16_t s=READ_U16();
        Value v=get_local(vm,(int)s);
        if(!IS_NUMBER(v)) RT_ERROR_TYPE("DEC_LOCAL: expected number, got %s",vtype(v));
        set_local(vm,(int)s,NUMBER_VAL(AS_NUMBER(v)-1.0));
        DISPATCH(); }

    CASE(OP_CALL_TAIL):{
        

        uint16_t argc=READ_U16();
        Value fv=vm->stack[vm->stack_top-argc-1];
        if(!IS_FUNCTION(fv)) RT_ERROR_TYPE("attempt to tail-call a %s",vtype(fv));
        FunctionObject *f=AS_FUNCTION(fv);
        if((int)argc!=f->arity)
            RT_ERROR_TYPE("'%s' expects %d arg(s) but got %d",f->name,f->arity,(int)argc);
        
        int base=FRAME.base_idx;
        for(int i=0;i<(int)argc;i++)
            vm->stack[base+i]=vm->stack[vm->stack_top-argc+i];
        vm->stack_top=base+argc;
        
        FRAME.function=f;
        FRAME.ip=f->chunk.code;
        DISPATCH(); }

    CASE(OP_HALT): return VM_OK;

    CASE(OP_AWAIT):
        
        DISPATCH();

    

    CASE(OP_IN):{
        Value container=POP(), val=POP();
        bool found=false;
        if(IS_ARRAY(container)){
            ObjArray *a=AS_ARRAY(container);
            for(int i=0;i<a->len;i++){
                if(val_eq(a->items[i],val)){ found=true; break; }
            }
        } else if(IS_DICT(container)){
            if(IS_STRING(val)){
                Value got=gc_dict_get(AS_DICT(container),AS_STRING(val));
                found=!IS_NIL(got);
            } else {
                RT_ERROR_TYPE("'in' on dict requires a string key, got %s",vtype(val));
            }
        } else if(IS_STRING(container)){
            if(!IS_STRING(val)) RT_ERROR_TYPE("'in' on string requires a string needle, got %s",vtype(val));
            ObjString *haystack=AS_STRING(container);
            ObjString *needle  =AS_STRING(val);
            if(needle->len==0){ found=true; }
            else if(needle->len<=haystack->len){
                found=(memmem(haystack->chars,(size_t)haystack->len,
                              needle->chars,(size_t)needle->len)!=NULL);
            }
        } else {
            RT_ERROR_TYPE("'in' requires array, dict, or string on right side, got %s",vtype(container));
        }
        PUSH(BOOL_VAL(found)); DISPATCH();
    }

#ifdef __GNUC__
    
    {
        RT_ERROR("unknown opcode 0x%02X",(unsigned)*(FIP-1));
    }
#else
    default:
        RT_ERROR("unknown opcode 0x%02X",(unsigned)_op);
    } 
#endif

    return VM_OK;
}
