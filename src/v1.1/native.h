

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
    NATIVE_OS_SETENV     = 0x000A,  
    NATIVE_OS_UNSETENV   = 0x000B,  
    NATIVE_OS_CHDIR      = 0x000C,  
    NATIVE_OS_GETCWD     = 0x000D,  

    
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
    NATIVE_FILE_RENAME   = 0x010A,  
    NATIVE_FILE_MOVE     = 0x010B,  
    NATIVE_DIR_EXISTS    = 0x010C,  
    NATIVE_DIR_REMOVE    = 0x010D,  
    NATIVE_FILE_PERMS    = 0x010E,  

    
    NATIVE_MATH_FLOOR    = 0x0200,
    NATIVE_MATH_CEIL     = 0x0201,
    NATIVE_MATH_ROUND    = 0x0202,
    NATIVE_MATH_ABS      = 0x0203,
    NATIVE_MATH_SQRT     = 0x0204,
    NATIVE_MATH_POW      = 0x0205,
    NATIVE_MATH_SIN      = 0x0206,
    NATIVE_MATH_COS      = 0x0207,
    NATIVE_MATH_TAN      = 0x0208,
    NATIVE_MATH_LOG      = 0x0209,  
    NATIVE_MATH_LOG10    = 0x020A,
    NATIVE_MATH_MIN      = 0x020B,  
    NATIVE_MATH_MAX      = 0x020C,  
    NATIVE_MATH_RANDOM   = 0x020D,  
    NATIVE_MATH_CLAMP    = 0x020E,  
    NATIVE_MATH_PI       = 0x020F,  
    NATIVE_MATH_E        = 0x0210,  
    NATIVE_MATH_ATAN2    = 0x0211,
    NATIVE_MATH_TRUNC    = 0x0212,
    NATIVE_MATH_SIGN     = 0x0213,  
    NATIVE_MATH_LERP     = 0x0214,  
    NATIVE_MATH_IS_NAN   = 0x0215,  
    NATIVE_MATH_IS_INF   = 0x0216,  
    
    NATIVE_MATH_ASIN     = 0x0217,  
    NATIVE_MATH_ACOS     = 0x0218,  
    NATIVE_MATH_ATAN     = 0x0219,  
    NATIVE_MATH_SINH     = 0x021A,  
    NATIVE_MATH_COSH     = 0x021B,  
    NATIVE_MATH_TANH     = 0x021C,  
    NATIVE_MATH_EXP      = 0x021D,  
    NATIVE_MATH_EXP2     = 0x021E,  
    NATIVE_MATH_LOG2     = 0x021F,  
    NATIVE_MATH_CBRT     = 0x0220,  
    NATIVE_MATH_HYPOT    = 0x0221,  
    NATIVE_MATH_GCD      = 0x0222,  
    NATIVE_MATH_LCM      = 0x0223,  
    NATIVE_MATH_FACTORIAL= 0x0224,  
    NATIVE_MATH_DEG      = 0x0225,  
    NATIVE_MATH_RAD      = 0x0226,  

    
    NATIVE_BIN_WRITE     = 0x0300,  
    NATIVE_BIN_READ      = 0x0301,  
    NATIVE_BIN_WRITE_NUM = 0x0302,

    
    NATIVE_RANGE         = 0x0400,  
    NATIVE_STR           = 0x0401,  
    NATIVE_LEN           = 0x0402,  
    
    NATIVE_TYPE_INT      = 0x0403,  
    NATIVE_TYPE_FLOAT    = 0x0404,  
    NATIVE_TYPE_BOOL     = 0x0405,  
    NATIVE_STR_FORMAT    = 0x0406,  
    NATIVE_STR_BYTES     = 0x0407,  
    NATIVE_STR_FROM_BYTES= 0x0408,  
    NATIVE_STR_ORD       = 0x0409,  
    NATIVE_STR_CHR       = 0x040A,  
    
    NATIVE_OS_POPEN      = 0x000E,  
    NATIVE_OS_CPU_COUNT  = 0x000F,  
    
    NATIVE_JSON_PARSE      = 0x0600, 
    NATIVE_JSON_STRINGIFY  = 0x0601, 

    
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

extern char **g_runtime_argv;
extern int    g_runtime_argc;

#endif
