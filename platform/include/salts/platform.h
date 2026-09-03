#ifndef SALTS_PLATFORM_MODULE_H
#define SALTS_PLATFORM_MODULE_H

/* Platform owns its linkage contract. It never reuses Core SALTS_API state. */
#ifndef SALTS_PLATFORM_API
  #if !defined(_WIN32) && defined(__GNUC__) && __GNUC__ >= 4
    #define SALTS_PLATFORM_API __attribute__((visibility("default")))
  #else
    #define SALTS_PLATFORM_API
  #endif
#endif

#ifndef SALTS_PLATFORM_C_API
  #ifdef __cplusplus
    #define SALTS_PLATFORM_C_API extern "C" SALTS_PLATFORM_API
  #else
    #define SALTS_PLATFORM_C_API SALTS_PLATFORM_API
  #endif
#endif

#endif /* SALTS_PLATFORM_MODULE_H */
