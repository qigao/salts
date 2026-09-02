#ifndef TURBO_API_H
#define TURBO_API_H

/* Core linkage markers. Build-system producer/consumer state is supplied by
 * the Rocida::Core target; this header does not inspect CMake internals. */
#ifndef TURBO_API
  #if !defined(_WIN32) && defined(__GNUC__) && __GNUC__ >= 4
    #define TURBO_API __attribute__((visibility("default")))
  #else
    #define TURBO_API
  #endif
#endif

#ifndef TURBO_C_API
  #ifdef __cplusplus
    #define TURBO_C_API extern "C" TURBO_API
  #else
    #define TURBO_C_API TURBO_API
  #endif
#endif

#endif /* TURBO_API_H */
