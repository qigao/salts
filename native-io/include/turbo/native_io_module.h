#ifndef TURBO_NATIVE_IO_MODULE_H
#define TURBO_NATIVE_IO_MODULE_H

#ifndef TURBO_NATIVE_IO_API
  #if !defined(_WIN32) && defined(__GNUC__) && __GNUC__ >= 4
    #define TURBO_NATIVE_IO_API __attribute__((visibility("default")))
  #else
    #define TURBO_NATIVE_IO_API
  #endif
#endif

#ifndef TURBO_NATIVE_IO_C_API
  #ifdef __cplusplus
    #define TURBO_NATIVE_IO_C_API extern "C" TURBO_NATIVE_IO_API
  #else
    #define TURBO_NATIVE_IO_C_API TURBO_NATIVE_IO_API
  #endif
#endif

#endif /* TURBO_NATIVE_IO_MODULE_H */
