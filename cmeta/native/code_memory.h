#ifndef CMETA_NATIVE_CODE_MEMORY_H
#define CMETA_NATIVE_CODE_MEMORY_H
#include <cmeta/native/thunk.h>
cmeta_status cmeta_native_memory_create(size_t budget, cmeta_native_thunk *thunk);
cmeta_status cmeta_native_memory_write(cmeta_native_thunk *thunk);
cmeta_status cmeta_native_memory_publish(cmeta_native_thunk *thunk);
cmeta_status cmeta_native_memory_destroy(cmeta_native_thunk *thunk);
#endif
