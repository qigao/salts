#ifndef SALTS_NATIVE_IO_MODULE_H
#define SALTS_NATIVE_IO_MODULE_H

#ifndef SALTS_NATIVE_IO_API
  #if !defined(_WIN32) && defined(__GNUC__) && __GNUC__ >= 4
    #define SALTS_NATIVE_IO_API __attribute__((visibility("default")))
  #else
    #define SALTS_NATIVE_IO_API
  #endif
#endif

#ifndef SALTS_NATIVE_IO_C_API
  #ifdef __cplusplus
    #define SALTS_NATIVE_IO_C_API extern "C" SALTS_NATIVE_IO_API
  #else
    #define SALTS_NATIVE_IO_C_API SALTS_NATIVE_IO_API
  #endif
#endif

#endif /* SALTS_NATIVE_IO_MODULE_H */
