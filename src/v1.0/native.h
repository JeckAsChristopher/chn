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

#ifndef NATIVE_H
#define NATIVE_H

#include "common.h"
#include "gc.h"

typedef struct VM VM;

typedef enum {
    
    NATIVE_OS_TIME       = 0x0000,  
    NATIVE_OS_CLOCK      = 0x0001,  
    NATIVE_OS_SLEEP      = 0x0002,  
    NATIVE_OS_EXIT       = 0x0003,  
    NATIVE_OS_GETENV     = 0x0004,  
    NATIVE_OS_ARGS       = 0x0005,  
    NATIVE_OS_PLATFORM   = 0x0006,  
    NATIVE_OS_HOSTNAME   = 0x0007,  
    NATIVE_OS_PID        = 0x0008,  
    NATIVE_OS_SYSTEM     = 0x0009,  
    NATIVE_OS_SETENV     = 0x000A,  /* setenv(name, value) -> bool               */
    NATIVE_OS_UNSETENV   = 0x000B,  /* unsetenv(name) -> bool                    */
    NATIVE_OS_CHDIR      = 0x000C,  /* chdir(path) -> bool                       */
    NATIVE_OS_GETCWD     = 0x000D,  /* getcwd() -> string                        */

    
    NATIVE_FILE_READ     = 0x0100,  
    NATIVE_FILE_WRITE    = 0x0101,  
    NATIVE_FILE_APPEND   = 0x0102,  
    NATIVE_FILE_EXISTS   = 0x0103,  
    NATIVE_FILE_DELETE   = 0x0104,  
    NATIVE_FILE_SIZE     = 0x0105,  
    NATIVE_FILE_LINES    = 0x0106,  
    NATIVE_DIR_LIST      = 0x0107,  
    NATIVE_DIR_MAKE      = 0x0108,  
    NATIVE_FILE_COPY     = 0x0109,  
    NATIVE_FILE_RENAME   = 0x010A,  /* rename(old, new) -> bool                  */
    NATIVE_FILE_MOVE     = 0x010B,  /* alias for rename                         */
    NATIVE_DIR_EXISTS    = 0x010C,  /* dir_exists(path) -> bool                  */
    NATIVE_DIR_REMOVE    = 0x010D,  /* rmdir(path) -> bool (must be empty)       */
    NATIVE_FILE_PERMS    = 0x010E,  /* chmod(path, mode_octal) -> bool           */

    /* math (0x0200-0x02FF) */
    NATIVE_MATH_FLOOR    = 0x0200,
    NATIVE_MATH_CEIL     = 0x0201,
    NATIVE_MATH_ROUND    = 0x0202,
    NATIVE_MATH_ABS      = 0x0203,
    NATIVE_MATH_SQRT     = 0x0204,
    NATIVE_MATH_POW      = 0x0205,
    NATIVE_MATH_SIN      = 0x0206,
    NATIVE_MATH_COS      = 0x0207,
    NATIVE_MATH_TAN      = 0x0208,
    NATIVE_MATH_LOG      = 0x0209,  /* log(x) natural log; log(x,base) any base */
    NATIVE_MATH_LOG10    = 0x020A,
    NATIVE_MATH_MIN      = 0x020B,  /* min(a, b) */
    NATIVE_MATH_MAX      = 0x020C,  /* max(a, b) */
    NATIVE_MATH_RANDOM   = 0x020D,  /* random() -> [0,1)  random(n) -> [0,n)     */
    NATIVE_MATH_CLAMP    = 0x020E,  /* clamp(v, lo, hi)                         */
    NATIVE_MATH_PI       = 0x020F,  /* pi() -> 3.14159-                          */
    NATIVE_MATH_E        = 0x0210,  /* e() -> 2.71828-                           */
    NATIVE_MATH_ATAN2    = 0x0211,
    NATIVE_MATH_TRUNC    = 0x0212,
    NATIVE_MATH_SIGN     = 0x0213,  /* sign(x) -> -1, 0, or 1                   */
    NATIVE_MATH_LERP     = 0x0214,  /* lerp(a, b, t)                            */
    NATIVE_MATH_IS_NAN   = 0x0215,  /* is_nan(x) -> bool                         */
    NATIVE_MATH_IS_INF   = 0x0216,  /* is_inf(x) -> bool                         */

    
    NATIVE_BIN_WRITE     = 0x0300,  
    NATIVE_BIN_READ      = 0x0301,  
    NATIVE_BIN_WRITE_NUM = 0x0302,

    /* builtin utility functions (0x0400-0x04FF) */
    NATIVE_RANGE    = 0x0400,   /* range(stop)  range(start,stop)  range(start,stop,step) */
    NATIVE_STR      = 0x0401,   /* str(val)  -> string representation */
    NATIVE_LEN      = 0x0402,   /* len(val)  -> length of array/string/dict */

    /* network (0x0500-0x05FF) - moved from 0x0400 in v3.6 */
    NATIVE_NET_TCP_LISTEN  = 0x0500,
    NATIVE_NET_TCP_ACCEPT  = 0x0501,
    NATIVE_NET_TCP_CONNECT = 0x0502,
    NATIVE_NET_SEND        = 0x0503,
    NATIVE_NET_RECV        = 0x0504,
    NATIVE_NET_CLOSE       = 0x0505,
    NATIVE_NET_DNS         = 0x0506,
    NATIVE_NET_UDP_BIND    = 0x0507,
    NATIVE_NET_UDP_SEND    = 0x0508,
    NATIVE_NET_UDP_RECV    = 0x0509,
    NATIVE_NET_HTTP_GET    = 0x050A,
    NATIVE_NET_HTTP_POST   = 0x050B,
    NATIVE_NET_TLS_LISTEN  = 0x050C,
    NATIVE_NET_TLS_ACCEPT  = 0x050D,
    NATIVE_NET_TLS_CONNECT = 0x050E,
    NATIVE_NET_TLS_SEND    = 0x050F,
    NATIVE_NET_TLS_RECV    = 0x0510,
    NATIVE_NET_TLS_CLOSE   = 0x0511,
    NATIVE_NET_PEER_ADDR   = 0x0512,
    NATIVE_NET_SET_TIMEOUT = 0x0513,

} NativeCallID;

void native_dispatch(VM *vm, uint16_t id, uint8_t argc);
void net_dispatch   (VM *vm, uint16_t id, uint8_t argc, Value *args);

/* 1.0: runtime arguments set by main() after CLI parsing.
 * Exposed via os::args() through NATIVE_OS_ARGS.               */
extern char **g_runtime_argv;
extern int    g_runtime_argc;

#endif
