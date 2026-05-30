

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdlib.h>  

char **g_runtime_argv = NULL;
int    g_runtime_argc = 0;
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "native.h"
#include "vm.h"
#include "error.h"
#include "gc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>

#define N_PUSH(v)  do { vm->stack[vm->stack_top++] = (v); } while(0)
#define N_POP()    (vm->stack[--vm->stack_top])
#define N_PEEK(n)  (vm->stack[vm->stack_top-1-(n)])

#define STR(v)     (IS_STRING(v) ? AS_STRING(v)->chars : "")
#define NUM(v)     (IS_NUMBER(v) ? AS_NUMBER(v) : 0.0)

static void pop_args(VM *vm, uint8_t argc, Value *args) {
    for (int i = argc - 1; i >= 0; i--)
        args[i] = N_POP();
}

static ObjArray *new_arr(void) { return gc_array(); }

static Value sv(const char *s) {
    if (!s) return NIL_VAL;
    return STRING_VAL(gc_cstring(s));
}

#define STR_GROW_INIT 4096
typedef struct { char *buf; size_t cap; size_t len; } StrBuf;

static void str_buf_init(StrBuf *b) {
    b->buf = (char*)malloc(STR_GROW_INIT);
    if(b->buf) b->buf[0] = '\0';
    b->cap = STR_GROW_INIT; b->len = 0;
}
static void str_buf_free(StrBuf *b) { free(b->buf); b->buf = NULL; b->cap = b->len = 0; }
static void str_buf_append(StrBuf *b, const char *s, size_t sl) {
    if(b->len + sl + 1 > b->cap) {
        b->cap = (b->len + sl + 1) * 2;
        b->buf = (char*)realloc(b->buf, b->cap);
    }
    memcpy(b->buf + b->len, s, sl);
    b->len += sl;
    b->buf[b->len] = '\0';
}
static void str_buf_push(StrBuf *b, const char *s) { str_buf_append(b, s, strlen(s)); }
static void str_buf_char(StrBuf *b, char c) { str_buf_append(b, &c, 1); }

static void str_value_recursive(StrBuf *b, Value v, int depth) {
    char nb[64];
    if(depth > 8) { str_buf_push(b, "..."); return; }
    switch(v.type) {
        case VAL_STRING:
            str_buf_char(b, '"');
            str_buf_push(b, AS_STRING(v)->chars);
            str_buf_char(b, '"');
            break;
        case VAL_NUMBER:
            if(v.as.number == (long long)v.as.number) snprintf(nb,sizeof(nb),"%lld",(long long)v.as.number);
            else snprintf(nb,sizeof(nb),"%.14g",v.as.number);
            str_buf_push(b, nb); break;
        case VAL_BOOL:   str_buf_push(b, v.as.boolean ? "true" : "false"); break;
        case VAL_NIL:    str_buf_push(b, "nil"); break;
        case VAL_FUNCTION:
            snprintf(nb, sizeof(nb), "<func %s>", v.as.function->name);
            str_buf_push(b, nb); break;
        case VAL_ARRAY: {
            ObjArray *a = v.as.array;
            str_buf_char(b, '[');
            for(int i = 0; i < a->len; i++) {
                if(i) str_buf_push(b, ", ");
                str_value_recursive(b, a->items[i], depth + 1);
            }
            str_buf_char(b, ']'); break;
        }
        case VAL_DICT: {
            ObjDict *d = v.as.dict;
            str_buf_char(b, '{');
            bool first = true;
            for(int i = 0; i < d->cap; i++) {
                DictEntry *e = &d->entries[i];
                if(!e->key) continue;
                if(!first) str_buf_push(b, ", ");
                first = false;
                str_buf_char(b, '"');
                str_buf_push(b, e->key->chars);
                str_buf_push(b, "\": ");
                str_value_recursive(b, e->value, depth + 1);
            }
            str_buf_char(b, '}'); break;
        }
        default: str_buf_push(b, "<value>"); break;
    }
}

void native_dispatch(VM *vm, uint16_t id, uint8_t argc) {
    Value args[16];
    if (argc > 16) argc = 16;
    pop_args(vm, argc, args);

    switch (id) {

    
    case NATIVE_OS_TIME: {
        
        N_PUSH(NUMBER_VAL((double)time(NULL)));
        break;
    }
    case NATIVE_OS_CLOCK: {
        
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        double t = (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
        N_PUSH(NUMBER_VAL(t));
        break;
    }
    case NATIVE_OS_SLEEP: {
        
        double ms = argc >= 1 ? NUM(args[0]) : 0;
        struct timespec req;
        req.tv_sec  = (time_t)(ms / 1000.0);
        req.tv_nsec = (long)(fmod(ms, 1000.0) * 1000000.0);
        nanosleep(&req, NULL);
        N_PUSH(NIL_VAL);
        break;
    }
    case NATIVE_OS_EXIT: {
        int code = argc >= 1 ? (int)NUM(args[0]) : 0;
        exit(code);
        break;  
    }
    case NATIVE_OS_GETENV: {
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(NIL_VAL); break; }
        const char *val = getenv(STR(args[0]));
        N_PUSH(val ? sv(val) : NIL_VAL);
        break;
    }
    case NATIVE_OS_ARGS: {
        

        ObjArray *arr = new_arr();
        for (int i = 0; i < g_runtime_argc; i++)
            gc_arr_push(arr, sv(g_runtime_argv[i]));
        N_PUSH(ARRAY_VAL(arr));
        break;
    }
    case NATIVE_OS_PLATFORM: {
#if defined(_WIN32)
        N_PUSH(sv("windows"));
#elif defined(__APPLE__)
        N_PUSH(sv("mac"));
#else
        N_PUSH(sv("linux"));
#endif
        break;
    }
    case NATIVE_OS_HOSTNAME: {
        char buf[256];
        if (gethostname(buf, sizeof(buf)) == 0)
            N_PUSH(sv(buf));
        else
            N_PUSH(sv("unknown"));
        break;
    }
    case NATIVE_OS_PID: {
        N_PUSH(NUMBER_VAL((double)getpid()));
        break;
    }
    case NATIVE_OS_SYSTEM: {
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(NUMBER_VAL(-1)); break; }
        int rc = system(STR(args[0]));
        N_PUSH(NUMBER_VAL((double)rc));
        break;
    }

    
    case NATIVE_FILE_READ: {
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(NIL_VAL); break; }
        FILE *fp = fopen(STR(args[0]), "rb");
        if (!fp) { N_PUSH(NIL_VAL); break; }
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        rewind(fp);
        if (sz < 0 || sz > 64*1024*1024) { fclose(fp); N_PUSH(NIL_VAL); break; }
        char *buf = (char*)malloc(sz + 1);
        if (!buf) { fclose(fp); N_PUSH(NIL_VAL); break; }
        size_t nread = fread(buf, 1, (size_t)sz, fp);
        buf[nread] = '\0';
        fclose(fp);
        N_PUSH(sv(buf));
        free(buf);
        break;
    }
    case NATIVE_FILE_WRITE: {
        if (argc < 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
            N_PUSH(BOOL_VAL(false)); break;
        }
        FILE *fp = fopen(STR(args[0]), "w");
        if (!fp) { N_PUSH(BOOL_VAL(false)); break; }
        fputs(STR(args[1]), fp);
        fclose(fp);
        N_PUSH(BOOL_VAL(true));
        break;
    }
    case NATIVE_FILE_APPEND: {
        if (argc < 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
            N_PUSH(BOOL_VAL(false)); break;
        }
        FILE *fp = fopen(STR(args[0]), "a");
        if (!fp) { N_PUSH(BOOL_VAL(false)); break; }
        fputs(STR(args[1]), fp);
        fclose(fp);
        N_PUSH(BOOL_VAL(true));
        break;
    }
    case NATIVE_FILE_EXISTS: {
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(BOOL_VAL(false)); break; }
        struct stat st;
        N_PUSH(BOOL_VAL(stat(STR(args[0]), &st) == 0));
        break;
    }
    case NATIVE_FILE_DELETE: {
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(BOOL_VAL(false)); break; }
        N_PUSH(BOOL_VAL(remove(STR(args[0])) == 0));
        break;
    }
    case NATIVE_FILE_SIZE: {
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(NUMBER_VAL(-1)); break; }
        struct stat st;
        if (stat(STR(args[0]), &st) != 0) { N_PUSH(NUMBER_VAL(-1)); break; }
        N_PUSH(NUMBER_VAL((double)st.st_size));
        break;
    }
    case NATIVE_FILE_LINES: {
        ObjArray *arr = new_arr();
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(ARRAY_VAL(arr)); break; }
        FILE *fp = fopen(STR(args[0]), "r");
        if (!fp) { N_PUSH(ARRAY_VAL(arr)); break; }
        char line[4096];
        while (fgets(line, sizeof(line), fp)) {
            
            int len = strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
                line[--len] = '\0';
            gc_arr_push(arr, sv(line));
        }
        fclose(fp);
        N_PUSH(ARRAY_VAL(arr));
        break;
    }
    case NATIVE_DIR_LIST: {
        const char *path = (argc >= 1 && IS_STRING(args[0])) ? STR(args[0]) : ".";
        ObjArray *arr = new_arr();
        DIR *d = opendir(path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d))) {
                if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
                gc_arr_push(arr, sv(e->d_name));
            }
            closedir(d);
        }
        N_PUSH(ARRAY_VAL(arr));
        break;
    }
    case NATIVE_DIR_MAKE: {
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(BOOL_VAL(false)); break; }
        
        char path[1024];
        strncpy(path, STR(args[0]), sizeof(path)-1);
        for (char *p = path + 1; *p; p++) {
            if (*p == '/') {
                *p = '\0';
                mkdir(path, 0755);
                *p = '/';
            }
        }
        int r = mkdir(path, 0755);
        N_PUSH(BOOL_VAL(r == 0 || errno == EEXIST));
        break;
    }
    case NATIVE_FILE_COPY: {
        if (argc < 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
            N_PUSH(BOOL_VAL(false)); break;
        }
        FILE *src = fopen(STR(args[0]), "rb");
        if (!src) { N_PUSH(BOOL_VAL(false)); break; }
        FILE *dst = fopen(STR(args[1]), "wb");
        if (!dst) { fclose(src); N_PUSH(BOOL_VAL(false)); break; }
        char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
            fwrite(buf, 1, n, dst);
        fclose(src); fclose(dst);
        N_PUSH(BOOL_VAL(true));
        break;
    }

    
    case NATIVE_BIN_WRITE: {
        
        if (argc < 2 || !IS_STRING(args[0]) || !IS_ARRAY(args[1])) {
            N_PUSH(BOOL_VAL(false)); break;
        }
        FILE *fp = fopen(STR(args[0]), "wb");
        if (!fp) { N_PUSH(BOOL_VAL(false)); break; }
        ObjArray *arr = AS_ARRAY(args[1]);
        for (int i = 0; i < arr->len; i++) {
            if (IS_NUMBER(arr->items[i])) {
                unsigned char b = (unsigned char)((int)AS_NUMBER(arr->items[i]) & 0xFF);
                fwrite(&b, 1, 1, fp);
            }
        }
        fclose(fp);
        N_PUSH(BOOL_VAL(true));
        break;
    }
    case NATIVE_BIN_READ: {
        
        if (argc < 1 || !IS_STRING(args[0])) { N_PUSH(ARRAY_VAL(new_arr())); break; }
        FILE *fp = fopen(STR(args[0]), "rb");
        if (!fp) { N_PUSH(ARRAY_VAL(new_arr())); break; }
        ObjArray *arr = new_arr();
        unsigned char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
            for (size_t i = 0; i < n; i++)
                gc_arr_push(arr, NUMBER_VAL((double)buf[i]));
        }
        fclose(fp);
        N_PUSH(ARRAY_VAL(arr));
        break;
    }
    case NATIVE_BIN_WRITE_NUM: {
        
        if (argc < 3 || !IS_STRING(args[0]) || !IS_ARRAY(args[1]) || !IS_NUMBER(args[2])) {
            N_PUSH(BOOL_VAL(false)); break;
        }
        int bits = (int)AS_NUMBER(args[2]);
        if (bits != 8 && bits != 16 && bits != 32 && bits != 64) bits = 8;
        int bytes = bits / 8;
        FILE *fp = fopen(STR(args[0]), "wb");
        if (!fp) { N_PUSH(BOOL_VAL(false)); break; }
        ObjArray *arr = AS_ARRAY(args[1]);
        for (int i = 0; i < arr->len; i++) {
            if (IS_NUMBER(arr->items[i])) {
                long long v = (long long)AS_NUMBER(arr->items[i]);
                unsigned char buf[8];
                for (int b = 0; b < bytes; b++)
                    buf[b] = (unsigned char)((v >> (8*b)) & 0xFF);
                fwrite(buf, 1, bytes, fp);
            }
        }
        fclose(fp);
        N_PUSH(BOOL_VAL(true));
        break;
    }

    case NATIVE_RANGE: {
        
        int n_start=0, n_stop=0, n_step=1;
        if(argc==1){ n_stop=(int)NUM(args[0]); }
        else if(argc>=2){ n_start=(int)NUM(args[0]); n_stop=(int)NUM(args[1]); }
        if(argc>=3){ n_step=(int)NUM(args[2]); }
        if(n_step==0){ N_PUSH(ARRAY_VAL(new_arr())); break; }
        ObjArray *rarr=new_arr();
        if(n_step>0){ for(int i=n_start;i<n_stop;i+=n_step) gc_arr_push(rarr,NUMBER_VAL((double)i)); }
        else         { for(int i=n_start;i>n_stop;i+=n_step) gc_arr_push(rarr,NUMBER_VAL((double)i)); }
        N_PUSH(ARRAY_VAL(rarr));
        break;
    }
    case NATIVE_STR: {
        

        if(argc<1){ N_PUSH(sv("")); break; }
        Value v=args[0];
        if(IS_STRING(v)){ N_PUSH(v); break; }
        StrBuf sb; str_buf_init(&sb);
        str_value_recursive(&sb, v, 0);
        N_PUSH(sv(sb.buf));
        str_buf_free(&sb);
        break;
    }
    case NATIVE_LEN: {
        
        if(argc<1){ N_PUSH(NUMBER_VAL(0)); break; }
        Value v=args[0];
        if(IS_ARRAY(v))       N_PUSH(NUMBER_VAL((double)AS_ARRAY(v)->len));
        else if(IS_STRING(v)) N_PUSH(NUMBER_VAL((double)AS_STRING(v)->len));
        else if(IS_DICT(v))   N_PUSH(NUMBER_VAL((double)AS_DICT(v)->count));
        else                  N_PUSH(NUMBER_VAL(0));
        break;
    }

    case NATIVE_OS_SETENV: {
        if(argc<2||!IS_STRING(args[0])||!IS_STRING(args[1])){ N_PUSH(BOOL_VAL(false)); break; }
        N_PUSH(BOOL_VAL(setenv(STR(args[0]), STR(args[1]), 1) == 0));
        break;
    }
    case NATIVE_OS_UNSETENV: {
        if(argc<1||!IS_STRING(args[0])){ N_PUSH(BOOL_VAL(false)); break; }
        N_PUSH(BOOL_VAL(unsetenv(STR(args[0])) == 0));
        break;
    }
    case NATIVE_OS_CHDIR: {
        if(argc<1||!IS_STRING(args[0])){ N_PUSH(BOOL_VAL(false)); break; }
        N_PUSH(BOOL_VAL(chdir(STR(args[0])) == 0));
        break;
    }
    case NATIVE_OS_GETCWD: {
        char buf[4096];
        if(getcwd(buf, sizeof(buf))) N_PUSH(sv(buf));
        else                         N_PUSH(NIL_VAL);
        break;
    }

    case NATIVE_FILE_RENAME:
    case NATIVE_FILE_MOVE: {
        if(argc<2||!IS_STRING(args[0])||!IS_STRING(args[1])){ N_PUSH(BOOL_VAL(false)); break; }
        N_PUSH(BOOL_VAL(rename(STR(args[0]), STR(args[1])) == 0));
        break;
    }
    case NATIVE_DIR_EXISTS: {
        if(argc<1||!IS_STRING(args[0])){ N_PUSH(BOOL_VAL(false)); break; }
        struct stat st;
        int r = stat(STR(args[0]), &st);
        N_PUSH(BOOL_VAL(r==0 && S_ISDIR(st.st_mode)));
        break;
    }
    case NATIVE_DIR_REMOVE: {
        if(argc<1||!IS_STRING(args[0])){ N_PUSH(BOOL_VAL(false)); break; }
        N_PUSH(BOOL_VAL(rmdir(STR(args[0])) == 0));
        break;
    }
    case NATIVE_FILE_PERMS: {
        if(argc<2||!IS_STRING(args[0])||!IS_NUMBER(args[1])){ N_PUSH(BOOL_VAL(false)); break; }
        int mode = (int)AS_NUMBER(args[1]);
        N_PUSH(BOOL_VAL(chmod(STR(args[0]), (mode_t)mode) == 0));
        break;
    }

    case NATIVE_MATH_FLOOR:  N_PUSH(NUMBER_VAL(floor(argc>=1?NUM(args[0]):0))); break;
    case NATIVE_MATH_CEIL:   N_PUSH(NUMBER_VAL(ceil (argc>=1?NUM(args[0]):0))); break;
    case NATIVE_MATH_ROUND:  N_PUSH(NUMBER_VAL(round(argc>=1?NUM(args[0]):0))); break;
    case NATIVE_MATH_TRUNC:  N_PUSH(NUMBER_VAL(trunc(argc>=1?NUM(args[0]):0))); break;
    case NATIVE_MATH_ABS:    N_PUSH(NUMBER_VAL(fabs (argc>=1?NUM(args[0]):0))); break;
    case NATIVE_MATH_SQRT: {
        double x = argc>=1?NUM(args[0]):0;
        N_PUSH(x<0 ? NIL_VAL : NUMBER_VAL(sqrt(x)));
        break;
    }
    case NATIVE_MATH_POW:    N_PUSH(NUMBER_VAL(pow  (argc>=1?NUM(args[0]):0, argc>=2?NUM(args[1]):1))); break;
    case NATIVE_MATH_SIN:    N_PUSH(NUMBER_VAL(sin  (argc>=1?NUM(args[0]):0))); break;
    case NATIVE_MATH_COS:    N_PUSH(NUMBER_VAL(cos  (argc>=1?NUM(args[0]):0))); break;
    case NATIVE_MATH_TAN:    N_PUSH(NUMBER_VAL(tan  (argc>=1?NUM(args[0]):0))); break;
    case NATIVE_MATH_ATAN2:  N_PUSH(NUMBER_VAL(atan2(argc>=1?NUM(args[0]):0, argc>=2?NUM(args[1]):1))); break;
    case NATIVE_MATH_LOG: {
        double x = argc>=1?NUM(args[0]):1;
        if(argc>=2 && NUM(args[1])>0 && NUM(args[1])!=1)
            N_PUSH(NUMBER_VAL(log(x)/log(NUM(args[1]))));
        else
            N_PUSH(NUMBER_VAL(log(x)));
        break;
    }
    case NATIVE_MATH_LOG10:  N_PUSH(NUMBER_VAL(log10(argc>=1?NUM(args[0]):1))); break;
    case NATIVE_MATH_MIN: {
        if(argc<2){ N_PUSH(argc>=1?args[0]:NIL_VAL); break; }
        N_PUSH(NUM(args[0])<=NUM(args[1])?args[0]:args[1]);
        break;
    }
    case NATIVE_MATH_MAX: {
        if(argc<2){ N_PUSH(argc>=1?args[0]:NIL_VAL); break; }
        N_PUSH(NUM(args[0])>=NUM(args[1])?args[0]:args[1]);
        break;
    }
    case NATIVE_MATH_RANDOM: {
        
        static int seeded=0;
        if(!seeded){ srand((unsigned)time(NULL)^(unsigned)getpid()); seeded=1; }
        double r=(double)rand()/(double)RAND_MAX;
        if(argc>=1) r*=NUM(args[0]);
        N_PUSH(NUMBER_VAL(r));
        break;
    }
    case NATIVE_MATH_CLAMP: {
        double v=argc>=1?NUM(args[0]):0;
        double lo=argc>=2?NUM(args[1]):0;
        double hi=argc>=3?NUM(args[2]):1;
        if(v<lo) v=lo; else if(v>hi) v=hi;
        N_PUSH(NUMBER_VAL(v));
        break;
    }
    case NATIVE_MATH_LERP: {
        double a=argc>=1?NUM(args[0]):0;
        double b=argc>=2?NUM(args[1]):0;
        double t=argc>=3?NUM(args[2]):0;
        N_PUSH(NUMBER_VAL(a + t*(b-a)));
        break;
    }
    case NATIVE_MATH_SIGN: {
        double x=argc>=1?NUM(args[0]):0;
        N_PUSH(NUMBER_VAL(x>0?1.0:x<0?-1.0:0.0));
        break;
    }
    case NATIVE_MATH_PI:    N_PUSH(NUMBER_VAL(3.14159265358979323846)); break;
    case NATIVE_MATH_E:     N_PUSH(NUMBER_VAL(2.71828182845904523536)); break;
    case NATIVE_MATH_IS_NAN: N_PUSH(BOOL_VAL(argc>=1 && IS_NUMBER(args[0]) && isnan(AS_NUMBER(args[0])))); break;
    case NATIVE_MATH_IS_INF: N_PUSH(BOOL_VAL(argc>=1 && IS_NUMBER(args[0]) && isinf(AS_NUMBER(args[0])))); break;

    
    case NATIVE_MATH_ASIN:  N_PUSH(NUMBER_VAL(asin (argc>=1?NUM(args[0]):0))); break;
    case NATIVE_MATH_ACOS:  N_PUSH(NUMBER_VAL(acos (argc>=1?NUM(args[0]):0))); break;
    case NATIVE_MATH_ATAN:  N_PUSH(NUMBER_VAL(atan (argc>=1?NUM(args[0]):0))); break;
    case NATIVE_MATH_SINH:  N_PUSH(NUMBER_VAL(sinh (argc>=1?NUM(args[0]):0))); break;
    case NATIVE_MATH_COSH:  N_PUSH(NUMBER_VAL(cosh (argc>=1?NUM(args[0]):0))); break;
    case NATIVE_MATH_TANH:  N_PUSH(NUMBER_VAL(tanh (argc>=1?NUM(args[0]):0))); break;
    case NATIVE_MATH_EXP:   N_PUSH(NUMBER_VAL(exp  (argc>=1?NUM(args[0]):0))); break;
    case NATIVE_MATH_EXP2:  N_PUSH(NUMBER_VAL(exp2 (argc>=1?NUM(args[0]):0))); break;
    case NATIVE_MATH_LOG2:  N_PUSH(NUMBER_VAL(log2 (argc>=1?NUM(args[0]):1))); break;
    case NATIVE_MATH_CBRT:  N_PUSH(NUMBER_VAL(cbrt (argc>=1?NUM(args[0]):0))); break;
    case NATIVE_MATH_HYPOT: N_PUSH(NUMBER_VAL(hypot(argc>=1?NUM(args[0]):0, argc>=2?NUM(args[1]):0))); break;
    case NATIVE_MATH_DEG:   N_PUSH(NUMBER_VAL((argc>=1?NUM(args[0]):0) * (180.0 / 3.14159265358979323846))); break;
    case NATIVE_MATH_RAD:   N_PUSH(NUMBER_VAL((argc>=1?NUM(args[0]):0) * (3.14159265358979323846 / 180.0))); break;
    case NATIVE_MATH_GCD: {
        long long a=(long long)fabs(argc>=1?NUM(args[0]):0);
        long long b=(long long)fabs(argc>=2?NUM(args[1]):0);
        while(b){ long long t=b; b=a%b; a=t; }
        N_PUSH(NUMBER_VAL((double)a)); break;
    }
    case NATIVE_MATH_LCM: {
        long long a=(long long)fabs(argc>=1?NUM(args[0]):0);
        long long b=(long long)fabs(argc>=2?NUM(args[1]):0);
        if(!a||!b){ N_PUSH(NUMBER_VAL(0)); break; }
        long long g=a; long long tmp=b;
        while(tmp){ long long t=tmp; tmp=g%tmp; g=t; }
        N_PUSH(NUMBER_VAL((double)(a/g*b))); break;
    }
    case NATIVE_MATH_FACTORIAL: {
        long long n=(long long)(argc>=1?NUM(args[0]):0);
        if(n<0||n>20){ N_PUSH(NUMBER_VAL(n<0?-1.0:1.0/0.0)); break; }
        long long r=1; for(long long i=2;i<=n;i++) r*=i;
        N_PUSH(NUMBER_VAL((double)r)); break;
    }

    
    case NATIVE_OS_POPEN: {
        if(argc<1||!IS_STRING(args[0])){ N_PUSH(NIL_VAL); break; }
        FILE *pp=popen(STR(args[0]),"r");
        if(!pp){ N_PUSH(NIL_VAL); break; }
        char *out=NULL; size_t outlen=0, outcap=0;
        char pbuf[4096]; size_t nr;
        while((nr=fread(pbuf,1,sizeof(pbuf),pp))>0){
            if(outlen+nr+1>outcap){
                outcap=(outlen+nr+1)*2+4096;
                out=(char*)realloc(out,outcap);
                if(!out){pclose(pp);N_PUSH(NIL_VAL);goto popen_done;}
            }
            memcpy(out+outlen,pbuf,nr); outlen+=nr;
        }
        pclose(pp);
        if(out){ out[outlen]='\0'; N_PUSH(sv(out)); free(out); }
        else N_PUSH(sv(""));
        popen_done:; break;
    }
    case NATIVE_OS_CPU_COUNT: {
#ifdef _SC_NPROCESSORS_ONLN
        long n=sysconf(_SC_NPROCESSORS_ONLN);
        N_PUSH(NUMBER_VAL(n>0?(double)n:1.0));
#else
        N_PUSH(NUMBER_VAL(1.0));
#endif
        break;
    }

    
    case NATIVE_TYPE_INT: {
        if(argc<1){ N_PUSH(NUMBER_VAL(0)); break; }
        Value v=args[0];
        if(IS_NUMBER(v))  { N_PUSH(NUMBER_VAL((double)(long long)v.as.number)); break; }
        if(IS_STRING(v))  { N_PUSH(NUMBER_VAL((double)(long long)strtoll(AS_STRING(v)->chars,NULL,10))); break; }
        if(IS_BOOL(v))    { N_PUSH(NUMBER_VAL(v.as.boolean?1.0:0.0)); break; }
        if(IS_NIL(v))     { N_PUSH(NUMBER_VAL(0)); break; }
        N_PUSH(NUMBER_VAL(0)); break;
    }
    case NATIVE_TYPE_FLOAT: {
        if(argc<1){ N_PUSH(NUMBER_VAL(0.0)); break; }
        Value v=args[0];
        if(IS_NUMBER(v))  { N_PUSH(v); break; }
        if(IS_STRING(v))  { char *ep=NULL; double d=strtod(AS_STRING(v)->chars,&ep); N_PUSH(NUMBER_VAL(d)); break; }
        if(IS_BOOL(v))    { N_PUSH(NUMBER_VAL(v.as.boolean?1.0:0.0)); break; }
        if(IS_NIL(v))     { N_PUSH(NUMBER_VAL(0.0)); break; }
        N_PUSH(NUMBER_VAL(0.0)); break;
    }
    case NATIVE_TYPE_BOOL: {
        if(argc<1){ N_PUSH(BOOL_VAL(false)); break; }
        Value v=args[0];
        bool b=false;
        if(IS_BOOL(v))    b=v.as.boolean;
        else if(IS_NUMBER(v)) b=(v.as.number!=0.0&&!isnan(v.as.number));
        else if(IS_STRING(v)) b=(AS_STRING(v)->len>0&&strcmp(AS_STRING(v)->chars,"false")!=0&&strcmp(AS_STRING(v)->chars,"0")!=0);
        else if(IS_ARRAY(v))  b=true;
        else if(IS_DICT(v))   b=true;
        else if(IS_NIL(v))    b=false;
        N_PUSH(BOOL_VAL(b)); break;
    }

    
    case NATIVE_STR_FORMAT: {
        
        if(argc<1||!IS_STRING(args[0])){ N_PUSH(sv("")); break; }
        const char *fmt=STR(args[0]);
        char *out=(char*)malloc(4096); size_t ocap=4096, olen=0;
        int ai=1;
        for(const char *p=fmt;*p;p++){
            if(*p!='%'||!*(p+1)){
                if(olen+2>ocap){ocap*=2;out=(char*)realloc(out,ocap);}
                out[olen++]=*p; continue;
            }
            p++;
            char tmp[128]; int tl=0;
            switch(*p){
                case 's':{
                    const char *s=(ai<argc&&IS_STRING(args[ai]))?STR(args[ai]):(ai<argc&&IS_NUMBER(args[ai]))?"<n>":"";
                    if(ai<argc&&IS_NUMBER(args[ai])){
                        if(args[ai].as.number==(long long)args[ai].as.number) tl=snprintf(tmp,sizeof(tmp),"%lld",(long long)args[ai].as.number);
                        else tl=snprintf(tmp,sizeof(tmp),"%.14g",args[ai].as.number);
                        s=tmp;
                    } else if(ai<argc&&IS_BOOL(args[ai])){ s=args[ai].as.boolean?"true":"false"; }
                    else if(ai<argc&&IS_NIL(args[ai]))  { s="nil"; }
                    size_t sl=strlen(s);
                    if(olen+sl+1>ocap){ocap=(olen+sl+1)*2;out=(char*)realloc(out,ocap);}
                    memcpy(out+olen,s,sl); olen+=sl; ai++; break;
                }
                case 'd':case 'i':
                    tl=snprintf(tmp,sizeof(tmp),"%lld",(long long)(ai<argc?NUM(args[ai]):0)); ai++;
                    goto fmt_append;
                case 'f':
                    tl=snprintf(tmp,sizeof(tmp),"%f",(ai<argc?NUM(args[ai]):0.0)); ai++;
                    goto fmt_append;
                case 'g':case 'G':
                    tl=snprintf(tmp,sizeof(tmp),"%.14g",(ai<argc?NUM(args[ai]):0.0)); ai++;
                    goto fmt_append;
                case 'x':
                    tl=snprintf(tmp,sizeof(tmp),"%llx",(long long)(ai<argc?NUM(args[ai]):0)); ai++;
                    goto fmt_append;
                case '%':
                    tl=1; tmp[0]='%'; tmp[1]='\0';
                    goto fmt_append;
                fmt_append:
                    if(olen+(size_t)tl+1>ocap){ocap=(olen+(size_t)tl+1)*2;out=(char*)realloc(out,ocap);}
                    memcpy(out+olen,tmp,(size_t)tl); olen+=(size_t)tl; break;
                default:
                    if(olen+3>ocap){ocap*=2;out=(char*)realloc(out,ocap);}
                    out[olen++]='%'; out[olen++]=*p; break;
            }
        }
        out[olen]='\0';
        N_PUSH(sv(out)); free(out); break;
    }
    case NATIVE_STR_BYTES: {
        if(argc<1||!IS_STRING(args[0])){ N_PUSH(ARRAY_VAL(gc_array())); break; }
        ObjString *s=AS_STRING(args[0]);
        ObjArray *arr=gc_array();
        for(int i=0;i<s->len;i++) gc_arr_push(arr,NUMBER_VAL((double)(unsigned char)s->chars[i]));
        N_PUSH(ARRAY_VAL(arr)); break;
    }
    case NATIVE_STR_FROM_BYTES: {
        if(argc<1||!IS_ARRAY(args[0])){ N_PUSH(sv("")); break; }
        ObjArray *arr=AS_ARRAY(args[0]);
        char *buf=(char*)malloc((size_t)arr->len+1);
        for(int i=0;i<arr->len;i++)
            buf[i]=(char)((int)(IS_NUMBER(arr->items[i])?AS_NUMBER(arr->items[i]):0)&0xFF);
        buf[arr->len]='\0';
        N_PUSH(sv(buf)); free(buf); break;
    }
    case NATIVE_STR_ORD: {
        if(argc<1||!IS_STRING(args[0])||AS_STRING(args[0])->len==0){ N_PUSH(NUMBER_VAL(-1)); break; }
        N_PUSH(NUMBER_VAL((double)(unsigned char)AS_STRING(args[0])->chars[0])); break;
    }
    case NATIVE_STR_CHR: {
        if(argc<1||!IS_NUMBER(args[0])){ N_PUSH(sv("")); break; }
        int cp=(int)AS_NUMBER(args[0]);
        char buf[2]={(char)(cp&0xFF),'\0'};
        N_PUSH(sv(buf)); break;
    }

    
    case NATIVE_JSON_PARSE: {
        if(argc<1||!IS_STRING(args[0])){ N_PUSH(NIL_VAL); break; }
        
        const char *js=STR(args[0]);
        

        typedef struct { const char *p; bool err; VM *vm_ref; } JP;
        #define JP_SKIP(j) do{ while(*(j)->p==' '||*(j)->p=='\t'||*(j)->p=='\n'||*(j)->p=='\r')(j)->p++; }while(0)

        Value json_parse_value(JP *j);

        

        

        
        #define JSON_STACK_MAX 64
        typedef struct { int state; Value container; ObjString *key; int arr_idx; } JFrame;
        JFrame jstack[JSON_STACK_MAX]; int jdepth=0;
        JP jp={js,false,vm};

        #define JSKIP() do{ while(*jp.p==' '||*jp.p=='\t'||*jp.p=='\n'||*jp.p=='\r') jp.p++; }while(0)

        
        #define JSON_PARSE_STR(out_str) do { \
            char *_sb=(char*)malloc(256); size_t _scap=256, _slen=0; \
            while(*jp.p&&*jp.p!='"'){ \
                char _c=*jp.p++; \
                if(_c=='\\'){ char _e=*jp.p++; \
                    switch(_e){ case '"': _c='"'; break; case '\\': _c='\\'; break; \
                        case 'n': _c='\n'; break; case 't': _c='\t'; break; \
                        case 'r': _c='\r'; break; default: _c=_e; break; } } \
                if(_slen+2>_scap){_scap*=2;_sb=(char*)realloc(_sb,_scap);} \
                _sb[_slen++]=_c; \
            } \
            if(*jp.p=='"') jp.p++; \
            _sb[_slen]='\0'; \
            (out_str)=gc_string(_sb,(int)_slen); free(_sb); \
        } while(0)

        

        Value root_val=NIL_VAL;
        bool root_set=false;

        

        

        jdepth=0;
        int jstate=0; 
        (void)jstate;

        

        
        #define JPUSH_VAL(v) do { \
            if(jdepth==0){ root_val=(v); root_set=true; } \
            else { \
                JFrame *_fr=&jstack[jdepth-1]; \
                if(IS_ARRAY(_fr->container)){ gc_arr_push(AS_ARRAY(_fr->container),(v)); } \
                else if(IS_DICT(_fr->container)&&_fr->key){ \
                    gc_dict_set(AS_DICT(_fr->container),_fr->key,(v)); _fr->key=NULL; } \
            } \
        } while(0)

        
        JSKIP();
        while(*jp.p && !jp.err){
            JSKIP();
            if(!*jp.p) break;
            char c=*jp.p;

            
            if((c==']'||c=='}')){
                jp.p++;
                if(jdepth>0){
                    Value completed=jstack[jdepth-1].container;
                    jdepth--;
                    JPUSH_VAL(completed);
                } else { jp.err=true; break; }
                JSKIP();
                if(*jp.p==',') jp.p++;
                continue;
            }

            
            if(jdepth>0 && IS_DICT(jstack[jdepth-1].container) && !jstack[jdepth-1].key){
                if(c=='"'){
                    jp.p++;
                    ObjString *k=NULL; JSON_PARSE_STR(k);
                    jstack[jdepth-1].key=k;
                    JSKIP(); if(*jp.p==':') jp.p++;
                    continue;
                }
                if(c==','){jp.p++;continue;}
                if(c!='}'){jp.err=true;break;}
                continue;
            }

            
            Value parsed=NIL_VAL; bool got=true;
            if(c=='"'){
                jp.p++;
                ObjString *s=NULL; JSON_PARSE_STR(s);
                parsed=STRING_VAL(s);
            } else if(c=='['){
                jp.p++;
                if(jdepth>=JSON_STACK_MAX){jp.err=true;break;}
                jstack[jdepth].container=ARRAY_VAL(gc_array());
                jstack[jdepth].key=NULL;
                jdepth++;
                JSKIP(); continue;
            } else if(c=='{'){
                jp.p++;
                if(jdepth>=JSON_STACK_MAX){jp.err=true;break;}
                jstack[jdepth].container=DICT_VAL(gc_dict());
                jstack[jdepth].key=NULL;
                jdepth++;
                JSKIP(); continue;
            } else if(c=='t'&&strncmp(jp.p,"true",4)==0){jp.p+=4;parsed=BOOL_VAL(true);}
            else if(c=='f'&&strncmp(jp.p,"false",5)==0){jp.p+=5;parsed=BOOL_VAL(false);}
            else if(c=='n'&&strncmp(jp.p,"null",4)==0){jp.p+=4;parsed=NIL_VAL;}
            else if(c=='-'||(c>='0'&&c<='9')){
                char *ep=NULL; double d=strtod(jp.p,&ep);
                if(ep&&ep!=jp.p){jp.p=ep;parsed=NUMBER_VAL(d);}
                else {jp.err=true;break;}
            } else { got=false; jp.p++; }

            if(got){
                JPUSH_VAL(parsed);
                JSKIP(); if(*jp.p==',') jp.p++;
            }
        }

        #undef JSON_PARSE_STR
        #undef JPUSH_VAL
        #undef JSKIP
        #undef JP_SKIP
        #undef JSON_STACK_MAX

        N_PUSH(jp.err ? NIL_VAL : (root_set ? root_val : NIL_VAL));
        break;
    }

    case NATIVE_JSON_STRINGIFY: {
        if(argc<1){ N_PUSH(sv("null")); break; }
        
        
        char *jbuf=(char*)malloc(4096); size_t jcap=4096, jlen=0;
        #define JA(s) do{ size_t _l=strlen(s); if(jlen+_l+1>jcap){jcap=(jlen+_l+1)*2;jbuf=(char*)realloc(jbuf,jcap);} memcpy(jbuf+jlen,s,_l); jlen+=_l; jbuf[jlen]='\0'; }while(0)
        #define JC(c) do{ if(jlen+2>jcap){jcap*=2;jbuf=(char*)realloc(jbuf,jcap);} jbuf[jlen++]=(c); jbuf[jlen]='\0'; }while(0)

        
        #define JSON_STR_VAL(v) do { \
            Value _v=(v); \
            if(IS_NIL(_v))       JA("null"); \
            else if(IS_BOOL(_v)) JA(_v.as.boolean?"true":"false"); \
            else if(IS_NUMBER(_v)){ char _nb[64]; \
                if(_v.as.number==(long long)_v.as.number) snprintf(_nb,sizeof(_nb),"%lld",(long long)_v.as.number); \
                else snprintf(_nb,sizeof(_nb),"%.14g",_v.as.number); JA(_nb); } \
            else if(IS_STRING(_v)){ JC('"'); \
                for(int _i=0;_i<AS_STRING(_v)->len;_i++){ \
                    char _sc=AS_STRING(_v)->chars[_i]; \
                    if(_sc=='"'){JC('\\');JC('"');} else if(_sc=='\\'){JC('\\');JC('\\');} \
                    else if(_sc=='\n'){JC('\\');JC('n');} else if(_sc=='\t'){JC('\\');JC('t');} \
                    else if(_sc=='\r'){JC('\\');JC('r');} else JC(_sc); \
                } JC('"'); } \
            else if(IS_FUNCTION(_v)){ JA("null");  } \
            else if(IS_ARRAY(_v)){ \
                JC('['); ObjArray *_a=AS_ARRAY(_v); \
                for(int _i=0;_i<_a->len;_i++){ \
                    if(_i) JA(","); \
                    Value _it=_a->items[_i]; \
                    if(IS_NIL(_it))       JA("null"); \
                    else if(IS_BOOL(_it)) JA(_it.as.boolean?"true":"false"); \
                    else if(IS_NUMBER(_it)){ char _nb[64]; \
                        if(_it.as.number==(long long)_it.as.number) snprintf(_nb,sizeof(_nb),"%lld",(long long)_it.as.number); \
                        else snprintf(_nb,sizeof(_nb),"%.14g",_it.as.number); JA(_nb); } \
                    else if(IS_STRING(_it)){ JC('"'); \
                        for(int _j=0;_j<AS_STRING(_it)->len;_j++){ char _sc=AS_STRING(_it)->chars[_j]; \
                            if(_sc=='"'){JC('\\');JC('"');}else if(_sc=='\\'){JC('\\');JC('\\');}else JC(_sc);} JC('"'); } \
                    else JA("null"); \
                } JC(']'); } \
            else if(IS_DICT(_v)){ \
                JC('{'); ObjDict *_d=AS_DICT(_v); bool _f=true; \
                for(int _i=0;_i<_d->cap;_i++){ \
                    DictEntry *_e=&_d->entries[_i]; if(!_e->key) continue; \
                    if(!_f) JA(","); _f=false; \
                    JC('"'); JA(_e->key->chars); JA("\":"); \
                    Value _val=_e->value; \
                    if(IS_NIL(_val))       JA("null"); \
                    else if(IS_BOOL(_val)) JA(_val.as.boolean?"true":"false"); \
                    else if(IS_NUMBER(_val)){ char _nb[64]; \
                        if(_val.as.number==(long long)_val.as.number) snprintf(_nb,sizeof(_nb),"%lld",(long long)_val.as.number); \
                        else snprintf(_nb,sizeof(_nb),"%.14g",_val.as.number); JA(_nb); } \
                    else if(IS_STRING(_val)){ JC('"'); JA(AS_STRING(_val)->chars); JC('"'); } \
                    else JA("null"); \
                } JC('}'); } \
            else JA("null"); \
        } while(0)

        JSON_STR_VAL(args[0]);

        #undef JSON_STR_VAL
        #undef JA
        #undef JC

        N_PUSH(sv(jbuf)); free(jbuf);
        break;
    }

    default:
        if (id >= 0x0500 && id <= 0x05FF) {
            net_dispatch(vm, id, argc, args);
        } else {
            N_PUSH(NIL_VAL);
        }
        break;
    }
}
