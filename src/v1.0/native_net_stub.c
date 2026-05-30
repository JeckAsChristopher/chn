
#include "native.h"
#include "error.h"
void net_dispatch(VM *vm, uint16_t id, uint8_t argc, Value *args) {
    (void)vm; (void)id; (void)argc; (void)args;
    error_runtime(0, "network functions require OpenSSL (rebuild with -DHAVE_OPENSSL)");
}
