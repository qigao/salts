/*
 * tinytest.h - Header-only BDD/TDD testing framework for C and C++
 *
 * Version: 1.0.0
 * License: MIT
 *
 * A single-header testing library providing:
 *   - BDD syntax:  spec/describe/it/before/after/before_each/after_each
 *   - TDD syntax:  suite/section/it/check macros
 *   - Typed assertions: check_int_eq, check_str_eq, check_float_eq, etc.
 *   - C++ template, container, string, and exception assertions are provided by
 *     tinytest.hpp; include that header from C++ tests.
 *   - Benchmarking: benchmark_batch/benchmark_ops/benchmark_bytes/benchmark_io
 *   - Output formats: colored console, TAP, JUnit XML
 *   - Test filtering: --filter, --list, focus (fit/it_only), skip (xit)
 *
 * Usage:
 *   #include "tinytest.h" (C tests)
 *   #include "tinytest.hpp" (C++ tests)
 *
 *   spec("my module") {
 *       describe("feature") {
 *           it("should work") {
 *               check(1 + 1 == 2);
 *           }
 *       }
 *   }
 *
 * Define TINYTEST_NO_MAIN before including this header to suppress the
 * built-in main() function (useful for custom test runners).
 */

#ifndef TINYTEST_H
#define TINYTEST_H

/* C++ compatibility */
#ifdef __cplusplus
  #include <exception>
extern "C" {
#endif

#ifdef _WIN32
  #include <stdio.h>
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <Windows.h>
  #include <direct.h>
  #include <io.h>
  #include <sys/stat.h>
  #define TTEST_IS_ATTY__() _isatty(_fileno(stdout))
#else
  #ifndef _POSIX_C_SOURCE
    /* This definition is required for `fileno` to be defined */
    #define _POSIX_C_SOURCE 200809L
  #endif
  #include <dirent.h>
  #include <stdio.h>
  #include <sys/stat.h>
  #include <unistd.h>
  #define TTEST_IS_ATTY__() isatty(fileno(stdout))
#endif

#include <float.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
/* C++ has bool built-in, C needs stdbool.h */
#ifndef __cplusplus
  #include <stdbool.h>
#endif

#ifdef _MSC_VER
  #pragma warning(push)
  #pragma warning(disable : 4130)
  #pragma warning(disable : 4702) /* fail-fast longjmp paths can leave marker returns unreachable */
  #pragma warning(disable : 4996) /* _CRT_SECURE_NO_WARNINGS */
  #pragma warning(                                                                                 \
      disable : 4127) /* conditional expression is constant (check macros with constant args) */
#endif

#ifndef TT_USE_COLOR
  #define TT_USE_COLOR 1
#endif

#ifndef TT_USE_TAP
  #define TT_USE_TAP 0
#endif

#ifndef TT_BENCH_NAME_WIDTH
  #define TT_BENCH_NAME_WIDTH 32
#endif

#ifndef TT_BENCH_TABLE
  #define TT_BENCH_TABLE 1
#endif

#if defined(__clang__)
  #define TTEST_NO_SANITIZE_ADDRESS__ __attribute__((no_sanitize("address")))
#elif defined(__GNUC__)
  #define TTEST_NO_SANITIZE_ADDRESS__ __attribute__((no_sanitize_address))
#else
  #define TTEST_NO_SANITIZE_ADDRESS__
#endif

/* _Pragma-based diagnostic suppression for GCC/Clang.
 * Applied around macro bodies that declare internal variables (to suppress
 * -Wshadow when expanded inside user scopes) and that use !!(x) expressions
 * (to suppress -Wunused-value on some compilers).
 * MSVC diagnostic suppression is handled by the file-level #pragma warning above. */
#if defined(__clang__)
  #define TTEST_DIAG_PUSH__                _Pragma("clang diagnostic push")
  #define TTEST_DIAG_POP__                 _Pragma("clang diagnostic pop")
  #define TTEST_DIAG_IGNORE_SHADOW__       _Pragma("clang diagnostic ignored \"-Wshadow\"")
  #define TTEST_DIAG_IGNORE_UNUSED_VALUE__ _Pragma("clang diagnostic ignored \"-Wunused-value\"")
#elif defined(__GNUC__)
  #define TTEST_DIAG_PUSH__                _Pragma("GCC diagnostic push")
  #define TTEST_DIAG_POP__                 _Pragma("GCC diagnostic pop")
  #define TTEST_DIAG_IGNORE_SHADOW__       _Pragma("GCC diagnostic ignored \"-Wshadow\"")
  #define TTEST_DIAG_IGNORE_UNUSED_VALUE__ _Pragma("GCC diagnostic ignored \"-Wunused-value\"")
#else
  #define TTEST_DIAG_PUSH__
  #define TTEST_DIAG_POP__
  #define TTEST_DIAG_IGNORE_SHADOW__
  #define TTEST_DIAG_IGNORE_UNUSED_VALUE__
#endif

/* Cast macros to avoid -Wold-style-cast in C++ */
#ifdef __cplusplus
  #define TTEST_CAST(type, expr) static_cast<type>(expr)
  #define TTEST_REINTERPRET_CAST(type, expr) reinterpret_cast<type>(expr)
  #define TTEST_CONST_CAST(type, expr) const_cast<type>(expr)
#else
  #define TTEST_CAST(type, expr) ((type)(expr))
  #define TTEST_REINTERPRET_CAST(type, expr) ((type)(expr))
  #define TTEST_CONST_CAST(type, expr) ((type)(expr))
#endif

#define TTEST_COLOR_RESET__ "\x1B[0m"
#define TTEST_COLOR_RED__ "\x1B[31m"
#define TTEST_COLOR_GREEN__ "\x1B[32m"
#define TTEST_COLOR_YELLOW__ "\x1B[33m"
#define TTEST_COLOR_BOLD__ "\x1B[1m"
#define TTEST_COLOR_MAGENTA__ "\x1B[35m"

#ifndef TTEST_TLS
  #if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
    #define TTEST_TLS _Thread_local
  #elif defined(_MSC_VER)
    #define TTEST_TLS __declspec(thread)
  #elif defined(__GNUC__) || defined(__clang__)
    #define TTEST_TLS __thread
  #else
    #define TTEST_TLS
  #endif
#endif

/* Cross-TU shared globals for header-only library */
#if defined(_MSC_VER)
  #define TTEST_SELECTANY __declspec(selectany)
#else
  #define TTEST_SELECTANY __attribute__((weak))
#endif

/* Cross-platform high-resolution timer */
static inline double ttest_get_time_ms__(void) {
#ifdef _WIN32
  LARGE_INTEGER frequency, counter;
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&counter);
  return TTEST_CAST(double, counter.QuadPart) * 1000.0 / TTEST_CAST(double, frequency.QuadPart);
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return TTEST_CAST(double, ts.tv_sec) * 1000.0 + TTEST_CAST(double, ts.tv_nsec) / 1000000.0;
#endif
}

static void ttest_indent__(FILE *fp, size_t level);

typedef struct ttest_config_type__ ttest_config_type__;
typedef void (*ttest_spec_fn__)(ttest_config_type__ *ttest_config__);

typedef enum ttest_error_code__ {
  TTEST_ERR_OK__ = 0,
  TTEST_ERR_IO__ = -1,
  TTEST_ERR_TIME__ = -2,
  TTEST_ERR_FORMAT__ = -3
} ttest_error_code__;

typedef struct ttest_result__ {
  bool ok;
  ttest_error_code__ error;
  const char *message;
} ttest_result__;

static inline ttest_result__ ttest_result_ok__(void) {
  ttest_result__ result = {true, TTEST_ERR_OK__, NULL};
  return result;
}

static inline ttest_result__ ttest_result_error__(ttest_error_code__ error, const char *message) {
  ttest_result__ result = {false, error, message};
  return result;
}

typedef struct ttest_spec_entry__ {
  const char *name;
  ttest_spec_fn__ fn;
  struct ttest_spec_entry__ *next;
} ttest_spec_entry__;

TTEST_SELECTANY ttest_spec_entry__ *ttest_specs__ = NULL;
TTEST_SELECTANY size_t ttest_spec_count__ = 0;

static void ttest_register_spec__(const char *name, ttest_spec_fn__ fn) {
  ttest_spec_entry__ *e = TTEST_CAST(ttest_spec_entry__ *, malloc(sizeof(ttest_spec_entry__)));
  if (!e) {
    perror("malloc(spec)");
    abort();
  }
  e->name = name;
  e->fn = fn;
  e->next = ttest_specs__;
  ttest_specs__ = e;
  ttest_spec_count__++;
}

static ttest_spec_entry__ *ttest_get_spec_entry__(size_t index) {
  ttest_spec_entry__ *e = ttest_specs__;
  for (size_t i = 0; i < index && e; ++i) {
    e = e->next;
  }
  return e;
}

static void ttest_cleanup_specs__(void) {
  ttest_spec_entry__ *e = ttest_specs__;
  while (e) {
    ttest_spec_entry__ *next = e->next;
    free(e);
    e = next;
  }
  ttest_specs__ = NULL;
  ttest_spec_count__ = 0;
}

#if defined(__cplusplus)
  #define TTEST_CONSTRUCTOR__(fn)                                                                  \
    static void fn(void);                                                                          \
    namespace {                                                                                    \
      struct TTEST_CAT2(ttest_ctor_struct_, fn) {                                                  \
        TTEST_CAT2(ttest_ctor_struct_, fn)() { fn(); }                                             \
      };                                                                                           \
      static TTEST_CAT2(ttest_ctor_struct_, fn) TTEST_CAT2(ttest_ctor_obj_, fn);                   \
    }                                                                                              \
    static void fn(void)
#elif defined(_WIN32) && defined(__clang__)
  #ifdef read
    #pragma push_macro("read")
    #undef read
    #define TTEST_POP_READ__ 1
  #endif
  #pragma section(".CRT$XCU", read)
  #ifdef TTEST_POP_READ__
    #pragma pop_macro("read")
    #undef TTEST_POP_READ__
  #endif
typedef void(__cdecl *ttest_ctor_fn__)(void);
  #define TTEST_CONSTRUCTOR__(fn)                                                                  \
    static void __cdecl fn(void);                                                                  \
    __declspec(allocate(".CRT$XCU"))                                                               \
    __attribute__((used)) static ttest_ctor_fn__ TTEST_CAT2(ttest_ctor_, fn) = fn;                 \
    static void __cdecl fn(void)
#elif defined(_MSC_VER)
  #ifdef read
    #pragma push_macro("read")
    #undef read
    #define TTEST_POP_READ__ 1
  #endif
  #pragma section(".CRT$XCU", read)
  #ifdef TTEST_POP_READ__
    #pragma pop_macro("read")
    #undef TTEST_POP_READ__
  #endif
typedef void(__cdecl *ttest_ctor_fn__)(void);
  #define TTEST_CONSTRUCTOR__(fn)                                                                  \
    static void __cdecl fn(void);                                                                  \
    __declspec(allocate(".CRT$XCU")) static ttest_ctor_fn__ TTEST_CAT2(ttest_ctor_, fn) = fn;      \
    static void __cdecl fn(void)
#elif defined(__GNUC__) || defined(__clang__)
  #define TTEST_CONSTRUCTOR__(fn)                                                                  \
    static void fn(void) __attribute__((constructor));                                             \
    static void fn(void)
#else
  #define TTEST_CONSTRUCTOR__(fn) static void fn(void)
#endif

static inline void ttest_bench_print_header__(ttest_config_type__ *config, size_t level);

static inline void ttest_bench_print__(
    ttest_config_type__ *config, const char *title, size_t samples, double sum_ms, double min_ms,
    double max_ms, size_t operations_per_sample, size_t bytes_per_sample, bool tracks_bytes,
    size_t level, bool use_color);

static inline void ttest_bench_reset__(ttest_config_type__ *config);

/* Simple file helpers (cross-platform) */
static inline char *tt_temp_dir(void) {
#ifdef _WIN32
  static char path[MAX_PATH];
  DWORD n = GetTempPathA((DWORD)sizeof(path), path);
  if (n == 0 || n >= sizeof(path)) {
    const char *env = getenv("TEMP");
    if (!env) env = getenv("TMP");
    if (!env) env = ".";
    char *res = TTEST_CAST(char *, malloc(strlen(env) + 1));
    if (res) strcpy(res, env);
    return res;
  }
  char *res = TTEST_CAST(char *, malloc(strlen(path) + 1));
  if (res) strcpy(res, path);
  return res;
#else
  const char *tmp = getenv("TMPDIR");
  if (!tmp) tmp = getenv("TMP");
  if (!tmp) tmp = getenv("TEMP");
  if (!tmp) tmp = "/tmp";
  char *res = TTEST_CAST(char *, malloc(strlen(tmp) + 1));
  if (res) strcpy(res, tmp);
  return res;
#endif
}

static inline char *tt_read_file(const char *path, size_t *out_size) {
  FILE *fp = fopen(path, "rb");
  long size;
  char *buf;
  if (!fp) return NULL;
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  size = ftell(fp);
  if (size < 0) {
    fclose(fp);
    return NULL;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }
  buf = TTEST_CAST(char *, malloc((size_t)size + 1));
  if (!buf) {
    fclose(fp);
    return NULL;
  }
  if (size > 0 && fread(buf, 1, (size_t)size, fp) != (size_t)size) {
    fclose(fp);
    free(buf);
    return NULL;
  }
  fclose(fp);
  buf[size] = '\0';
  if (out_size) *out_size = (size_t)size;
  return buf;
}

static inline int tt_write_file(const char *path, const void *data, size_t size) {
  FILE *fp = fopen(path, "wb");
  if (!fp) return -1;
  if (size > 0 && fwrite(data, 1, size, fp) != size) {
    fclose(fp);
    return -1;
  }
  fclose(fp);
  return 0;
}

static inline int tt_remove_file(const char *path) {
#ifdef _WIN32
  DWORD attrs = GetFileAttributesA(path);
  if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) &&
      (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
    return RemoveDirectoryA(path) ? 0 : -1;
  }
  return DeleteFileA(path) ? 0 : -1;
#else
  return unlink(path) == 0 ? 0 : -1;
#endif
}

static inline int tt_make_dir(const char *path) {
#ifdef _WIN32
  return _mkdir(path) == 0 ? 0 : -1;
#else
  return mkdir(path, 0777) == 0 ? 0 : -1;
#endif
}

static inline int tt_is_dir(const char *path) {
#ifdef _WIN32
  DWORD attrs = GetFileAttributesA(path);
  if (attrs == INVALID_FILE_ATTRIBUTES) return 0;
  return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0 && (attrs & FILE_ATTRIBUTE_REPARSE_POINT) == 0;
#else
  struct stat st;
  if (lstat(path, &st) != 0) return 0;
  return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

static inline char *tt_make_temp_file(const char *prefix, const char *suffix) {
  const char *pre = prefix ? prefix : "tt";
  const char *suf = suffix ? suffix : "";
#ifdef _WIN32
  char base[MAX_PATH];
  char file[MAX_PATH];
  if (GetTempPathA((DWORD)sizeof(base), base) == 0) return NULL;
  if (GetTempFileNameA(base, pre, 0, file) == 0) return NULL;
  if (suf[0]) {
    char renamed[MAX_PATH];
    size_t flen = strlen(file);
    int strip_tmp = (flen >= 4 && strcmp(file + flen - 4, ".tmp") == 0) ? 4 : 0;
    /* GetTempFileNameA always appends ".tmp"; replace it with the requested
     * suffix so semantics match the POSIX path. Fail fast on rename errors. */
    snprintf(renamed, sizeof(renamed), "%.*s%s", (int)(flen - strip_tmp), file, suf);
    if (MoveFileExA(file, renamed, MOVEFILE_REPLACE_EXISTING)) {
      strncpy(file, renamed, sizeof(file) - 1);
      file[sizeof(file) - 1] = '\0';
    } else {
      DeleteFileA(file);
      return NULL;
    }
  }
  return _strdup(file);
#else
  char *dir = tt_temp_dir();
  if (!dir) return NULL;
  char tmpl[512];
  int fd;
  snprintf(tmpl, sizeof(tmpl), "%s/%sXXXXXX", dir, pre);
  fd = mkstemp(tmpl);
  if (fd < 0) {
    free(dir);
    return NULL;
  }
  close(fd);
  if (suf[0]) {
    char renamed[512];
    snprintf(renamed, sizeof(renamed), "%s%s", tmpl, suf);
    if (rename(tmpl, renamed) == 0) {
      free(dir);
      return strdup(renamed);
    }
    unlink(tmpl);
    free(dir);
    return NULL;
  }
  free(dir);
  return strdup(tmpl);
#endif
}

static inline char *tt_make_temp_dir(const char *prefix) {
  const char *pre = prefix ? prefix : "tt";
#ifdef _WIN32
  char base[MAX_PATH];
  char file[MAX_PATH];
  if (GetTempPathA((DWORD)sizeof(base), base) == 0) return NULL;
  if (GetTempFileNameA(base, pre, 0, file) == 0) return NULL;
  DeleteFileA(file);
  if (CreateDirectoryA(file, NULL) == 0) return NULL;
  return _strdup(file);
#else
  char *dir = tt_temp_dir();
  if (!dir) return NULL;
  char tmpl[512];
  snprintf(tmpl, sizeof(tmpl), "%s/%sXXXXXX", dir, pre);
  if (!mkdtemp(tmpl)) {
    free(dir);
    return NULL;
  }
  free(dir);
  return strdup(tmpl);
#endif
}

static inline int tt_remove_tree(const char *path) {
  if (!path || !path[0]) return -1;
  if (!tt_is_dir(path)) return tt_remove_file(path);
#ifdef _WIN32
  char pattern[MAX_PATH];
  WIN32_FIND_DATAA fdata;
  HANDLE h;
  snprintf(pattern, sizeof(pattern), "%s\\*", path);
  h = FindFirstFileA(pattern, &fdata);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      const char *name = fdata.cFileName;
      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
      char child[MAX_PATH];
      snprintf(child, sizeof(child), "%s\\%s", path, name);
      if ((fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
          (fdata.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
        tt_remove_tree(child);
      } else {
        tt_remove_file(child);
      }
    } while (FindNextFileA(h, &fdata));
    FindClose(h);
  }
  return RemoveDirectoryA(path) ? 0 : -1;
#else
  DIR *dir = opendir(path);
  if (dir) {
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
      const char *name = ent->d_name;
      if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
      char child[1024];
      snprintf(child, sizeof(child), "%s/%s", path, name);
      if (tt_is_dir(child)) {
        tt_remove_tree(child);
      } else {
        unlink(child);
      }
    }
    closedir(dir);
  }
  return rmdir(path) == 0 ? 0 : -1;
#endif
}

typedef struct ttest_array__ {
  void **values;
  size_t capacity;
  size_t size;
} ttest_array__;

static inline ttest_array__ *ttest_array_create__(void) {
  ttest_array__ *arr = TTEST_CAST(ttest_array__ *, malloc(sizeof(ttest_array__)));
  if (!arr) {
    perror("malloc(array)");
    abort();
  }
  arr->capacity = 4;
  arr->size = 0;
  arr->values = TTEST_CAST(void **, calloc(arr->capacity, sizeof(void *)));
  if (!arr->values) {
    perror("calloc(array->values)");
    free(arr);
    abort();
  }
  return arr;
}

static inline void *ttest_array_push__(ttest_array__ *arr, void *item) {
  if (arr->size == arr->capacity) {
    arr->capacity *= 2;
    void **v = TTEST_CAST(void **, realloc(arr->values, sizeof(void *) * arr->capacity));
    if (!v) {
      perror("realloc(array)");
      abort();
    }
    arr->values = v;
  }
  arr->values[arr->size++] = item;
  return item;
}

static inline void *ttest_array_last__(ttest_array__ *arr) {
  if (arr->size == 0) {
    return NULL;
  }
  return arr->values[arr->size - 1];
}

static inline void *ttest_array_pop__(ttest_array__ *arr) {
  if (arr->size == 0) {
    return NULL;
  }
  void *result = arr->values[arr->size - 1];
  --arr->size;
  return result;
}

static inline void ttest_array_free__(ttest_array__ *arr) {
  if (arr) {
    free(arr->values);
    free(arr);
  }
}

static inline ttest_array__ *ttest_array_get_or_create__(ttest_array__ **arr_ptr) {
  if (!*arr_ptr) {
    *arr_ptr = ttest_array_create__();
  }
  return *arr_ptr;
}

static inline size_t ttest_array_size__(ttest_array__ *arr) { return arr ? arr->size : 0; }

typedef enum ttest_node_type__ {
  TTEST_NODE_GROUP__ = 1,
  TTEST_NODE_TEST__ = 2,
  TTEST_NODE_INTERIM__ = 3
} ttest_node_type__;

enum {
  TTEST_RESULT_CATEGORY_NONE__ = 0,
  TTEST_RESULT_CATEGORY_PASS__,
  TTEST_RESULT_CATEGORY_FAIL__,
  TTEST_RESULT_CATEGORY_SKIP__,
  TTEST_RESULT_CATEGORY_FILTER__,
  TTEST_RESULT_CATEGORY_TODO__
};

/*
 * Single source of truth for test-result enum values, display names, and
 * downstream classification (JUnit, TAP, and console summaries).
 */
#define TTEST_TEST_RESULT_X__                                                                     \
  X(PENDING, 0, "pending", TTEST_RESULT_CATEGORY_NONE__)                                          \
  X(PASSED, 1, "passed", TTEST_RESULT_CATEGORY_PASS__)                                            \
  X(FAILED, 2, "failed", TTEST_RESULT_CATEGORY_FAIL__)                                            \
  X(SKIPPED, 3, "skipped", TTEST_RESULT_CATEGORY_SKIP__)                                         \
  X(EXPECTED_FAIL, 4, "expected_fail", TTEST_RESULT_CATEGORY_PASS__)                              \
  X(UNEXPECTED_PASS, 5, "unexpected_pass", TTEST_RESULT_CATEGORY_TODO__)                          \
  X(FILTERED, 6, "filtered", TTEST_RESULT_CATEGORY_FILTER__)

typedef enum ttest_test_result__ {
#define X(name, value, label, category) TTEST_RESULT_##name##__ = value,
  TTEST_TEST_RESULT_X__
#undef X
} ttest_test_result__;

static inline const char *ttest_test_result_name__(ttest_test_result__ result) {
  switch (result) {
#define X(name, value, label, category)                                                          \
    case TTEST_RESULT_##name##__:                                                                \
      return label;
    TTEST_TEST_RESULT_X__
#undef X
    default:
      return "unknown";
  }
}

static inline int ttest_test_result_category__(ttest_test_result__ result) {
  switch (result) {
#define X(name, value, label, category)                                                          \
    case TTEST_RESULT_##name##__:                                                                \
      return category;
    TTEST_TEST_RESULT_X__
#undef X
    default:
      return TTEST_RESULT_CATEGORY_NONE__;
  }
}

static inline bool ttest_test_result_is_skip__(ttest_test_result__ result) {
  const int category = ttest_test_result_category__(result);
  return category == TTEST_RESULT_CATEGORY_SKIP__ || category == TTEST_RESULT_CATEGORY_FILTER__;
}

static inline bool ttest_test_result_is_fail__(ttest_test_result__ result) {
  const int category = ttest_test_result_category__(result);
  return category == TTEST_RESULT_CATEGORY_FAIL__ || category == TTEST_RESULT_CATEGORY_TODO__;
}

typedef enum ttest_node_flags__ {
  ttest_node_flags_none__ = 0,
  ttest_node_flags_focus__ = 1 << 0,
  ttest_node_flags_skip__ = 1 << 1,
  ttest_node_flags_expected_fail__ = 1 << 2,
  ttest_node_flags_benchmark__ = 1 << 3,
} ttest_node_flags__;

typedef struct ttest_test_step__ {
  size_t level;
  int id;
  char *name;
  ttest_node_type__ type;
  ttest_node_flags__ flags;
  bool executed;
  bool passed;
  ttest_test_result__ result;
  const char *skip_reason;
  char *failure_message;
  char *failure_location;
  double execution_time_ms;
  char *full_path; /* Full hierarchical path like "Calculator.should add two numbers" */
  ttest_array__ *before_each_nodes;
  ttest_array__ *after_each_nodes;
} ttest_test_step__;

typedef struct ttest_bench_entry__ {
  const char *title;
  size_t samples;
  size_t operations_per_sample;
  size_t bytes_per_sample;
  bool tracks_bytes;
  double avg_op_us;
  double min_sample_us;
  double max_sample_us;
  double ops_s;
  double mib_s;
} ttest_bench_entry__;

typedef struct ttest_node__ {
  int id;
  int next_node_id;
  char *name;
  ttest_node_flags__ flags;
  ttest_node_type__ type;
  ttest_array__ *list_before;
  ttest_array__ *list_after;
  ttest_array__ *list_before_each;
  ttest_array__ *list_after_each;
  ttest_array__ *list_children;
} ttest_node__;

enum ttest_run_type__ { TTEST_INIT_RUN__ = 1, TTEST_TEST_RUN__ = 2 };

typedef struct ttest_config_type__ {
  enum ttest_run_type__ run;
  int id;
  size_t test_index;
  size_t test_tap_index;
  int target_node_id;
  size_t failed_test_count;
  ttest_test_step__ *current_test;
  ttest_array__ *node_stack;
  ttest_array__ *nodes;
  char *error;
  char *location;
  bool use_color;
  bool use_tap;
  bool has_focus_nodes;
  const char *junit_file;
  size_t warn_count;
  size_t assertion_count;
  size_t assertion_failed_count;
  char info_buffer[2048];
  size_t info_len;
  char location_buf[512];
  jmp_buf jump_buffer;
  bool skip_subsequent;
  bool list_only;
  const char *filter;
  int bench_header_printed;  /* replaces static ttest_bench_header_printed__ */
  size_t bench_header_level; /* replaces static ttest_bench_header_level__   */
} ttest_config_type__;

#ifdef __cplusplus
class ttest_fail_exception__ {};
#endif

static TTEST_NO_SANITIZE_ADDRESS__ void ttest_longjmp_fail__(ttest_config_type__ *config)
#ifdef __cplusplus
    noexcept(false)
#endif
{
  if (!config) {
    abort();
  }
#ifdef __cplusplus
  throw ttest_fail_exception__();
#else
  longjmp(config->jump_buffer, 1);
#endif
}

static inline void ttest_bench_reset__(ttest_config_type__ *config) {
  config->bench_header_printed = 0;
  config->bench_header_level = 0;
}

static inline ttest_bench_entry__ ttest_bench_make_entry__(
    const char *title, size_t samples, double sum_ms, double min_ms, double max_ms,
    size_t operations_per_sample, size_t bytes_per_sample, bool tracks_bytes) {
  const double bytes_per_mib = 1024.0 * 1024.0;
  const double elapsed_s = sum_ms / 1000.0;
  const double total_operations = (double)samples * (double)operations_per_sample;
  ttest_bench_entry__ entry;

  memset(&entry, 0, sizeof(entry));
  entry.title = title;
  entry.samples = samples;
  entry.operations_per_sample = operations_per_sample;
  entry.bytes_per_sample = tracks_bytes ? bytes_per_sample : 0;
  entry.tracks_bytes = tracks_bytes;
  entry.avg_op_us = total_operations > 0.0 ? (sum_ms * 1000.0) / total_operations : 0.0;
  entry.min_sample_us = min_ms * 1000.0;
  entry.max_sample_us = max_ms * 1000.0;
  entry.ops_s = elapsed_s > 0.0 ? total_operations / elapsed_s : 0.0;
  entry.mib_s = tracks_bytes && elapsed_s > 0.0
                    ? ((double)samples * (double)bytes_per_sample) / elapsed_s / bytes_per_mib
                    : 0.0;
  return entry;
}

static inline void ttest_bench_format_optional_metrics__(const ttest_bench_entry__ *entry,
                                                         char *bytes, size_t bytes_cap, char *mib_s,
                                                         size_t mib_s_cap) {
  if (entry->tracks_bytes) {
    snprintf(bytes, bytes_cap, "%zu", entry->bytes_per_sample);
    snprintf(mib_s, mib_s_cap, "%.2f", entry->mib_s);
  } else {
    snprintf(bytes, bytes_cap, "-");
    snprintf(mib_s, mib_s_cap, "-");
  }
}

static inline ttest_test_step__ *ttest_test_step_create__(size_t level, ttest_node__ *node) {
  ttest_test_step__ *step = TTEST_CAST(ttest_test_step__ *, malloc(sizeof(ttest_test_step__)));
  if (!step) {
    perror("malloc(step)");
    abort();
  }
  step->id = node->id;
  step->level = level;
  step->type = node->type;
  step->name = node->name;
  step->flags = node->flags;
  step->executed = false;
  step->passed = false;
  step->result = TTEST_RESULT_PENDING__;
  step->skip_reason = NULL;
  step->failure_message = NULL;
  step->failure_location = NULL;
  step->execution_time_ms = 0.0;
  step->full_path = NULL;
  step->before_each_nodes = NULL;
  step->after_each_nodes = NULL;
  return step;
}

static inline void ttest_test_step_free__(ttest_test_step__ *step) {
  if (step) {
    free(step->failure_message);
    free(step->failure_location);
    free(step->full_path);
    ttest_array_free__(step->before_each_nodes);
    ttest_array_free__(step->after_each_nodes);
    free(step);
  }
}

static char *ttest_build_full_path__(ttest_test_step__ **group_stack, int stack_depth,
                                     const char *test_name) {
  size_t total_len = 0;
  char *path;
  char *dst;

  for (int i = 1; i < stack_depth; ++i) {
    const char *segment = (group_stack[i] != NULL) ? group_stack[i]->name : NULL;
    if (!segment || segment[0] == '\0') continue;
    if (total_len > 0) total_len += 1;
    total_len += strlen(segment);
  }

  if (test_name && test_name[0] != '\0') {
    if (total_len > 0) total_len += 1;
    total_len += strlen(test_name);
  }

  if (total_len == 0) return NULL;

  path = TTEST_CAST(char *, malloc(total_len + 1));
  if (!path) {
    perror("malloc(full_path)");
    abort();
  }

  dst = path;
  for (int i = 1; i < stack_depth; ++i) {
    const char *segment = (group_stack[i] != NULL) ? group_stack[i]->name : NULL;
    size_t len;
    if (!segment || segment[0] == '\0') continue;
    if (dst != path) *dst++ = '.';
    len = strlen(segment);
    memcpy(dst, segment, len);
    dst += len;
  }

  if (test_name && test_name[0] != '\0') {
    size_t len = strlen(test_name);
    if (dst != path) *dst++ = '.';
    memcpy(dst, test_name, len);
    dst += len;
  }

  *dst = '\0';
  return path;
}

static inline ttest_node__ *ttest_node_create__(int id, const char *name, ttest_node_type__ type,
                                                ttest_node_flags__ flags) {
  ttest_node__ *n = TTEST_CAST(ttest_node__ *, malloc(sizeof(ttest_node__)));
  if (!n) {
    perror("malloc(node)");
    abort();
  }
  n->id = id;
  n->next_node_id = id + 1;
  n->name = TTEST_CONST_CAST(char *, name);
  n->type = type;
  n->flags = flags;
  n->list_before = NULL;
  n->list_after = NULL;
  n->list_before_each = NULL;
  n->list_after_each = NULL;
  n->list_children = NULL;
  return n;
}

static inline bool ttest_node_is_leaf__(ttest_node__ *node) {
  return !node->list_children || node->list_children->size == 0;
}

static void ttest_node_flatten_internal__(ttest_config_type__ *config, size_t level,
                                          ttest_node__ *node, ttest_array__ *steps,
                                          ttest_array__ *before_each_lists,
                                          ttest_array__ *after_each_lists) {
  if (ttest_node_is_leaf__(node)) {
    ttest_test_step__ *test_step = ttest_test_step_create__(level, node);

    for (size_t listIndex = 0; listIndex < before_each_lists->size; ++listIndex) {
      ttest_array__ *list = TTEST_CAST(ttest_array__ *, before_each_lists->values[listIndex]);
      for (size_t i = 0; i < ttest_array_size__(list); ++i) {
        ttest_array_push__(ttest_array_get_or_create__(&test_step->before_each_nodes),
                           list->values[i]);
      }
    }

    ttest_array_push__(steps, test_step);

    for (size_t listIndex = 0; listIndex < after_each_lists->size; ++listIndex) {
      size_t reverseListIndex = after_each_lists->size - listIndex - 1;
      ttest_array__ *list = TTEST_CAST(ttest_array__ *, after_each_lists->values[reverseListIndex]);
      for (size_t i = 0; i < ttest_array_size__(list); ++i) {
        ttest_array_push__(ttest_array_get_or_create__(&test_step->after_each_nodes),
                           list->values[i]);
      }
    }
    return;
  }

  ttest_array_push__(steps, ttest_test_step_create__(level, node));

  for (size_t i = 0; i < ttest_array_size__(node->list_before); ++i) {
    ttest_array_push__(steps,
                      ttest_test_step_create__(level + 1,
                                              TTEST_CAST(ttest_node__ *, node->list_before->values[i])));
  }

  ttest_array_push__(before_each_lists, node->list_before_each);
  ttest_array_push__(after_each_lists, node->list_after_each);

  for (size_t i = 0; i < ttest_array_size__(node->list_children); ++i) {
    ttest_node_flatten_internal__(config, level + 1,
                                  TTEST_CAST(ttest_node__ *, node->list_children->values[i]), steps,
                                  before_each_lists, after_each_lists);
  }

  ttest_array_pop__(before_each_lists);
  ttest_array_pop__(after_each_lists);

  for (size_t i = 0; i < ttest_array_size__(node->list_after); ++i) {
    ttest_array_push__(steps,
                      ttest_test_step_create__(level + 1,
                                              TTEST_CAST(ttest_node__ *, node->list_after->values[i])));
  }
}

static ttest_array__ *ttest_node_flatten__(ttest_config_type__ *config, ttest_node__ *node,
                                           ttest_array__ *steps) {
  if (node == NULL) return steps;
  ttest_array__ *before_each_lists = ttest_array_create__();
  ttest_array__ *after_each_lists = ttest_array_create__();
  ttest_node_flatten_internal__(config, 0, node, steps, before_each_lists, after_each_lists);
  ttest_array_free__(before_each_lists);
  ttest_array_free__(after_each_lists);
  return steps;
}

static void ttest_node_free__(ttest_node__ *n) {
  free(n->name);
  ttest_array_free__(n->list_before);
  ttest_array_free__(n->list_after);
  ttest_array_free__(n->list_before_each);
  ttest_array_free__(n->list_after_each);
  ttest_array_free__(n->list_children);
  free(n);
}

TTEST_SELECTANY TTEST_TLS ttest_spec_fn__ ttest_current_spec_fn__ = NULL;
TTEST_SELECTANY TTEST_TLS ttest_config_type__ *ttest_active_config__ = NULL;

static inline void ttest_test_main__(ttest_config_type__ *config) {
  ttest_active_config__ = config;
  if (ttest_current_spec_fn__) ttest_current_spec_fn__(config);
}
static char *ttest_vformat__(const char *format, va_list va);
static char *ttest_format__(const char *format, ...);
static void ttest_tap_failure_diagnostic__(const char *message);

static void ttest_indent__(FILE *fp, size_t level) {
  if (!fp) return;
  for (size_t i = 0; i < level; ++i) fprintf(fp, "  ");
}

static inline void ttest_bench_print_header__(ttest_config_type__ *config, size_t level) {
  (void)config;
#if TT_BENCH_TABLE
  if (!config) return;
  if (config->bench_header_printed && config->bench_header_level == level) return;
  config->bench_header_printed = 1;
  config->bench_header_level = level;
  ttest_indent__(stdout, level);
  printf("  %-*s  %8s  %10s  %12s  %11s  %14s  %14s  %11s  %11s\n", TT_BENCH_NAME_WIDTH,
         "benchmark", "samples", "ops/sample", "bytes/sample", "avg/op(us)", "min/sample(us)",
         "max/sample(us)", "ops/s", "MiB/s");
  ttest_indent__(stdout, level);
  printf("  %-*s  %8s  %10s  %12s  %11s  %14s  %14s  %11s  %11s\n", TT_BENCH_NAME_WIDTH,
         "---------", "-------", "----------", "------------", "----------", "--------------",
         "--------------", "-----", "-----");
#endif
}

static inline void ttest_bench_print__(
    ttest_config_type__ *config, const char *title, size_t samples, double sum_ms, double min_ms,
    double max_ms, size_t operations_per_sample, size_t bytes_per_sample, bool tracks_bytes,
    size_t level, bool use_color) {
  ttest_bench_entry__ e = ttest_bench_make_entry__(title, samples, sum_ms, min_ms, max_ms,
                                                   operations_per_sample, bytes_per_sample,
                                                   tracks_bytes);
  char bytes[32];
  char mib_s[32];
  ttest_bench_format_optional_metrics__(&e, bytes, sizeof(bytes), mib_s, sizeof(mib_s));
  ttest_bench_print_header__(config, level);
  ttest_indent__(stdout, level);
#if TT_BENCH_TABLE
  printf("  %s%-*s%s  %8zu  %10zu  %12s  %11.3f  %14.3f  %14.3f  %11.0f  %11s\n",
         use_color ? TTEST_COLOR_MAGENTA__ : "", TT_BENCH_NAME_WIDTH, e.title,
         use_color ? TTEST_COLOR_RESET__ : "", e.samples, e.operations_per_sample, bytes, e.avg_op_us,
         e.min_sample_us, e.max_sample_us, e.ops_s, mib_s);
#else
  printf("%s%-*s%s  samples=%zu  ops/sample=%zu  bytes/sample=%s  avg/op=%9.3f us  min/sample=%9.3f us  max/sample=%9.3f us  ops/s=%9.0f  MiB/s=%s\n",
         use_color ? TTEST_COLOR_MAGENTA__ : "", TT_BENCH_NAME_WIDTH, e.title,
         use_color ? TTEST_COLOR_RESET__ : "", e.samples, e.operations_per_sample, bytes, e.avg_op_us,
         e.min_sample_us, e.max_sample_us, e.ops_s, mib_s);
#endif
}

/* remainder of file preserved */

#define TTEST_CHECK_IMPL__(condition, file, line, ...)                                              \
  TTEST_DIAG_PUSH__                                                                                \
  TTEST_DIAG_IGNORE_SHADOW__                                                                       \
  TTEST_DIAG_IGNORE_UNUSED_VALUE__                                                                 \
  do {                                                                                             \
    if (!ttest_eval_bool__(!!(condition))) {                                                       \
      if (ttest_active_config__) {                                                                 \
        bool ttest_expected_fail__ = (ttest_active_config__->current_test &&                       \
                                      (ttest_active_config__->current_test->flags &                 \
                                       ttest_node_flags_expected_fail__));                          \
        ++ttest_active_config__->assertion_count;                                                  \
        if (!ttest_expected_fail__) ++ttest_active_config__->assertion_failed_count;               \
        if (ttest_active_config__->run == TTEST_TEST_RUN__ && !ttest_active_config__->error) {    \
          char *ttest_message__ = ttest_format__(__VA_ARGS__);                                     \
          const char *fmt = ttest_active_config__->use_color ? TTEST_FMT_COLOR__ : TTEST_FMT_PLAIN__; \
          snprintf(ttest_active_config__->location_buf, sizeof(ttest_active_config__->location_buf), \
                   "at %s:%s", file, line);                                                       \
          ttest_active_config__->location = ttest_active_config__->location_buf;                   \
          size_t bufflen = strlen(fmt) + strlen(ttest_message__) + 1;                              \
          ttest_active_config__->error = TTEST_CAST(char *, calloc(bufflen, sizeof(char)));        \
          if (ttest_active_config__->use_color)                                                    \
            snprintf(ttest_active_config__->error, bufflen, TTEST_FMT_COLOR__, ttest_message__);   \
          else                                                                                     \
            snprintf(ttest_active_config__->error, bufflen, TTEST_FMT_PLAIN__, ttest_message__);   \
          free(ttest_message__);                                                                   \
          ttest_longjmp_fail__(ttest_active_config__);                                             \
        }                                                                                          \
      }                                                                                            \
    } else {                                                                                       \
      if (ttest_active_config__) ++ttest_active_config__->assertion_count;                         \
    }                                                                                              \
    TTEST_DIAG_POP__                                                                               \
  } while (0)

#define TTEST_WARN_IMPL__(condition, file, line, ...)                                               \
  TTEST_DIAG_PUSH__                                                                                \
  TTEST_DIAG_IGNORE_SHADOW__                                                                       \
  TTEST_DIAG_IGNORE_UNUSED_VALUE__                                                                 \
  do {                                                                                             \
    if (ttest_active_config__ && !ttest_eval_bool__(!!(condition))) {                              \
      char *ttest_message__ = ttest_format__(__VA_ARGS__);                                         \
      ++ttest_active_config__->warn_count;                                                         \
      ttest_indent__(stdout, ttest_active_config__->current_test                                   \
                                 ? ttest_active_config__->current_test->level + 1                  \
                                 : 1);                                                             \
      if (ttest_active_config__->use_color)                                                        \
        printf(TTEST_COLOR_YELLOW__ "Warning:" TTEST_COLOR_RESET__ " %s", ttest_message__);      \
      else                                                                                         \
        printf("Warning: %s", ttest_message__);                                                    \
      printf(" at %s:%s\n", file, line);                                                         \
      free(ttest_message__);                                                                       \
    }                                                                                              \
    TTEST_DIAG_POP__                                                                               \
  } while (0)

#endif /* TINYTEST_H */
