#ifndef TURBO_CAPTURE_EXPORT_H
#define TURBO_CAPTURE_EXPORT_H

#if defined(_WIN32) && defined(TURBO_CAPTURE_SHARED)
#  if defined(TURBO_CAPTURE_BUILD)
#    define TURBO_CAPTURE_API __declspec(dllexport)
#  else
#    define TURBO_CAPTURE_API __declspec(dllimport)
#  endif
#else
#  define TURBO_CAPTURE_API
#endif

#endif
