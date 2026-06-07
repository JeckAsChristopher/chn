

#define _POSIX_C_SOURCE 200809L
#include "bytecode.h"
#include "gc.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

bool g_minify = true;

static uint32_t fnv1a32(const char *s) {
    uint32_t h = 0x811C9DC5u;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 0x01000193u;
    return h;
}

static void minify_rename(const char *orig, char *out, size_t outsz) {
    if (!orig || !*orig || orig[0] == '<') {
        strncpy(out, orig ? orig : "", outsz - 1);
        out[outsz - 1] = '\0';
        return;
    }
    uint32_t h = fnv1a32(orig);
    snprintf(out, outsz, "_m%06x", (unsigned)(h & 0xFFFFFFu));
}

static uint16_t minify_seed(const char *name) {
    uint32_t h = fnv1a32(name ? name : "<module>");
    uint16_t s = (uint16_t)((h ^ (h >> 16)) & 0xFFFFu);
    return s ? s : 1u;
}

static void stream_xor(uint8_t *code, int len, uint16_t seed) {
    uint32_t st = (uint32_t)seed ^ 0xA3C15E7Du;
    for (int i = 0; i < len; i++) {
        st = st * 0x6C078965u + 1u;
        code[i] ^= (uint8_t)((st >> 16) & 0xFFu);
    }
}

static void string_xor(char *buf, int len, uint16_t seed, int pool_idx) {
    uint8_t key = (uint8_t)((seed ^ (uint32_t)(pool_idx * 0x53u + 0xA7u)) & 0xFFu);
    for (int i = 0; i < len; i++)
        buf[i] ^= (uint8_t)((key + (uint32_t)(i * 0x1Bu)) & 0xFFu);
}

static int opcode_extra(uint8_t op) {
    switch ((OpCode)op) {
        
        case OP_CONST:
        case OP_GET_VAR: case OP_SET_VAR: case OP_DEF_VAR:
        case OP_GET_LOCAL: case OP_SET_LOCAL:
        case OP_JUMP: case OP_JUMP_IF_FALSE:
        case OP_JUMP_IF_TRUE: case OP_JUMP_IF_NIL:
        case OP_CALL: case OP_CALL_TAIL:
        case OP_FOREACH_STEP:
        case OP_PUSH_HANDLER:
        case OP_PRINT:
        case OP_INC_LOCAL: case OP_DEC_LOCAL:
            return 2;
        
        case OP_INPUT:
            return 3;
        
        case OP_NATIVE:
            return 3;
        
        case OP_METHOD_CALL:
            return 2;
        
        default:
            return 0;
    }
}

typedef struct {
    Value   *vals;
    int      count;
    int      cap;
} ConstPool;

static bool const_eq(Value a, Value b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case VAL_NUMBER:   return a.as.number == b.as.number;
        case VAL_BOOL:     return a.as.boolean == b.as.boolean;
        case VAL_NIL:      return true;
        case VAL_STRING:
            return a.as.string->len == b.as.string->len &&
                   memcmp(a.as.string->chars, b.as.string->chars,
                          (size_t)a.as.string->len) == 0;
        case VAL_FUNCTION:
            return a.as.function && b.as.function &&
                   strcmp(a.as.function->name, b.as.function->name) == 0;
        default: return false;
    }
}

static bool dedup_constants(Value *consts, int count,
                             ConstPool *out_pool, int **out_remap) {
    *out_remap = (int *)malloc((size_t)count * sizeof(int));
    if (!*out_remap && count > 0) return false;
    out_pool->vals  = (Value *)malloc((size_t)count * sizeof(Value));
    out_pool->count = 0;
    out_pool->cap   = count;
    if (!out_pool->vals && count > 0) { free(*out_remap); return false; }

    for (int i = 0; i < count; i++) {
        int found = -1;
        for (int j = 0; j < out_pool->count; j++) {
            if (const_eq(consts[i], out_pool->vals[j])) { found = j; break; }
        }
        if (found >= 0) {
            (*out_remap)[i] = found;
        } else {
            out_pool->vals[out_pool->count] = consts[i];
            (*out_remap)[i] = out_pool->count++;
        }
    }
    return true;
}

static void patch_const_ops(uint8_t *code, int len, const int *remap) {
    int i = 0;
    while (i < len) {
        uint8_t op = code[i];
        int extra = opcode_extra(op);
        if (op == (uint8_t)OP_CONST && i + 2 < len) {
            uint16_t old_idx = (uint16_t)(code[i+1] | (code[i+2] << 8));
            uint16_t new_idx = (uint16_t)remap[old_idx];
            code[i+1] = (uint8_t)(new_idx & 0xFF);
            code[i+2] = (uint8_t)(new_idx >> 8);
        }
        i += 1 + extra;
    }
}

const char *bc_result_str(BCResult r){
    switch(r){
        case BC_OK:            return "ok";
        case BC_ERR_IO:        return "I/O error";
        case BC_ERR_MAGIC:     return "bad magic (not a CHNO bytecode file)";
        case BC_ERR_VERSION:   return "unsupported bytecode version";
        case BC_ERR_CHECKSUM:  return "checksum mismatch (file may be corrupted)";
        case BC_ERR_TRUNCATED: return "payload truncated";
        case BC_ERR_OOM:       return "out of memory";
        case BC_ERR_SECTION:   return "required section missing";
        case BC_ERR_CCO:       return "malformed CCO container";
        default:               return "unknown error";
    }
}

static uint32_t adler32(const uint8_t *data, size_t len){
    uint32_t a=1,b=0;
    for(size_t i=0;i<len;i++){ a=(a+data[i])%65521u; b=(b+a)%65521u; }
    return (b<<16)|a;
}

typedef struct { uint8_t *buf; size_t len, cap; } WBuf;

static void wb_grow(WBuf *w, size_t need){
    if(w->len+need<=w->cap) return;
    size_t nc=w->cap<1024?1024:w->cap*2;
    while(nc<w->len+need) nc*=2;
    w->buf=(uint8_t*)realloc(w->buf,nc);
    if(!w->buf){fprintf(stderr,"bc: oom\n");exit(1);}
    w->cap=nc;
}
static void wb_u8 (WBuf *w, uint8_t  v){ wb_grow(w,1); w->buf[w->len++]=v; }
static void wb_u16(WBuf *w, uint16_t v){ wb_u8(w,(uint8_t)(v&0xFF)); wb_u8(w,(uint8_t)(v>>8)); }
static void wb_i16(WBuf *w, int16_t  v){ wb_u16(w,(uint16_t)v); }
static void wb_u32(WBuf *w, uint32_t v){
    wb_u8(w,(uint8_t)(v&0xFF)); wb_u8(w,(uint8_t)((v>>8)&0xFF));
    wb_u8(w,(uint8_t)((v>>16)&0xFF)); wb_u8(w,(uint8_t)((v>>24)&0xFF));
}
static void wb_i64(WBuf *w, int64_t v){
    for(int i=0;i<8;i++){ wb_u8(w,(uint8_t)(v&0xFF)); v>>=8; }
}
static void wb_f64(WBuf *w, double v){
    uint64_t bits; memcpy(&bits,&v,8);
    for(int i=0;i<8;i++){ wb_u8(w,(uint8_t)(bits&0xFF)); bits>>=8; }
}
static void wb_str(WBuf *w, const char *s, int len){
    wb_u32(w,(uint32_t)len);
    wb_grow(w,(size_t)len);
    memcpy(w->buf+w->len,s,(size_t)len); w->len+=(size_t)len;
}
static void wb_cstr(WBuf *w, const char *s){ wb_str(w,s?s:"",(int)(s?strlen(s):0)); }

static void wb_const(WBuf *w, Value v){
    switch(v.type){
        case VAL_NUMBER:{
            double n=v.as.number;
            int64_t as_int=(int64_t)n;
            if((double)as_int==n){
                
                wb_u8(w,CTAG_INT);
                wb_i64(w,as_int);
            } else {
                wb_u8(w,CTAG_FLOAT);
                wb_f64(w,n);
            }
            break;
        }
        case VAL_STRING:
            wb_u8(w,CTAG_STRING);
            wb_str(w,v.as.string->chars,v.as.string->len);
            break;
        case VAL_BOOL:
            wb_u8(w,CTAG_BOOL);
            wb_u8(w,v.as.boolean?1:0);
            break;
        case VAL_NIL:
            wb_u8(w,CTAG_NIL);
            break;
        case VAL_FUNCTION:
            wb_u8(w,CTAG_FUNCREF);
            wb_cstr(w,v.as.function?v.as.function->name:"");
            break;
        default:
            wb_u8(w,CTAG_NIL);
            break;
    }
}

static void wb_lnotab(WBuf *w, Chunk *ch){
    

    wb_u32(w,(uint32_t)ch->lines_len);
    int prev_offset=0, prev_line=0;
    for(int i=0;i<ch->lines_len;i++){
        int bdelta = ch->lines[i].offset - prev_offset;
        int ldelta = ch->lines[i].line   - prev_line;
        
        if(bdelta>32767) bdelta=32767;
        if(ldelta<-32768) ldelta=-32768;
        if(ldelta>32767)  ldelta= 32767;
        wb_u16(w,(uint16_t)bdelta);
        wb_i16(w,(int16_t) ldelta);
        prev_offset=ch->lines[i].offset;
        prev_line  =ch->lines[i].line;
    }
}

static void wb_codeobj(WBuf *w, Chunk *ch, FunctionObject *fn) {
    char name_buf[64];
    const char *raw_name = fn ? fn->name : "<module>";
    uint16_t enc_seed = 0;

    

    bool can_rename = g_minify && fn && !fn->exported &&
                      fn->visibility != VIS_PUBLIC &&
                      raw_name[0] != '<';

    if (can_rename) {
        minify_rename(raw_name, name_buf, sizeof(name_buf));
        enc_seed = minify_seed(name_buf);
    } else {
        strncpy(name_buf, raw_name, sizeof(name_buf) - 1);
        name_buf[sizeof(name_buf) - 1] = '\0';
        
        if (g_minify && fn) enc_seed = minify_seed(raw_name);
    }

    ConstPool pool = {0};
    int *remap = NULL;
    bool did_dedup = false;

    if (g_minify && ch->const_count > 0) {
        did_dedup = dedup_constants(ch->constants, ch->const_count, &pool, &remap);
        if (!did_dedup) {
            
            pool.vals  = ch->constants;
            pool.count = ch->const_count;
        }
    } else {
        pool.vals  = ch->constants;
        pool.count = ch->const_count;
    }

    uint8_t *code_buf = NULL;
    int      code_len = ch->code_len;
    if (g_minify && ch->code_len > 0) {
        code_buf = (uint8_t *)malloc((size_t)ch->code_len);
        if (code_buf) {
            memcpy(code_buf, ch->code, (size_t)ch->code_len);
            if (did_dedup && remap) patch_const_ops(code_buf, code_len, remap);
            if (enc_seed) stream_xor(code_buf, code_len, enc_seed);
        }
    }

    
    wb_cstr(w, name_buf);
    
    wb_cstr(w, g_minify ? "" : (fn ? fn->source_file : ""));
    
    wb_u16(w, enc_seed);
    
    uint8_t  arity = fn ? (uint8_t)fn->arity : 0;
    uint16_t flags = 0;
    if (fn && fn->exported)                flags |= COFLAG_EXPORTED;
    if (fn && fn->visibility == VIS_PRIVATE) flags |= COFLAG_PRIVATE;
    uint8_t  vis   = fn ? (uint8_t)fn->visibility : 0;
    wb_u8(w, arity);
    wb_u16(w, flags);
    wb_u16(w, (uint16_t)ch->stack_size);
    wb_u8(w, vis);
    
    wb_u8(w, arity);
    if (fn) for (int i = 0; i < fn->arity; i++) wb_cstr(w, fn->params[i]);

    wb_u32(w, (uint32_t)pool.count);
    for (int i = 0; i < pool.count; i++) {
        Value v = pool.vals[i];
        switch (v.type) {
            case VAL_NUMBER: {
                double n = v.as.number;
                int64_t as_int = (int64_t)n;
                if ((double)as_int == n) { wb_u8(w, CTAG_INT);   wb_i64(w, as_int); }
                else                     { wb_u8(w, CTAG_FLOAT); wb_f64(w, n); }
                break;
            }
            case VAL_STRING: {
                wb_u8(w, CTAG_STRING);
                int slen = v.as.string->len;
                if (enc_seed && slen > 0) {
                    
                    char *tmp = (char *)malloc((size_t)slen);
                    if (tmp) {
                        memcpy(tmp, v.as.string->chars, (size_t)slen);
                        string_xor(tmp, slen, enc_seed, i);
                        wb_str(w, tmp, slen);
                        free(tmp);
                    } else {
                        wb_str(w, v.as.string->chars, slen);
                    }
                } else {
                    wb_str(w, v.as.string->chars, slen);
                }
                break;
            }
            case VAL_BOOL:
                wb_u8(w, CTAG_BOOL); wb_u8(w, v.as.boolean ? 1 : 0);
                break;
            case VAL_NIL:
                wb_u8(w, CTAG_NIL);
                break;
            case VAL_FUNCTION: {
                
                wb_u8(w, CTAG_FUNCREF);
                const char *fn_name = v.as.function ? v.as.function->name : "";
                bool ref_can_rename = g_minify && v.as.function &&
                                      !v.as.function->exported &&
                                      v.as.function->visibility != VIS_PUBLIC;
                if (ref_can_rename) {
                    char ref_buf[64];
                    minify_rename(fn_name, ref_buf, sizeof(ref_buf));
                    wb_cstr(w, ref_buf);
                } else {
                    wb_cstr(w, fn_name);
                }
                break;
            }
            default:
                wb_u8(w, CTAG_NIL);
                break;
        }
    }

    if (g_minify) {
        wb_u16(w, 0); 
    } else {
        wb_u16(w, (uint16_t)ch->varname_count);
        for (int i = 0; i < ch->varname_count; i++) wb_cstr(w, ch->co_varnames[i]);
    }

    if (g_minify) {
        wb_u32(w, 0); 
    } else {
        wb_lnotab(w, ch);
    }

    if (code_buf) {
        
        wb_u32(w, (uint32_t)code_len);
        wb_grow(w, (size_t)code_len);
        memcpy(w->buf + w->len, code_buf, (size_t)code_len);
        w->len += (size_t)code_len;
        free(code_buf);
    } else {
        wb_u32(w, (uint32_t)ch->code_len);
        wb_grow(w, (size_t)ch->code_len);
        memcpy(w->buf + w->len, ch->code, (size_t)ch->code_len);
        w->len += (size_t)ch->code_len;
    }

    
    if (did_dedup) { free(pool.vals); }
    if (remap)     { free(remap); }
}

typedef struct { uint8_t type; uint32_t offset; uint32_t length; uint16_t crc; } SectInfo;

static SectInfo wb_section_begin(uint8_t type, WBuf *payload){
    SectInfo s; s.type=type; s.offset=(uint32_t)payload->len; s.length=0; s.crc=0;
    return s;
}
static void wb_section_end(SectInfo *s, WBuf *payload){
    s->length=(uint32_t)(payload->len - s->offset);
    s->crc   =(uint16_t)(adler32(payload->buf+s->offset, s->length)&0xFFFF);
}

static BCResult flush_chno(const char *path, uint16_t file_flags,
                            SectInfo *sects, int nsects,
                            WBuf *payload){
    FILE *fp=fopen(path,"wb");
    if(!fp){ free(payload->buf); return BC_ERR_IO; }

    
    uint32_t magic=CHNO_MAGIC;
    uint16_t ver=CHNO_VERSION;
    uint32_t nsects32=(uint32_t)nsects;
    uint32_t csum=adler32(payload->buf,payload->len);

    fwrite(&magic,4,1,fp);
    fwrite(&ver,2,1,fp);
    fwrite(&file_flags,2,1,fp);
    fwrite(&nsects32,4,1,fp);
    fwrite(&csum,4,1,fp);

    
    for(int i=0;i<nsects;i++){
        uint8_t pad=0;
        fwrite(&sects[i].type,1,1,fp);
        fwrite(&pad,1,1,fp);
        fwrite(&sects[i].offset,4,1,fp);
        fwrite(&sects[i].length,4,1,fp);
        fwrite(&sects[i].crc,2,1,fp);
    }

    
    fwrite(payload->buf,1,payload->len,fp);
    fclose(fp);
    free(payload->buf);
    return BC_OK;
}

BCResult bc_write_program(const char *path, Compiler *C){
    WBuf payload={0};
    SectInfo sects[8]; int nsects=0;

    
    sects[nsects]=wb_section_begin(SECT_IMPORTS,&payload);
    wb_u32(&payload,(uint32_t)C->imp_name_count);
    for(int i=0;i<C->imp_name_count;i++)
        wb_str(&payload,C->imp_names[i],(int)strlen(C->imp_names[i]));
    wb_section_end(&sects[nsects++],&payload);

    
    sects[nsects]=wb_section_begin(SECT_EXPORTS,&payload);
    int exported=0;
    for(int i=0;i<C->func_count;i++) if(C->functions[i]->exported) exported++;
    wb_u32(&payload,(uint32_t)exported);
    for(int i=0;i<C->func_count;i++){
        if(!C->functions[i]->exported) continue;
        wb_cstr(&payload,C->functions[i]->name);
        wb_u16(&payload,(uint16_t)i);
    }
    wb_section_end(&sects[nsects++],&payload);

    
    sects[nsects]=wb_section_begin(SECT_FUNCS,&payload);
    wb_u32(&payload,(uint32_t)C->func_count);
    for(int i=0;i<C->func_count;i++)
        wb_codeobj(&payload,&C->functions[i]->chunk,C->functions[i]);
    wb_section_end(&sects[nsects++],&payload);

    
    sects[nsects]=wb_section_begin(SECT_MAIN,&payload);
    wb_codeobj(&payload,&C->top_chunk,NULL);
    wb_section_end(&sects[nsects++],&payload);

    
    sects[nsects]=wb_section_begin(SECT_SYMBOLS,&payload);
    wb_u32(&payload,(uint32_t)C->top_chunk.var_count);
    for(int i=0;i<C->top_chunk.var_count;i++)
        wb_cstr(&payload,C->top_chunk.var_names[i]?C->top_chunk.var_names[i]:"?");
    wb_section_end(&sects[nsects++],&payload);

    uint16_t flags=CHNO_PROG_FLAG;
    return flush_chno(path,flags,sects,nsects,&payload);
}

BCResult bc_write_functions(const char *path, Compiler *C){
    WBuf payload={0};
    SectInfo sects[4]; int nsects=0;

    sects[nsects]=wb_section_begin(SECT_FUNCS,&payload);
    int ex=0;
    for(int i=0;i<C->func_count;i++) if(C->functions[i]->exported) ex++;
    wb_u32(&payload,(uint32_t)ex);
    for(int i=0;i<C->func_count;i++)
        if(C->functions[i]->exported)
            wb_codeobj(&payload,&C->functions[i]->chunk,C->functions[i]);
    wb_section_end(&sects[nsects++],&payload);

    return flush_chno(path,0,sects,nsects,&payload);
}

typedef struct { const uint8_t *p; size_t left; bool err; } RBuf;

static inline uint8_t  rb_u8 (RBuf *r){
    if(r->left<1){r->err=true;return 0;}
    uint8_t v=*r->p++; r->left--; return v;
}

static uint16_t rb_u16(RBuf *r){ uint8_t lo=rb_u8(r),hi=rb_u8(r); return (uint16_t)(lo|((uint16_t)hi<<8)); }
static int16_t  rb_i16(RBuf *r){ return (int16_t)rb_u16(r); }
static uint32_t rb_u32(RBuf *r){
    uint32_t a=rb_u8(r),b=rb_u8(r),c=rb_u8(r),d=rb_u8(r);
    return a|(b<<8)|(c<<16)|(d<<24);
}
static int64_t rb_i64(RBuf *r){
    int64_t v=0;
    for(int i=0;i<8;i++) v|=((int64_t)rb_u8(r))<<(i*8);
    return v;
}
static double rb_f64(RBuf *r){
    uint64_t bits=0;
    for(int i=0;i<8;i++) bits|=((uint64_t)rb_u8(r))<<(i*8);
    double v; memcpy(&v,&bits,8); return v;
}
static char *rb_str(RBuf *r, int *out_len){
    uint32_t len=rb_u32(r);
    if(r->left<len){r->err=true;return NULL;}
    char *s=(char*)malloc(len+1); if(!s){r->err=true;return NULL;}
    memcpy(s,r->p,len); s[len]='\0'; r->p+=len; r->left-=len;
    if(out_len) *out_len=(int)len;
    return s;
}

static Value rb_const_minified(RBuf *r, uint16_t enc_seed, int pool_idx) {
    uint8_t tag = rb_u8(r);
    switch ((ConstTag)tag) {
        case CTAG_INT:   return NUMBER_VAL((double)rb_i64(r));
        case CTAG_FLOAT: return NUMBER_VAL(rb_f64(r));
        case CTAG_STRING: {
            int l = 0; char *s = rb_str(r, &l);
            if (s && enc_seed && l > 0) string_xor(s, l, enc_seed, pool_idx);
            Value v = STRING_VAL(gc_string(s ? s : "", l));
            free(s); return v;
        }
        case CTAG_BOOL:   return BOOL_VAL(rb_u8(r) ? true : false);
        case CTAG_NIL:    return NIL_VAL;
        case CTAG_FUNCREF: {
            int l = 0; char *name = rb_str(r, &l);
            FunctionObject *f = func_lookup(name);
            free(name);
            return f ? FUNC_VAL(f) : NIL_VAL;
        }
        default: return NIL_VAL;
    }
}

static Value rb_const(RBuf *r) { return rb_const_minified(r, 0, 0); }

static void rb_lnotab(RBuf *r, Chunk *ch){
    uint32_t count=rb_u32(r);
    int offset=0, line=0;
    for(uint32_t i=0;i<count;i++){
        uint16_t bdelta=rb_u16(r);
        int16_t  ldelta=rb_i16(r);
        offset+=bdelta; line+=ldelta;
        if(ch->lines_len>=ch->lines_cap){
            int nc=ch->lines_cap<8?8:ch->lines_cap*2;
            ch->lines=(struct LineRun*)realloc(ch->lines,(size_t)nc*sizeof(ch->lines[0]));
            ch->lines_cap=nc;
        }
        ch->lines[ch->lines_len].offset=offset;
        ch->lines[ch->lines_len].line=line;
        ch->lines_len++;
    }
}

static FunctionObject *rb_codeobj(RBuf *r, Chunk *out_chunk){
    int nl=0; char *name=rb_str(r,&nl);
    int fl=0; char *file=rb_str(r,&fl);

    

    extern uint16_t _rb_chno_ver; 
    uint16_t enc_seed = 0;
    if (_rb_chno_ver >= 7) enc_seed = rb_u16(r);

    uint8_t  arity      =rb_u8(r);
    uint16_t flags      =rb_u16(r);
    uint16_t stack_size =rb_u16(r);
    uint8_t  vis        =rb_u8(r);
    uint8_t  param_count=rb_u8(r);

    FunctionObject *fn=NULL;
    bool is_main=(name&&strcmp(name,"<module>")==0);
    if(!is_main){
        fn=func_new(name,(FunctionVisibility)vis,file?file:"");
        fn->arity=(int)arity;
        fn->exported=(flags&COFLAG_EXPORTED)?true:false;
        fn->params=(char**)malloc((size_t)param_count*sizeof(char*));
        fn->param_cap=(int)param_count;
        fn->arity=(int)param_count;
    }
    for(int i=0;i<(int)param_count;i++){
        int pl=0; char *p=rb_str(r,&pl);
        if(fn) fn->params[i]=p; else free(p);
    }

    Chunk *ch=is_main?out_chunk:&fn->chunk;
    chunk_init(ch);
    ch->stack_size=(int)stack_size;

    
    uint32_t cc=rb_u32(r);
    ch->constants=(Value*)malloc((size_t)(cc ? cc : 1)*sizeof(Value));
    ch->const_cap=(int)cc;
    for(uint32_t i=0;i<cc;i++)
        ch->constants[i] = rb_const_minified(r, enc_seed, (int)i);
    ch->const_count=(int)cc;

    
    uint16_t vc=rb_u16(r);
    if(vc > 0){
        ch->co_varnames=(char**)malloc((size_t)vc*sizeof(char*));
        ch->varname_cap=(int)vc;
        for(int i=0;i<(int)vc;i++){ int l=0; ch->co_varnames[i]=rb_str(r,&l); }
    }
    ch->varname_count=(int)vc;

    
    rb_lnotab(r,ch);

    
    uint32_t code_len=rb_u32(r);
    if(r->left<code_len){r->err=true;free(name);free(file);return fn;}
    ch->code=(uint8_t*)malloc(code_len ? code_len : 1);
    if(!ch->code){r->err=true;free(name);free(file);return fn;}
    memcpy(ch->code,r->p,code_len); r->p+=code_len; r->left-=code_len;
    if(enc_seed && code_len > 0) stream_xor(ch->code, (int)code_len, enc_seed);
    ch->code_len=ch->code_cap=(int)code_len;

    if(fn) func_register(fn);
    free(name); free(file);
    return fn;
}

uint16_t _rb_chno_ver = 0;

typedef struct { uint8_t type; uint32_t offset; uint32_t length; uint16_t crc; } SectDir;

typedef struct {
    uint8_t  *data;
    size_t    len;
    uint16_t  flags;
    SectDir   sects[16];
    int       nsects;
} CHNOFile;

static BCResult chno_open(const char *path, CHNOFile *out){
    FILE *fp=fopen(path,"rb");
    if(!fp) return BC_ERR_IO;
    fseek(fp,0,SEEK_END); long fsz=ftell(fp); rewind(fp);
    if(fsz<16){fclose(fp);return BC_ERR_TRUNCATED;}

    uint32_t magic=0; uint16_t ver=0,flags=0; uint32_t nsects32=0,csum=0;
    if(fread(&magic,4,1,fp)!=1){fclose(fp);return BC_ERR_TRUNCATED;}
    if(magic!=CHNO_MAGIC){fclose(fp);return BC_ERR_MAGIC;}
    if(fread(&ver,2,1,fp)!=1){fclose(fp);return BC_ERR_TRUNCATED;}
    if(ver>CHNO_VERSION){fclose(fp);return BC_ERR_VERSION;}
    if(fread(&flags,2,1,fp)!=1||fread(&nsects32,4,1,fp)!=1||fread(&csum,4,1,fp)!=1){
        fclose(fp);return BC_ERR_TRUNCATED;
    }

    
    int ns=(int)nsects32; if(ns>16) ns=16;
    if(fseek(fp,0,SEEK_CUR)!=0){fclose(fp);return BC_ERR_IO;} 
    SectDir sects[16];
    for(int i=0;i<ns;i++){
        uint8_t type=0,pad=0;
        uint32_t off=0,len=0; uint16_t crc16=0;
        if(fread(&type,1,1,fp)!=1||fread(&pad,1,1,fp)!=1||
           fread(&off,4,1,fp)!=1||fread(&len,4,1,fp)!=1||
           fread(&crc16,2,1,fp)!=1){fclose(fp);return BC_ERR_TRUNCATED;}
        (void)pad;
        sects[i].type=(uint8_t)type; sects[i].offset=off;
        sects[i].length=len; sects[i].crc=crc16;
    }

    
    long dir_end=16+(long)ns*12;
    fseek(fp,dir_end,SEEK_SET);
    size_t plen=(size_t)(fsz-dir_end);
    uint8_t *data=(uint8_t*)malloc(plen);
    if(!data){fclose(fp);return BC_ERR_OOM;}
    if(fread(data,1,plen,fp)!=plen){free(data);fclose(fp);return BC_ERR_TRUNCATED;}
    fclose(fp);

    
    if(adler32(data,plen)!=csum){free(data);return BC_ERR_CHECKSUM;}

    out->data=data; out->len=plen; out->flags=flags;
    out->nsects=ns;
    for(int i=0;i<ns;i++) out->sects[i]=sects[i];

    
    _rb_chno_ver = ver;

    return BC_OK;
}

static bool chno_section(CHNOFile *f, uint8_t type, RBuf *out){
    for(int i=0;i<f->nsects;i++){
        if(f->sects[i].type==type){
            out->p   = f->data+f->sects[i].offset;
            out->left= f->sects[i].length;
            out->err = false;
            return true;
        }
    }
    return false;
}

BCResult bc_read_imports(const char *path, char out_imps[][256], int *out_nimp){
    CHNOFile f; BCResult r=chno_open(path,&f); if(r!=BC_OK) return r;
    RBuf rb;
    if(!chno_section(&f,SECT_IMPORTS,&rb)){free(f.data);return BC_ERR_SECTION;}
    uint32_t n=rb_u32(&rb);
    if(out_nimp) *out_nimp=(int)n;
    for(uint32_t i=0;i<n;i++){
        int l=0; char *s=rb_str(&rb,&l);
        if(out_imps&&i<256){ strncpy(out_imps[i],s?s:"",255); out_imps[i][255]='\0'; }
        free(s);
    }
    free(f.data);
    return rb.err?BC_ERR_TRUNCATED:BC_OK;
}

BCResult bc_read_program(const char *path, Chunk *out_chunk,
                          FunctionObject ***out_funcs, int *out_nf,
                          char out_imps[][256], int *out_nimp){
    CHNOFile f; BCResult r=chno_open(path,&f); if(r!=BC_OK) return r;
    RBuf rb;

    
    if(chno_section(&f,SECT_IMPORTS,&rb)){
        uint32_t n=rb_u32(&rb);
        if(out_nimp) *out_nimp=(int)n;
        for(uint32_t i=0;i<n;i++){
            int l=0; char *s=rb_str(&rb,&l);
            if(out_imps&&i<256){ strncpy(out_imps[i],s?s:"",255); out_imps[i][255]='\0'; }
            free(s);
        }
    }

    
    int nf=0; FunctionObject **funcs=NULL;
    if(chno_section(&f,SECT_FUNCS,&rb)){
        nf=(int)rb_u32(&rb);
        funcs=(FunctionObject**)malloc((size_t)nf*sizeof(FunctionObject*));
        for(int i=0;i<nf;i++) funcs[i]=rb_codeobj(&rb,NULL);
    }

    
    chunk_init(out_chunk);
    if(chno_section(&f,SECT_MAIN,&rb)) rb_codeobj(&rb,out_chunk);

    free(f.data);
    *out_funcs=funcs; *out_nf=nf;
    return BC_OK;
}

BCResult bc_read_functions(const char *path, FunctionObject ***out_funcs, int *out_nf){
    CHNOFile f; BCResult r=chno_open(path,&f); if(r!=BC_OK) return r;
    RBuf rb;
    if(!chno_section(&f,SECT_FUNCS,&rb)){free(f.data);return BC_ERR_SECTION;}
    int nf=(int)rb_u32(&rb);
    FunctionObject **funcs=(FunctionObject**)malloc((size_t)nf*sizeof(FunctionObject*));
    for(int i=0;i<nf;i++) funcs[i]=rb_codeobj(&rb,NULL);
    free(f.data);
    *out_funcs=funcs; *out_nf=nf;
    return rb.err?BC_ERR_TRUNCATED:BC_OK;
}

BCResult bc_write_cco(const char *path,
                      Compiler **compilers,
                      const char **src_names,
                      int count)
{
    

    
    uint8_t **unit_data = (uint8_t**)calloc((size_t)count, sizeof(uint8_t*));
    uint32_t *unit_len  = (uint32_t*)calloc((size_t)count, sizeof(uint32_t));
    if(!unit_data || !unit_len){ free(unit_data); free(unit_len); return BC_ERR_OOM; }

    
    int    entry_unit_idx = -1;
    int valid_units = 0;

    for(int u = 0; u < count; u++){
        Compiler *C = compilers[u];
        bool has_entry = C->has_entry_main;
        if(has_entry && entry_unit_idx < 0) entry_unit_idx = u;

        
        WBuf payload = {0};
        SectInfo sects[10]; int nsects = 0;

        
        sects[nsects] = wb_section_begin(SECT_EXPORTS, &payload);
        int exported = 0;
        for(int i = 0; i < C->func_count; i++)
            if(C->functions[i]->exported) exported++;
        wb_u32(&payload, (uint32_t)exported);
        for(int i = 0; i < C->func_count; i++){
            if(!C->functions[i]->exported) continue;
            wb_cstr(&payload, C->functions[i]->name);
            wb_u16(&payload, (uint16_t)i);
        }
        wb_section_end(&sects[nsects++], &payload);

        
        sects[nsects] = wb_section_begin(SECT_FUNCS, &payload);
        wb_u32(&payload, (uint32_t)exported);
        for(int i = 0; i < C->func_count; i++)
            if(C->functions[i]->exported)
                wb_codeobj(&payload, &C->functions[i]->chunk, C->functions[i]);
        wb_section_end(&sects[nsects++], &payload);

        
        int internal_cnt = 0;
        for(int i = 0; i < C->func_count; i++)
            if(!C->functions[i]->exported) internal_cnt++;
        if(internal_cnt > 0){
            sects[nsects] = wb_section_begin(SECT_INTERNAL, &payload);
            wb_u32(&payload, (uint32_t)internal_cnt);
            for(int i = 0; i < C->func_count; i++)
                if(!C->functions[i]->exported)
                    wb_codeobj(&payload, &C->functions[i]->chunk, C->functions[i]);
            wb_section_end(&sects[nsects++], &payload);
        }

        

        if(C->imp_name_count > 0){
            sects[nsects] = wb_section_begin(SECT_IMPORTS, &payload);
            wb_u32(&payload, (uint32_t)C->imp_name_count);
            for(int ii = 0; ii < C->imp_name_count; ii++)
                wb_str(&payload, C->imp_names[ii], (int)strlen(C->imp_names[ii]));
            wb_section_end(&sects[nsects++], &payload);
        }

        
        if(has_entry){
            sects[nsects] = wb_section_begin(SECT_MAIN, &payload);
            wb_codeobj(&payload, &C->top_chunk, NULL);
            wb_section_end(&sects[nsects++], &payload);
        }

        
        WBuf chno = {0};
        uint16_t chno_flags = has_entry ? CHNO_PROG_FLAG : 0;
        wb_u32(&chno, CHNO_MAGIC);
        wb_u16(&chno, CHNO_VERSION);
        wb_u16(&chno, chno_flags);
        wb_u32(&chno, (uint32_t)nsects);
        size_t checksum_pos = chno.len;
        wb_u32(&chno, 0); 

        for(int i = 0; i < nsects; i++){
            wb_u8 (&chno, (uint8_t)sects[i].type);
            wb_u8 (&chno, 0);
            wb_u32(&chno, sects[i].offset);
            wb_u32(&chno, sects[i].length);
            wb_u16(&chno, (uint16_t)(adler32(
                payload.buf + sects[i].offset, sects[i].length) & 0xFFFF));
        }
        wb_grow(&chno, payload.len);
        memcpy(chno.buf + chno.len, payload.buf, payload.len);
        chno.len += payload.len;
        free(payload.buf);

        uint32_t cs = adler32(chno.buf + 16, chno.len - 16);
        chno.buf[checksum_pos+0]=(uint8_t)(cs&0xFF);   chno.buf[checksum_pos+1]=(uint8_t)((cs>>8)&0xFF);
        chno.buf[checksum_pos+2]=(uint8_t)((cs>>16)&0xFF); chno.buf[checksum_pos+3]=(uint8_t)((cs>>24)&0xFF);

        unit_data[u] = chno.buf;
        unit_len[u]  = (uint32_t)chno.len;
        valid_units++;
    }

    FILE *f = fopen(path, "wb");
    if(!f){
        for(int i = 0; i < count; i++) free(unit_data[i]);
        free(unit_data); free(unit_len);
        return BC_ERR_IO;
    }

    
    uint16_t cco_flags      = (entry_unit_idx >= 0) ? CCO_FLAG_HAS_ENTRY : 0;
    uint16_t cco_entry_unit = (entry_unit_idx >= 0) ? (uint16_t)entry_unit_idx : CCO_NO_ENTRY;
    uint8_t hdr[12];
    hdr[0]=(uint8_t)(CCO_MAGIC&0xFF);         hdr[1]=(uint8_t)((CCO_MAGIC>>8)&0xFF);
    hdr[2]=(uint8_t)((CCO_MAGIC>>16)&0xFF);   hdr[3]=(uint8_t)((CCO_MAGIC>>24)&0xFF);
    hdr[4]=(uint8_t)(CCO_VERSION&0xFF);        hdr[5]=(uint8_t)((CCO_VERSION>>8)&0xFF);
    hdr[6]=(uint8_t)(cco_flags&0xFF);          hdr[7]=(uint8_t)((cco_flags>>8)&0xFF);
    hdr[8]=(uint8_t)((uint16_t)count&0xFF);    hdr[9]=(uint8_t)(((uint16_t)count>>8)&0xFF);
    hdr[10]=(uint8_t)(cco_entry_unit&0xFF);    hdr[11]=(uint8_t)((cco_entry_unit>>8)&0xFF);
    fwrite(hdr, 1, 12, f);

    
    for(int u = 0; u < count; u++){
        const char *name = src_names[u] ? src_names[u] : "";
        uint16_t nlen = (uint16_t)strlen(name);
        uint8_t nl[2]; nl[0]=(uint8_t)(nlen&0xFF); nl[1]=(uint8_t)(nlen>>8);
        fwrite(nl, 1, 2, f);
        fwrite(name, 1, nlen, f);
        uint8_t dl[4];
        dl[0]=(uint8_t)(unit_len[u]&0xFF);       dl[1]=(uint8_t)((unit_len[u]>>8)&0xFF);
        dl[2]=(uint8_t)((unit_len[u]>>16)&0xFF); dl[3]=(uint8_t)((unit_len[u]>>24)&0xFF);
        fwrite(dl, 1, 4, f);
        fwrite(unit_data[u], 1, unit_len[u], f);
        free(unit_data[u]);
    }

    fclose(f);
    free(unit_data); free(unit_len);
    return BC_OK;
}

typedef struct {
    uint8_t  *data;
    long      fsz;
    uint16_t  ver;
    uint16_t  flags;       
    uint16_t  unit_count;
    uint16_t  entry_unit;  
    size_t    units_start; 
} CCOFile;

static BCResult cco_open(const char *path, CCOFile *out){
    FILE *f = fopen(path, "rb");
    if(!f) return BC_ERR_IO;
    fseek(f, 0, SEEK_END); long fsz = ftell(f); rewind(f);
    if(fsz < 8){ fclose(f); return BC_ERR_TRUNCATED; }
    uint8_t *data = (uint8_t*)malloc((size_t)fsz);
    if(!data){ fclose(f); return BC_ERR_OOM; }
    size_t nr = fread(data, 1, (size_t)fsz, f); fclose(f);
    if((long)nr != fsz){ free(data); return BC_ERR_IO; }

    uint32_t magic = (uint32_t)data[0]|((uint32_t)data[1]<<8)|
                     ((uint32_t)data[2]<<16)|((uint32_t)data[3]<<24);
    if(magic != CCO_MAGIC){ free(data); return BC_ERR_MAGIC; }

    uint16_t ver = (uint16_t)(data[4]|(data[5]<<8));
    if(ver != CCO_VERSION && ver != CCO_VERSION_V2 && ver != CCO_VERSION_LEGACY){ free(data); return BC_ERR_VERSION; }

    out->data = data; out->fsz = fsz; out->ver = ver;
    if(ver == CCO_VERSION_LEGACY){
        
        if(fsz < 8){ free(data); return BC_ERR_TRUNCATED; }
        out->flags      = 0;
        out->unit_count = (uint16_t)(data[6]|(data[7]<<8));
        out->entry_unit = CCO_NO_ENTRY;
        out->units_start = 8;
    } else {
        
        if(fsz < 12){ free(data); return BC_ERR_TRUNCATED; }
        out->flags      = (uint16_t)(data[6]|(data[7]<<8));
        out->unit_count = (uint16_t)(data[8]|(data[9]<<8));
        out->entry_unit = (uint16_t)(data[10]|(data[11]<<8));
        out->units_start = 12;
    }
    return BC_OK;
}

static void cco_load_unit_funcs(const uint8_t *blob, size_t blen,
                                 FunctionObject ***all, int *total, int *cap,
                                 char ***seen_names, int *seen_count, int *seen_cap)
{
    if(blen < 16) return;
    uint32_t bmagic = (uint32_t)blob[0]|((uint32_t)blob[1]<<8)|
                      ((uint32_t)blob[2]<<16)|((uint32_t)blob[3]<<24);
    if(bmagic != CHNO_MAGIC) return;
    
    _rb_chno_ver = (uint16_t)(blob[4]|(blob[5]<<8));
    uint32_t bnsects = (uint32_t)(blob[8]|(blob[9]<<8)|(blob[10]<<16)|(blob[11]<<24));
    if(bnsects > 16) bnsects = 16;
    size_t dir_start     = 16;
    size_t payload_start = dir_start + (size_t)bnsects * 12;
    if(payload_start > blen) return;

    uint32_t funcs_off = 0, funcs_len = 0; bool found = false;
    for(uint32_t si = 0; si < bnsects; si++){
        size_t de = dir_start + si * 12;
        if(de + 10 > blen) break;
        if(blob[de] == (uint8_t)SECT_FUNCS){
            funcs_off=(uint32_t)(blob[de+2]|(blob[de+3]<<8)|(blob[de+4]<<16)|(blob[de+5]<<24));
            funcs_len=(uint32_t)(blob[de+6]|(blob[de+7]<<8)|(blob[de+8]<<16)|(blob[de+9]<<24));
            found=true; break;
        }
    }
    if(!found || payload_start + funcs_off + funcs_len > blen) return;

    RBuf rb; rb.p = blob + payload_start + funcs_off; rb.left = funcs_len; rb.err = false;
    int unf = (int)rb_u32(&rb);
    for(int i = 0; i < unf && !rb.err; i++){
        FunctionObject *fn = rb_codeobj(&rb, NULL);
        if(!fn) continue;
        bool dup = false;
        for(int k = 0; k < *seen_count && !dup; k++)
            if(strcmp((*seen_names)[k], fn->name)==0) dup = true;
        if(dup) continue;
        if(*seen_count >= *seen_cap){
            *seen_cap = (*seen_cap < 16) ? 16 : (*seen_cap * 2);
            *seen_names = (char**)realloc(*seen_names, (size_t)(*seen_cap)*sizeof(char*));
        }
        (*seen_names)[(*seen_count)++] = strdup(fn->name);
        if(*total >= *cap){
            *cap = (*cap < 16) ? 16 : (*cap * 2);
            *all = (FunctionObject**)realloc(*all, (size_t)(*cap)*sizeof(FunctionObject*));
        }
        (*all)[(*total)++] = fn;
    }
}

static bool cco_load_unit_entry(const uint8_t *blob, size_t blen, Chunk *out_chunk){
    if(blen < 16) return false;
    uint32_t bmagic = (uint32_t)blob[0]|((uint32_t)blob[1]<<8)|
                      ((uint32_t)blob[2]<<16)|((uint32_t)blob[3]<<24);
    if(bmagic != CHNO_MAGIC) return false;
    _rb_chno_ver = (uint16_t)(blob[4]|(blob[5]<<8));
    uint32_t bnsects = (uint32_t)(blob[8]|(blob[9]<<8)|(blob[10]<<16)|(blob[11]<<24));
    if(bnsects > 16) bnsects = 16;
    size_t dir_start     = 16;
    size_t payload_start = dir_start + (size_t)bnsects * 12;
    if(payload_start > blen) return false;

    for(uint32_t si = 0; si < bnsects; si++){
        size_t de = dir_start + si * 12;
        if(de + 10 > blen) break;
        if(blob[de] == (uint8_t)SECT_MAIN){
            uint32_t soff=(uint32_t)(blob[de+2]|(blob[de+3]<<8)|(blob[de+4]<<16)|(blob[de+5]<<24));
            uint32_t slen=(uint32_t)(blob[de+6]|(blob[de+7]<<8)|(blob[de+8]<<16)|(blob[de+9]<<24));
            if(payload_start + soff + slen > blen) return false;
            RBuf rb; rb.p = blob + payload_start + soff; rb.left = slen; rb.err = false;
            rb_codeobj(&rb, out_chunk);
            return !rb.err;
        }
    }
    return false;
}

static void cco_load_unit_internal(const uint8_t *blob, size_t blen){
    if(blen < 16) return;
    uint32_t bmagic = (uint32_t)blob[0]|((uint32_t)blob[1]<<8)|
                      ((uint32_t)blob[2]<<16)|((uint32_t)blob[3]<<24);
    if(bmagic != CHNO_MAGIC) return;
    _rb_chno_ver = (uint16_t)(blob[4]|(blob[5]<<8));
    uint32_t bnsects = (uint32_t)(blob[8]|(blob[9]<<8)|(blob[10]<<16)|(blob[11]<<24));
    if(bnsects > 16) bnsects = 16;
    size_t dir_start     = 16;
    size_t payload_start = dir_start + (size_t)bnsects * 12;
    if(payload_start > blen) return;

    for(uint32_t si = 0; si < bnsects; si++){
        size_t de = dir_start + si * 12;
        if(de + 10 > blen) break;
        if(blob[de] == (uint8_t)SECT_INTERNAL){
            uint32_t soff=(uint32_t)(blob[de+2]|(blob[de+3]<<8)|(blob[de+4]<<16)|(blob[de+5]<<24));
            uint32_t slen=(uint32_t)(blob[de+6]|(blob[de+7]<<8)|(blob[de+8]<<16)|(blob[de+9]<<24));
            if(payload_start + soff + slen > blen) return;
            RBuf rb; rb.p = blob + payload_start + soff; rb.left = slen; rb.err = false;
            int nf = (int)rb_u32(&rb);
            for(int i = 0; i < nf && !rb.err; i++){
                FunctionObject *fn = rb_codeobj(&rb, NULL);
                if(fn) func_register(fn); 
            }
            return;
        }
    }
}

static void cco_load_unit_imports(const uint8_t *blob, size_t blen,
                                   char out_imps[][256], int *nimp, int max_imp,
                                   uint16_t chno_ver)
{
    if(blen < 16) return;
    uint32_t bmagic = (uint32_t)blob[0]|((uint32_t)blob[1]<<8)|
                      ((uint32_t)blob[2]<<16)|((uint32_t)blob[3]<<24);
    if(bmagic != CHNO_MAGIC) return;
    uint32_t bnsects = (uint32_t)(blob[8]|(blob[9]<<8)|(blob[10]<<16)|(blob[11]<<24));
    if(bnsects > 16) bnsects = 16;
    size_t dir_start     = 16;
    size_t payload_start = dir_start + (size_t)bnsects * 12;
    if(payload_start > blen) return;
    (void)chno_ver;

    for(uint32_t si = 0; si < bnsects; si++){
        size_t de = dir_start + si * 12;
        if(de + 10 > blen) break;
        if(blob[de] == (uint8_t)SECT_IMPORTS){
            uint32_t soff=(uint32_t)(blob[de+2]|(blob[de+3]<<8)|(blob[de+4]<<16)|(blob[de+5]<<24));
            uint32_t slen=(uint32_t)(blob[de+6]|(blob[de+7]<<8)|(blob[de+8]<<16)|(blob[de+9]<<24));
            if(payload_start + soff + slen > blen) return;
            RBuf rb; rb.p = blob + payload_start + soff; rb.left = slen; rb.err = false;
            uint32_t n = rb_u32(&rb);
            for(uint32_t i = 0; i < n && !rb.err; i++){
                int l = 0; char *s = rb_str(&rb, &l);
                if(s && *nimp < max_imp){
                    strncpy(out_imps[*nimp], s, 255);
                    out_imps[*nimp][255] = '\0';
                    (*nimp)++;
                }
                free(s);
            }
            return;
        }
    }
}

BCResult bc_read_cco_entry_imports(const char *path,
                                    char out_imps[][256], int *out_nimp){
    CCOFile cf; BCResult r = cco_open(path, &cf); if(r != BC_OK) return r;

    int nimp = 0;
    size_t pos = cf.units_start;

    for(int u = 0; u < (int)cf.unit_count; u++){
        if(pos + 2 > (size_t)cf.fsz) break;
        uint16_t nlen = (uint16_t)(cf.data[pos]|(cf.data[pos+1]<<8)); pos += 2;
        if(pos + nlen > (size_t)cf.fsz) break;
        pos += nlen;
        if(pos + 4 > (size_t)cf.fsz) break;
        uint32_t dlen = (uint32_t)(cf.data[pos]|(cf.data[pos+1]<<8)|
                                   (cf.data[pos+2]<<16)|(cf.data[pos+3]<<24)); pos += 4;
        if(pos + dlen > (size_t)cf.fsz) break;

        
        uint16_t unit_ver = 0;
        if(dlen >= 6)
            unit_ver = (uint16_t)(cf.data[pos+4]|(cf.data[pos+5]<<8));

        cco_load_unit_imports(cf.data + pos, dlen, out_imps, &nimp, 256, unit_ver);
        pos += dlen;
    }
    free(cf.data);
    if(out_nimp) *out_nimp = nimp;
    return BC_OK;
}

BCResult bc_read_cco(const char *path, FunctionObject ***out_funcs, int *out_nf){
    CCOFile cf; BCResult r = cco_open(path, &cf); if(r != BC_OK) return r;

    FunctionObject **all = NULL; int total = 0, cap = 0;
    char **seen = NULL; int seen_count = 0, seen_cap = 0;
    size_t pos = cf.units_start;

    for(int u = 0; u < (int)cf.unit_count; u++){
        if(pos + 2 > (size_t)cf.fsz) break;
        uint16_t nlen = (uint16_t)(cf.data[pos]|(cf.data[pos+1]<<8)); pos += 2;
        if(pos + nlen > (size_t)cf.fsz) break;
        pos += nlen;
        if(pos + 4 > (size_t)cf.fsz) break;
        uint32_t dlen = (uint32_t)(cf.data[pos]|(cf.data[pos+1]<<8)|
                                   (cf.data[pos+2]<<16)|(cf.data[pos+3]<<24)); pos += 4;
        if(pos + dlen > (size_t)cf.fsz) break;
        cco_load_unit_funcs(cf.data + pos, dlen, &all, &total, &cap, &seen, &seen_count, &seen_cap);
        
        cco_load_unit_internal(cf.data + pos, dlen);
        pos += dlen;
    }
    for(int i = 0; i < seen_count; i++) free(seen[i]);
    free(seen); free(cf.data);
    *out_funcs = all; *out_nf = total;
    return BC_OK;
}

BCResult bc_read_cco_entry(const char *path,
                            FunctionObject ***out_funcs, int *out_nf,
                            Chunk *out_chunk, bool *out_has_entry)
{
    CCOFile cf; BCResult r = cco_open(path, &cf); if(r != BC_OK) return r;

    *out_has_entry = false;
    bool want_entry = (cf.flags & CCO_FLAG_HAS_ENTRY) && (cf.entry_unit != CCO_NO_ENTRY);
    chunk_init(out_chunk);

    FunctionObject **all = NULL; int total = 0, cap = 0;
    char **seen = NULL; int seen_count = 0, seen_cap = 0;
    size_t pos = cf.units_start;

    for(int u = 0; u < (int)cf.unit_count; u++){
        if(pos + 2 > (size_t)cf.fsz) break;
        uint16_t nlen = (uint16_t)(cf.data[pos]|(cf.data[pos+1]<<8)); pos += 2;
        if(pos + nlen > (size_t)cf.fsz) break;
        pos += nlen;
        if(pos + 4 > (size_t)cf.fsz) break;
        uint32_t dlen = (uint32_t)(cf.data[pos]|(cf.data[pos+1]<<8)|
                                   (cf.data[pos+2]<<16)|(cf.data[pos+3]<<24)); pos += 4;
        if(pos + dlen > (size_t)cf.fsz) break;

        const uint8_t *blob = cf.data + pos;

        

        cco_load_unit_internal(blob, dlen);

        
        cco_load_unit_funcs(blob, dlen, &all, &total, &cap, &seen, &seen_count, &seen_cap);

        
        if(want_entry && u == (int)cf.entry_unit){
            if(cco_load_unit_entry(blob, dlen, out_chunk))
                *out_has_entry = true;
        }
        pos += dlen;
    }
    for(int i = 0; i < seen_count; i++) free(seen[i]);
    free(seen); free(cf.data);
    *out_funcs = all; *out_nf = total;
    if(want_entry && !*out_has_entry) return BC_ERR_CCO;
    return BC_OK;
}

