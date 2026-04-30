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

#include "gc.h"
#include "vm.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

GCState gc;

void chunk_init(Chunk *ch){ memset(ch,0,sizeof(Chunk)); }
void chunk_free(Chunk *ch){
    GC_FREE(ch->code,      (size_t)ch->code_cap);
    GC_FREE(ch->lines,     (size_t)ch->lines_cap*sizeof(ch->lines[0]));
    GC_FREE(ch->constants, (size_t)ch->const_cap*sizeof(Value));
    for(int i=0;i<ch->var_count;i++) free(ch->var_names[i]);
    GC_FREE(ch->var_names, (size_t)ch->var_cap*sizeof(char*));
    /* free co_varnames */
    for(int i=0;i<ch->varname_count;i++) free(ch->co_varnames[i]);
    GC_FREE(ch->co_varnames,(size_t)ch->varname_cap*sizeof(char*));
    memset(ch,0,sizeof(Chunk));
}

int chunk_add_line(Chunk *ch, int offset, int line){
    if(ch->lines_len>0 && ch->lines[ch->lines_len-1].line==line) return 0;
    if(ch->lines_len>=ch->lines_cap){
        int nc=ch->lines_cap<8?8:ch->lines_cap*2;
        ch->lines=(struct LineRun*)GC_GROW(ch->lines,
            (size_t)ch->lines_cap*sizeof(ch->lines[0]),
            (size_t)nc*sizeof(ch->lines[0]));
        ch->lines_cap=nc;
    }
    ch->lines[ch->lines_len].offset=offset;
    ch->lines[ch->lines_len].line=line;
    ch->lines_len++;
    return 0;
}

int chunk_line_at(Chunk *ch, int offset){
    int lo=0,hi=ch->lines_len-1,result=0;
    while(lo<=hi){
        int mid=(lo+hi)/2;
        if(ch->lines[mid].offset<=offset){ result=ch->lines[mid].line; lo=mid+1; }
        else hi=mid-1;
    }
    return result;
}

static uint64_t now_ns(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000000000ULL+(uint64_t)ts.tv_nsec;
}
static void record_pause(GCPauseStat *s, uint64_t elapsed_ns){
    s->total_ns+=elapsed_ns;
    s->count++;
    if(elapsed_ns>s->max_ns) s->max_ns=elapsed_ns;
    if(s->count==1||elapsed_ns<s->min_ns) s->min_ns=elapsed_ns;
}

static void gray_push(Obj *obj){
    if(!obj||obj->color!=GC_WHITE) return;
    obj->color=GC_GRAY;
    if(gc.gray.top>=gc.gray.cap){
        int nc=gc.gray.cap<64?64:gc.gray.cap*2;
        gc.gray.stack=(Obj**)realloc(gc.gray.stack,(size_t)nc*sizeof(Obj*));
        if(!gc.gray.stack){fprintf(stderr,"gc: gray stack oom\n");exit(1);}
        gc.gray.cap=nc;
    }
    gc.gray.stack[gc.gray.top++]=obj;
}

static void trace_obj(Obj *obj){
    if(!obj||obj->color==GC_BLACK) return;
    obj->color=GC_BLACK;
    switch(obj->type){
        case OBJ_ARRAY:{
            ObjArray *a=(ObjArray*)obj;
            for(int i=0;i<a->len;i++){
                Value v=a->items[i];
                if(v.type==VAL_STRING) gray_push((Obj*)v.as.string);
                else if(v.type==VAL_ARRAY) gray_push((Obj*)v.as.array);
                else if(v.type==VAL_DICT)  gray_push((Obj*)v.as.dict);
            }
            break;
        }
        case OBJ_DICT:{
            ObjDict *d=(ObjDict*)obj;
            for(int i=0;i<d->cap;i++){
                DictEntry *e=&d->entries[i];
                if(!e->key) continue;
                gray_push((Obj*)e->key);
                Value v=e->value;
                if(v.type==VAL_STRING) gray_push((Obj*)v.as.string);
                else if(v.type==VAL_ARRAY) gray_push((Obj*)v.as.array);
                else if(v.type==VAL_DICT)  gray_push((Obj*)v.as.dict);
            }
            break;
        }
        case OBJ_STRING: break;
    }
}

static uint32_t fnv1a(const char *data, int len){
    uint32_t h=2166136261u;
    for(int i=0;i<len;i++){ h^=(uint8_t)data[i]; h*=16777619u; }
    return h;
}

static ObjString *intern_find(const char *chars, int len, uint32_t hash){
    uint32_t idx=hash&(INTERN_CAP-1);
    for(int i=0;i<INTERN_CAP;i++){
        uint32_t slot=(idx+(uint32_t)i)&(INTERN_CAP-1);
        InternEntry *e=&gc.intern[slot];
        if(!e->used) return NULL;                       /* empty -> stop   */
        if(e->tombstone) continue;                      /* dead -> skip    */
        if(e->str && e->hash==hash && e->str->len==len
           && memcmp(e->str->chars,chars,(size_t)len)==0) return e->str;
    }
    return NULL;
}

static void intern_insert(ObjString *s){
    uint32_t idx=s->hash&(INTERN_CAP-1);
    /* First pass: look for tombstone to reuse */
    int tomb_slot=-1;
    for(int i=0;i<INTERN_CAP;i++){
        uint32_t slot=(idx+(uint32_t)i)&(INTERN_CAP-1);
        InternEntry *e=&gc.intern[slot];
        if(!e->used){ e->used=true; e->tombstone=false; e->str=s; e->hash=s->hash; return; }
        if(e->tombstone && tomb_slot<0) tomb_slot=(int)slot;
    }
    if(tomb_slot>=0){
        gc.intern[tomb_slot].tombstone=false;
        gc.intern[tomb_slot].str=s;
        gc.intern[tomb_slot].hash=s->hash;
    }
    /* table full - silently skip interning (string still works, just not deduped) */
}

/* called by sweep to weakly evict a collected string from intern table */
static void intern_evict(ObjString *s){
    uint32_t idx=s->hash&(INTERN_CAP-1);
    for(int i=0;i<INTERN_CAP;i++){
        uint32_t slot=(idx+(uint32_t)i)&(INTERN_CAP-1);
        InternEntry *e=&gc.intern[slot];
        if(!e->used) return;
        if(e->str==s){
            e->str=NULL;
            e->tombstone=true; /* mark dead; do NOT clear used so probing continues */
            return;
        }
    }
}



/* Sweep empty slabs - called after each major GC */
static void slab_sweep(void){
    StringSlab **link=&gc.slab_list;
    while(*link){
        StringSlab *sl=*link;
        if(sl->live<=0){
            *link=sl->next;
            if(gc.slab_head==sl) gc.slab_head=gc.slab_list;
            gc.bytes_allocated=(gc.bytes_allocated>SLAB_SIZE)?gc.bytes_allocated-SLAB_SIZE:0;
            gc.total_freed+=SLAB_SIZE;
            free(sl->mem); free(sl);
        } else {
            link=&sl->next;
        }
    }
}

static Obj *alloc_obj(ObjType type, size_t size){
    Obj *obj=(Obj*)GC_ALLOC(size);
    obj->type=type;
    obj->color=GC_WHITE;
    obj->generation=GEN_YOUNG;
    obj->age=0;
    obj->pin_count=0;
    obj->next=gc.objects; gc.objects=obj;
    obj->gen_next=gc.young_list; gc.young_list=obj;
    return obj;
}

ObjString *gc_string(const char *chars, int len){
    if(!chars) chars="";
    uint32_t hash=fnv1a(chars,len);

    /* Intern lookup */
    ObjString *existing=intern_find(chars,len,hash);
    if(existing) return existing;

    /* Allocate header always from heap; chars from slab if small enough */
    ObjString *s=(ObjString*)GC_ALLOC(sizeof(ObjString)+(size_t)(len<=SLAB_STR_MAX?0:len+1));
    s->header.type=OBJ_STRING;
    s->header.color=GC_WHITE;
    s->header.generation=GEN_YOUNG;
    s->header.age=0;
    s->header.pin_count=0;
    s->header.next=gc.objects;     gc.objects=(Obj*)s;
    s->header.gen_next=gc.young_list; gc.young_list=(Obj*)s;
    s->len=len;
    s->hash=hash;

    if(len<=SLAB_STR_MAX){
        /* chars live in slab - header has no inline flexible array */
        s->slab_alloc=true;
        /* We need to store chars somewhere accessible via s->chars.
         * Since the flexible array has size 0 in this path, we use a
         * trick: allocate a slab region and store its pointer after
         * the header using a separate indirection stored in chars[].
         * Simpler: just always allocate header + chars together but
         * still track which slab they came from.
         *
         * Actual approach: for slab strings we DO allocate the full
         * sizeof(ObjString)+(len+1) from the slab for the chars portion
         * only, then memcpy.  The header itself is on the GC heap.
         * We store a pointer to the slab region in a sentinel right
         * after the header via a char* overlay.
         *
         * Simplest correct approach: just use a 2-field struct overlay.
         */
        /* Re-allocate properly: header + len+1 bytes, but mark slab_alloc
         * for accounting so slab_release_one is called at free.
         * For v3.2 we keep it simple: header always malloc'd via GC_ALLOC.
         * Slab tracking is per-header to count live objects in each slab.
         * The actual char data is carved from the slab for cache locality. */

        /* Grow the header to include a char* pointer after the flexible array */
        /* simplest: just embed the chars in the header normally but record slab */
        /* We need to re-do this: free the above partial alloc and redo with chars */
        /* Remove from object list since we're re-allocating */
        gc.objects=s->header.next;
        gc.young_list=s->header.gen_next;
        /* Adjust accounting */
        gc.bytes_allocated-=sizeof(ObjString);
        gc.total_allocated-=sizeof(ObjString);

        /* Allocate full object - but record it as slab for stats */
        s=(ObjString*)GC_ALLOC(sizeof(ObjString)+(size_t)(len+1));
        s->header.type=OBJ_STRING;
        s->header.color=GC_WHITE;
        s->header.generation=GEN_YOUNG;
        s->header.age=0;
        s->header.pin_count=0;
        s->header.next=gc.objects;     gc.objects=(Obj*)s;
        s->header.gen_next=gc.young_list; gc.young_list=(Obj*)s;
        s->len=len;
        s->hash=hash;
        /* v3.3 fix: chars live inside the GC-heap allocation (the slab carving
         * of chars was abandoned in the original implementation but the live-count
         * increment was left behind).  slab_release_one() scans slab memory
         * ranges and would never find heap-allocated chars, so the live counter
         * was incremented on creation but never decremented - slabs were never
         * freed.  Mark these strings as non-slab so the accounting is correct. */
        s->slab_alloc=false;
    } else {
        s->slab_alloc=false;
    }

    if(len>0) memcpy(s->chars,chars,(size_t)len);
    s->chars[len]='\0';
    intern_insert(s);
    gc.live_strings++;
    return s;
}

ObjString *gc_string_own(char *heap_str, int len){
    ObjString *s=gc_string(heap_str,len);
    free(heap_str); return s;
}
ObjString *gc_cstring(const char *cstr){
    return cstr?gc_string(cstr,(int)strlen(cstr)):gc_string("",0);
}

ObjArray *gc_array(void){
    ObjArray *a=(ObjArray*)alloc_obj(OBJ_ARRAY,sizeof(ObjArray));
    a->items=NULL; a->len=0; a->cap=0;
    gc.live_arrays++;
    return a;
}
ObjDict *gc_dict(void){
    ObjDict *d=(ObjDict*)alloc_obj(OBJ_DICT,sizeof(ObjDict));
    d->entries=NULL; d->count=0; d->cap=0;
    gc.live_dicts++;
    return d;
}

void gc_arr_push(ObjArray *a, Value v){
    if(a->len>=a->cap){
        int nc=a->cap<8?8:a->cap*2;
        a->items=(Value*)GC_GROW(a->items,(size_t)a->cap*sizeof(Value),(size_t)nc*sizeof(Value));
        a->cap=nc;
    }
    a->items[a->len++]=v;
}

static void dict_grow(ObjDict *d){
    int new_cap=d->cap<8?8:d->cap*2;
    DictEntry *ne=(DictEntry*)GC_ALLOC((size_t)new_cap*sizeof(DictEntry));
    memset(ne,0,(size_t)new_cap*sizeof(DictEntry));
    for(int i=0;i<d->cap;i++){
        DictEntry *e=&d->entries[i];
        if(!e->key) continue;
        uint32_t idx=e->key->hash&(uint32_t)(new_cap-1);
        while(ne[idx].key) idx=(idx+1)&(uint32_t)(new_cap-1);
        ne[idx]=*e;
    }
    if(d->entries) GC_FREE(d->entries,(size_t)d->cap*sizeof(DictEntry));
    d->entries=ne; d->cap=new_cap;
}
void gc_dict_set(ObjDict *d, ObjString *key, Value val){
    if(!d->entries||d->count+1>(d->cap*3/4)) dict_grow(d);
    uint32_t idx=key->hash&(uint32_t)(d->cap-1);
    while(d->entries[idx].key && d->entries[idx].key!=key)
        idx=(idx+1)&(uint32_t)(d->cap-1);
    if(!d->entries[idx].key) d->count++;
    GC_WRITE_BARRIER(&d->header,STRING_VAL(key));
    GC_WRITE_BARRIER(&d->header,val);
    d->entries[idx].key=key; d->entries[idx].value=val;
}
Value gc_dict_get(ObjDict *d, ObjString *key){
    if(!d->entries||d->cap==0) return NIL_VAL;
    uint32_t idx=key->hash&(uint32_t)(d->cap-1);
    for(int i=0;i<d->cap;i++){
        DictEntry *e=&d->entries[(idx+i)&(uint32_t)(d->cap-1)];
        if(!e->key) return NIL_VAL;
        if(e->key==key) return e->value;
    }
    return NIL_VAL;
}
bool gc_dict_delete(ObjDict *d, ObjString *key){
    if(!d->entries||d->cap==0) return false;
    uint32_t idx=key->hash&(uint32_t)(d->cap-1);
    for(int i=0;i<d->cap;i++){
        DictEntry *e=&d->entries[(idx+i)&(uint32_t)(d->cap-1)];
        if(!e->key) return false;
        if(e->key==key){ e->key=NULL; e->value=NIL_VAL; d->count--; return true; }
    }
    return false;
}

void gc_mark_obj(Obj *obj){ gray_push(obj); }
void gc_mark_value(Value v){
    if      (v.type==VAL_STRING) gray_push((Obj*)v.as.string);
    else if (v.type==VAL_ARRAY)  gray_push((Obj*)v.as.array);
    else if (v.type==VAL_DICT)   gray_push((Obj*)v.as.dict);
}
void gc_barrier_slow(Value v){ if(gc.marking) gc_mark_value(v); }

void gc_pin(Obj *obj){
    if(!obj) return;
    if(obj->pin_count==0){
        /* Move to pinned list */
        obj->generation=GEN_PINNED;
        obj->gen_next=gc.pinned_list;
        gc.pinned_list=obj;
    }
    if(obj->pin_count<0xFFFF) obj->pin_count++;
}
void gc_unpin(Obj *obj){
    if(!obj||obj->pin_count==0) return;
    obj->pin_count--;
    if(obj->pin_count==0){
        /* Demote back to old generation so normal GC can collect it */
        obj->generation=GEN_OLD;
        /* Remove from pinned_list - linear scan but pinned objects are rare */
        Obj **link=&gc.pinned_list;
        while(*link){ if(*link==obj){ *link=obj->gen_next; break; } link=&(*link)->gen_next; }
        obj->gen_next=gc.old_list; gc.old_list=obj;
    }
}

void gc_pressure_hint(void *vm_ptr){
    if(!vm_ptr||gc.paused) return;
    /* Only run if we're at -75% of the next young threshold */
    if(gc.bytes_allocated >= gc.next_gc_young*3/4) gc_collect(vm_ptr);
}

static void mark_vm_roots(VM *vm){
    for(int i=0;i<vm->stack_top;i++) gc_mark_value(vm->stack[i]);
    for(int i=0;i<vm->global_count;i++) gc_mark_value(vm->globals[i]);
    for(int i=0;i<vm->frame_count;i++){
        CallFrame *f=&vm->frames[i];
        Chunk *ch=f->function?&f->function->chunk:vm->top_chunk;
        if(!ch) continue;
        for(int c=0;c<ch->const_count;c++) gc_mark_value(ch->constants[c]);
    }
    if(vm->top_chunk)
        for(int c=0;c<vm->top_chunk->const_count;c++)
            gc_mark_value(vm->top_chunk->constants[c]);
    gc_mark_value(vm->error_value);
    /* Pinned objects are always reachable */
    for(Obj *o=gc.pinned_list;o;o=o->gen_next) gray_push(o);
}

static void free_obj(Obj *obj){
    size_t sz=0;
    switch(obj->type){
        case OBJ_STRING:{
            ObjString *s=(ObjString*)obj;
            intern_evict(s);   /* weak eviction, not hard remove */
            /* slab_alloc is always false (slab carving was never
             * implemented); chars are always embedded in the GC allocation. */
            sz=sizeof(ObjString)+(size_t)(s->len+1);
            gc.live_strings--;
            break;
        }
        case OBJ_ARRAY:{
            ObjArray *a=(ObjArray*)obj;
            if(a->items) GC_FREE(a->items,(size_t)a->cap*sizeof(Value));
            sz=sizeof(ObjArray); gc.live_arrays--; break;
        }
        case OBJ_DICT:{
            ObjDict *d=(ObjDict*)obj;
            if(d->entries) GC_FREE(d->entries,(size_t)d->cap*sizeof(DictEntry));
            sz=sizeof(ObjDict); gc.live_dicts--; break;
        }
    }
    GC_FREE(obj,sz);
}

static void sweep_generation(bool major){
    Obj **link=&gc.objects;
    while(*link){
        Obj *obj=*link;
        /* Never sweep pinned objects */
        if(obj->generation==GEN_PINNED){ link=&obj->next; continue; }
        /* In a minor GC, skip old-gen objects */
        if(!major&&obj->generation==GEN_OLD){
            obj->color=GC_WHITE; link=&obj->next; continue;
        }
        if(obj->color==GC_BLACK){
            if(obj->generation==GEN_YOUNG){
                obj->age++;
                if(obj->age>=GC_PROMOTE_AGE) obj->generation=GEN_OLD;
            }
            obj->color=GC_WHITE; link=&obj->next;
        } else {
            *link=obj->next;
            free_obj(obj);
        }
    }
    /* Rebuild per-generation lists */
    gc.young_list=NULL; gc.old_list=NULL;
    for(Obj *o=gc.objects;o;o=o->next){
        if(o->generation==GEN_YOUNG){ o->gen_next=gc.young_list; gc.young_list=o; }
        else if(o->generation==GEN_OLD){ o->gen_next=gc.old_list; gc.old_list=o; }
    }
    gc.remset.count=0;
    if(major) slab_sweep(); /* reclaim empty slabs after major GC */
}

void gc_init(void){
    memset(&gc,0,sizeof(GCState));
    gc.next_gc_young=GC_YOUNG_THRESHOLD;
    gc.next_gc_major=GC_MAJOR_THRESHOLD;
    gc.pause_minor.min_ns=UINT64_MAX;
    gc.pause_major.min_ns=UINT64_MAX;
}

void gc_collect(void *vm_ptr){
    if(gc.paused||!vm_ptr) return;
    gc.paused=true;
    gc.marking=true;

    bool major=gc.major_pending
            ||(gc.bytes_allocated>gc.next_gc_major)
            ||((gc.minor_collections%GC_MAJOR_EVERY)==0&&gc.minor_collections>0);

    uint64_t t0=now_ns();

    mark_vm_roots((VM*)vm_ptr);
    if(!major)
        for(int i=0;i<gc.remset.count;i++) gray_push(gc.remset.entries[i]);
    while(gc.gray.top>0) trace_obj(gc.gray.stack[--gc.gray.top]);
    gc.marking=false;

    sweep_generation(major);

    uint64_t elapsed=now_ns()-t0;
    if(major){ record_pause(&gc.pause_major,elapsed); gc.major_collections++; gc.major_pending=false; }
    else      { record_pause(&gc.pause_minor,elapsed); gc.minor_collections++; }

    size_t ba=gc.bytes_allocated;
    gc.next_gc_young=ba<GC_YOUNG_THRESHOLD?GC_YOUNG_THRESHOLD:ba*GC_GROWTH_FACTOR;
    gc.next_gc_major=ba<(GC_MAJOR_THRESHOLD/2)?GC_MAJOR_THRESHOLD:ba*GC_GROWTH_FACTOR*2;
    gc.paused=false;
}

void gc_step(void *vm_ptr){
    if(gc.paused||!vm_ptr) return;
    if(gc.marking&&gc.gray.top>0){
        int n=GC_STEP_SIZE;
        while(gc.gray.top>0&&n-->0) trace_obj(gc.gray.stack[--gc.gray.top]);
        if(gc.gray.top==0){
            gc.marking=false;
            gc.paused=true;
            uint64_t t0=now_ns();
            sweep_generation(gc.major_pending);
            record_pause(&gc.pause_minor,now_ns()-t0);
            size_t ba=gc.bytes_allocated;
            gc.next_gc_young=ba<GC_YOUNG_THRESHOLD?GC_YOUNG_THRESHOLD:ba*GC_GROWTH_FACTOR;
            gc.minor_collections++;
            gc.major_pending=false;
            gc.paused=false;
        }
        return;
    }
    if(gc.bytes_allocated>gc.next_gc_major){
        gc_collect(vm_ptr);
    } else if(gc.bytes_allocated>gc.next_gc_young){
        gc.marking=true; gc.paused=true;
        mark_vm_roots((VM*)vm_ptr);
        for(int i=0;i<gc.remset.count;i++) gray_push(gc.remset.entries[i]);
        gc.paused=false;
        int n=GC_STEP_SIZE;
        while(gc.gray.top>0&&n-->0) trace_obj(gc.gray.stack[--gc.gray.top]);
        if(gc.gray.top==0){
            gc.marking=false; gc.paused=true;
            uint64_t t0=now_ns();
            sweep_generation(false);
            record_pause(&gc.pause_minor,now_ns()-t0);
            size_t ba=gc.bytes_allocated;
            gc.next_gc_young=ba<GC_YOUNG_THRESHOLD?GC_YOUNG_THRESHOLD:ba*GC_GROWTH_FACTOR;
            gc.minor_collections++;
            gc.paused=false;
        }
    }
}

void gc_free_all(void){
    Obj *obj=gc.objects;
    while(obj){ Obj *next=obj->next; free_obj(obj); obj=next; }
    gc.objects=gc.young_list=gc.old_list=gc.pinned_list=NULL;
    free(gc.gray.stack); gc.gray.stack=NULL; gc.gray.top=gc.gray.cap=0;
    /* Free all slabs */
    StringSlab *sl=gc.slab_list;
    while(sl){ StringSlab *nx=sl->next; free(sl->mem); free(sl); sl=nx; }
    gc.slab_list=gc.slab_head=NULL;
    memset(gc.intern,0,sizeof(gc.intern));
    gc.remset.count=0;
    gc.bytes_allocated=0;
    gc.live_strings=gc.live_arrays=gc.live_dicts=0;
}

void gc_print_stats(void){
    /* Count slabs */
    int slab_count=0; size_t slab_bytes=0;
    for(StringSlab *sl=gc.slab_list;sl;sl=sl->next){ slab_count++; slab_bytes+=SLAB_SIZE; }

    uint64_t minor_avg = gc.pause_minor.count>0
        ? gc.pause_minor.total_ns/gc.pause_minor.count : 0;
    uint64_t major_avg = gc.pause_major.count>0
        ? gc.pause_major.total_ns/gc.pause_major.count : 0;

    fprintf(stderr,
        "[GC v3.7]\n"
        "  allocated   : %.1f KB\n"
        "  total alloc : %.1f KB  freed: %.1f KB\n"
        "  objects     : strings=%d  arrays=%d  dicts=%d\n"
        "  generations : minor=%d  major=%d\n"
        "  slabs       : count=%d  size=%.1f KB\n"
        "  pause minor : avg=%llu us  max=%llu us  count=%u\n"
        "  pause major : avg=%llu us  max=%llu us  count=%u\n",
        (double)gc.bytes_allocated/1024.0,
        (double)gc.total_allocated/1024.0,
        (double)gc.total_freed/1024.0,
        gc.live_strings, gc.live_arrays, gc.live_dicts,
        gc.minor_collections, gc.major_collections,
        slab_count, (double)slab_bytes/1024.0,
        (unsigned long long)minor_avg/1000ULL,
        (unsigned long long)(gc.pause_minor.max_ns>0?gc.pause_minor.max_ns:0)/1000ULL,
        gc.pause_minor.count,
        (unsigned long long)major_avg/1000ULL,
        (unsigned long long)(gc.pause_major.max_ns>0?gc.pause_major.max_ns:0)/1000ULL,
        gc.pause_major.count);
}
