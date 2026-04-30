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

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdlib.h>  /* NULL */

/* 1.0: runtime args set by main() after CLI parsing */
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
        /* Return CLI arguments passed after the entry file.
         * e.g.  chn4.4 script.chn --foo bar  ->  ["--foo", "bar"] */
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
        if (sz < 0 || sz > 8*1024*1024) { fclose(fp); N_PUSH(NIL_VAL); break; }
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
        /* range(stop)  range(start,stop)  range(start,stop,step) */
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
        /* str(val) -> string representation of any value */
        if(argc<1){ N_PUSH(sv("")); break; }
        Value v=args[0];
        if(IS_STRING(v)){ N_PUSH(v); break; }
        char buf[65536]; int pos=0;
        /* reuse the same formatting logic as print_value */
        switch(v.type){
            case VAL_NUMBER:
                if(v.as.number==(long long)v.as.number)
                    pos=snprintf(buf,sizeof(buf),"%lld",(long long)v.as.number);
                else
                    pos=snprintf(buf,sizeof(buf),"%.14g",v.as.number);
                break;
            case VAL_BOOL:   pos=snprintf(buf,sizeof(buf),"%s",v.as.boolean?"true":"false"); break;
            case VAL_NIL:    pos=snprintf(buf,sizeof(buf),"nil"); break;
            case VAL_FUNCTION: pos=snprintf(buf,sizeof(buf),"<func %s>",v.as.function->name); break;
            case VAL_ARRAY:  pos=snprintf(buf,sizeof(buf),"<array len=%d>",v.as.array->len); break;
            case VAL_DICT:   pos=snprintf(buf,sizeof(buf),"<dict keys=%d>",v.as.dict->count); break;
            default:         pos=snprintf(buf,sizeof(buf),"<value>"); break;
        }
        buf[pos]='\0';
        N_PUSH(sv(buf));
        break;
    }
    case NATIVE_LEN: {
        /* len(val) -> length of array, string, or dict */
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
        /* Seed once on first call */
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

    default:
        if (id >= 0x0500 && id <= 0x05FF) {
            net_dispatch(vm, id, argc, args);
        } else {
            N_PUSH(NIL_VAL);
        }
        break;
    }
}
