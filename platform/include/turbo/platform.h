#ifndef TURBO_PLATFORM_MODULE_H
#define TURBO_PLATFORM_MODULE_H

/* Platform owns its linkage contract. It never reuses Core TURBO_API state. */
#ifndef TURBO_PLATFORM_API
  #if !defined(_WIN32) && defined(__GNUC__) && __GNUC__ >= 4
    #define TURBO_PLATFORM_API __attribute__((visibility("default")))
  #else
    #define TURBO_PLATFORM_API
  #endif
#endif

#ifndef TURBO_PLATFORM_C_API
  #ifdef __cplusplus
    #define TURBO_PLATFORM_C_API extern "C" TURBO_PLATFORM_API
  #else
    #define TURBO_PLATFORM_C_API TURBO_PLATFORM_API
  #endif
#endif

#endif /* TURBO_PLATFORM_MODULE_H */
