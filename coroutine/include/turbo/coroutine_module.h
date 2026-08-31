#ifndef TURBO_COROUTINE_MODULE_H
#define TURBO_COROUTINE_MODULE_H

#ifndef TURBO_COROUTINE_API
  #if !defined(_WIN32) && defined(__GNUC__) && __GNUC__ >= 4
    #define TURBO_COROUTINE_API __attribute__((visibility("default")))
  #else
    #define TURBO_COROUTINE_API
  #endif
#endif

#ifndef TURBO_COROUTINE_C_API
  #ifdef __cplusplus
    #define TURBO_COROUTINE_C_API extern "C" TURBO_COROUTINE_API
  #else
    #define TURBO_COROUTINE_C_API TURBO_COROUTINE_API
  #endif
#endif

#endif /* TURBO_COROUTINE_MODULE_H */
