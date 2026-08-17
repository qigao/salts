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
/* Internal control-flow exception. A failed assertion unwinds C++ frames so
 * destructors run, then is caught at the test boundary. It deliberately does
 * not derive from std::exception so that user catch(...) blocks in tested
 * expressions do not silently absorb it; the framework's own exception
 * macros rethrow it explicitly. */
class ttest_fail_exception__ {};
#endif

static TTEST_NO_SANITIZE_ADDRESS__ void ttest_longjmp_fail__(ttest_config_type__ *config)
#ifdef __cplusplus
    noexcept(false) /* this extern "C" helper intentionally throws */
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
  n->name = TTEST_CONST_CAST(char *, name); /* node takes ownership of name */
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
    ttest_array_push__(
        steps, ttest_test_step_create__(level + 1,
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
    ttest_array_push__(
        steps, ttest_test_step_create__(level + 1,
                                        TTEST_CAST(ttest_node__ *, node->list_after->values[i])));
  }
}

static ttest_array__ *ttest_node_flatten__(ttest_config_type__ *config, ttest_node__ *node,
                                           ttest_array__ *steps) {
  if (node == NULL) {
    return steps;
  }

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
  if (ttest_current_spec_fn__) {
    ttest_current_spec_fn__(config);
  }
}
static char *ttest_vformat__(const char *format, va_list va);
static char *ttest_format__(const char *format, ...);
static void ttest_tap_failure_diagnostic__(const char *message);

static void ttest_indent__(FILE *fp, size_t level) {
  if (!fp) return;
  for (size_t i = 0; i < level; ++i) {
    fprintf(fp, "  ");
  }
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
  ttest_bench_entry__ e =
      ttest_bench_make_entry__(title, samples, sum_ms, min_ms, max_ms, operations_per_sample,
                               bytes_per_sample, tracks_bytes);
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
  printf("%s%-*s%s  samples=%zu  ops/sample=%zu  bytes/sample=%s  avg/op=%9.3f us  "
         "min/sample=%9.3f us  max/sample=%9.3f us  ops/s=%9.0f  MiB/s=%s\n",
         use_color ? TTEST_COLOR_MAGENTA__ : "", TT_BENCH_NAME_WIDTH, e.title,
         use_color ? TTEST_COLOR_RESET__ : "", e.samples, e.operations_per_sample, bytes, e.avg_op_us,
         e.min_sample_us, e.max_sample_us, e.ops_s, mib_s);
#endif
}

static bool ttest_enter_node__(ttest_node_flags__ node_flags, ttest_config_type__ *config,
                               ttest_node_type__ type, ptrdiff_t list_offset, bool format_name,
                               const char *fmt, ...) {
  if (config->run == TTEST_INIT_RUN__) {
    char *name;
    if (format_name) {
      va_list va;
      va_start(va, fmt);
      name = ttest_vformat__(fmt, va);
      va_end(va);
    } else {
      name = strdup(fmt);
      if (!name) {
        perror("strdup(node name)");
        abort();
      }
    }

    ttest_node__ *top = TTEST_CAST(ttest_node__ *, ttest_array_last__(config->node_stack));
    if (!top) {
      fprintf(stderr, "error: node_stack is empty\n");
      abort();
    }
    ttest_array__ **list_ptr = TTEST_REINTERPRET_CAST(
        ttest_array__ **, TTEST_REINTERPRET_CAST(unsigned char *, top) + list_offset);
    ttest_array__ *list = ttest_array_get_or_create__(list_ptr);

    int id = config->id++;
    ttest_node__ *node = ttest_node_create__(id, name, type, node_flags);
    if (node_flags & ttest_node_flags_focus__) {
      top->flags = (ttest_node_flags__)(top->flags | (node_flags & ttest_node_flags_focus__));
      config->has_focus_nodes = true;
    }
    ttest_array_push__(list, node);
    ttest_array_push__(config->nodes, node);
    if (type == TTEST_NODE_GROUP__) {
      ttest_array_push__(config->node_stack, node);
      return true;
    }
    return false;
  }

  /* TEST_RUN: no name allocation needed, use existing node */
  if (config->id >= (int)config->nodes->size) {
    fprintf(stderr, "non-deterministic spec\n");
    abort();
  }
  if (!config->nodes || !config->nodes->values) {
    fprintf(stderr, "error: config->nodes is invalid\n");
    abort();
  }
  ttest_node__ *node = TTEST_CAST(ttest_node__ *, config->nodes->values[config->id]);
  if (!node) {
    fprintf(stderr, "error: node at %d is NULL\n", config->id);
    abort();
  }

  int target_node_id = config->target_node_id;
  bool should_enter = target_node_id >= node->id && target_node_id < node->next_node_id;
  if (should_enter) {
    ttest_array_push__(config->node_stack, node);
    config->id++;
  } else {
    config->id = node->next_node_id;
  }
#if defined(TT_PRINT_TRACE)
  const char *color = config->use_color ? TTEST_COLOR_MAGENTA__ : "";
  fprintf(stderr, "%s% 3d ", color, target_node_id);
  ttest_indent__(stderr, config->node_stack->size - 1 - (int)should_enter);
  const char *reset = config->use_color ? TTEST_COLOR_RESET__ : "";
  fprintf(stderr, "%s [%d, %d) %s%s\n", should_enter ? ">" : "|", node->id, node->next_node_id,
          node->name, reset);
#endif
  return should_enter;
}

static void ttest_exit_node__(ttest_config_type__ *config) {
  ttest_node__ *top = TTEST_CAST(ttest_node__ *, ttest_array_pop__(config->node_stack));
  if (top && config->run == TTEST_INIT_RUN__) {
    top->next_node_id = config->id;
  }
}

static inline const char *ttest_skip_message__(const ttest_test_step__ *step) {
  return (step && step->skip_reason && step->skip_reason[0] != '\0') ? step->skip_reason
                                                                     : "Test was skipped";
}

static void ttest_report_skip__(ttest_config_type__ *config, ttest_test_step__ *step) {
  if (config->run == TTEST_TEST_RUN__ && config->use_tap && config->test_tap_index) {
    printf("ok %zu - %s # SKIP %s\n", config->test_tap_index, step->name,
           ttest_skip_message__(step));
  }
}

static void ttest_report_pass__(ttest_config_type__ *config, ttest_test_step__ *step) {
  if (config->run == TTEST_TEST_RUN__) {
    if (config->use_tap) {
      if (config->test_tap_index) {
        printf("ok %zu - %s\n", config->test_tap_index, step->name);
      }
    } else {
      if (step->flags & ttest_node_flags_benchmark__) {
        return;
      }
      ttest_indent__(stdout, step->level);
      printf("%s[ OK    ]%s", config->use_color ? TTEST_COLOR_GREEN__ : "",
             config->use_color ? TTEST_COLOR_RESET__ : "");
      if (step->execution_time_ms > 0.0) {
        printf(" (%.2fms)", step->execution_time_ms);
      }
      printf("\n");
    }
  }
}

static void ttest_report_unexpected_pass__(ttest_config_type__ *config, ttest_test_step__ *step) {
  ++config->failed_test_count;
  if (config->run == TTEST_TEST_RUN__) {
    if (config->use_tap) {
      if (config->test_tap_index) {
        printf("not ok %zu - %s # TODO was expected to fail but passed\n", config->test_tap_index,
               step->name);
      }
    } else {
      ttest_indent__(stdout, step->level);
      printf("%s[ UPASS ]%s", config->use_color ? TTEST_COLOR_YELLOW__ : "",
             config->use_color ? TTEST_COLOR_RESET__ : "");
      if (step->execution_time_ms > 0.0) {
        printf(" (%.2fms)", step->execution_time_ms);
      }
      printf("\n");
      ttest_indent__(stdout, step->level + 1);
      printf("This test was expected to fail but passed\n");
    }
  }
}

static void ttest_report_expected_fail__(ttest_config_type__ *config, ttest_test_step__ *step) {
  if (config->run == TTEST_TEST_RUN__) {
    if (config->use_tap) {
      if (config->test_tap_index) {
        printf("ok %zu - %s # TODO expected failure\n", config->test_tap_index, step->name);
      }
    } else {
      ttest_indent__(stdout, step->level);
      printf("%s[ XFAIL ]%s", config->use_color ? TTEST_COLOR_GREEN__ : "",
             config->use_color ? TTEST_COLOR_RESET__ : "");
      if (step->execution_time_ms > 0.0) {
        printf(" (%.2fms)", step->execution_time_ms);
      }
      printf("\n");
    }
  }
}

static void ttest_report_fail__(ttest_config_type__ *config, ttest_test_step__ *step) {
  ++config->failed_test_count;
  if (config->use_tap) {
    if (config->test_tap_index) {
      printf("not ok %zu - %s", config->test_tap_index, step->name);
      ttest_tap_failure_diagnostic__(config->error);
      printf("\n");
    }
  } else {
    ttest_indent__(stdout, step->level);
    printf("%s[ FAIL  ]%s", config->use_color ? TTEST_COLOR_RED__ : "",
           config->use_color ? TTEST_COLOR_RESET__ : "");
    if (step->execution_time_ms > 0.0) {
      printf(" (%.2fms)", step->execution_time_ms);
    }
    printf("\n");
    if (config->info_len > 0) {
      ttest_indent__(stdout, step->level + 1);
      printf("with info: %s\n", config->info_buffer);
    }
    ttest_indent__(stdout, step->level + 1);
    printf("%s\n", config->error);
    ttest_indent__(stdout, step->level + 2);
    printf("%s\n", config->location);
  }
}

#ifdef __cplusplus
static void ttest_record_unhandled_exception__(ttest_config_type__ *config, const char *message) {
  if (!config || config->error) return;
  ++config->assertion_count;
  if (!config->current_test ||
      !(config->current_test->flags & ttest_node_flags_expected_fail__)) {
    ++config->assertion_failed_count;
  }
  snprintf(config->location_buf, sizeof(config->location_buf), "at unhandled C++ exception");
  config->location = config->location_buf;
  config->error = ttest_format__("Unhandled C++ exception: %s", message ? message : "unknown");
}
#endif

static void ttest_execute_target__(ttest_config_type__ *config, int target_node_id) {
  config->node_stack->size = 1;
  config->id = 0;
  config->target_node_id = target_node_id;
#ifdef __cplusplus
  try {
    ttest_test_main__(config);
  } catch (const ttest_fail_exception__ &) {
    /* Assertion failure already recorded in config->error. */
  } catch (const std::exception &e) {
    ttest_record_unhandled_exception__(config, e.what());
  } catch (...) {
    ttest_record_unhandled_exception__(config, "non-standard exception");
  }
#else
  if (setjmp(config->jump_buffer) != 0) return;
  ttest_test_main__(config);
#endif
}

static void ttest_execute_cleanup_target__(ttest_config_type__ *config, int target_node_id) {
  char *primary_error = config->error;
  char primary_location[sizeof(config->location_buf)];

  primary_location[0] = '\0';
  if (primary_error && config->location) {
    snprintf(primary_location, sizeof(primary_location), "%s", config->location);
  }
  config->error = NULL;
  config->location = NULL;
  ttest_execute_target__(config, target_node_id);
  if (primary_error) {
    free(config->error);
    config->error = primary_error;
    snprintf(config->location_buf, sizeof(config->location_buf), "%s", primary_location);
    config->location = config->location_buf;
  }
}

static void ttest_run__(ttest_config_type__ *config) {
  ttest_test_step__ *step = config->current_test;

  if (step->type == TTEST_NODE_GROUP__ && !config->use_tap) {
    if (config->has_focus_nodes && !(step->flags & ttest_node_flags_focus__)) {
      return;
    }
    ttest_indent__(stdout, step->level);
    printf("%s%s%s\n", config->use_color ? TTEST_COLOR_BOLD__ : "", step->name,
           config->use_color ? TTEST_COLOR_RESET__ : "");
    return;
  }

  bool skipped = false;
  bool filtered = false;
  const char *skip_reason = NULL;
  if (step->type == TTEST_NODE_TEST__) {
    step->result = TTEST_RESULT_PENDING__;
    step->skip_reason = NULL;
    if (step->flags & ttest_node_flags_skip__) {
      skipped = true;
      skip_reason = "Test was skipped";
    } else if (config->has_focus_nodes && !(step->flags & ttest_node_flags_focus__)) {
      filtered = true;
      skip_reason = "filtered by focus";
    } else if (config->filter && !strstr(step->name, config->filter) &&
               !(step->full_path && strstr(step->full_path, config->filter))) {
      filtered = true;
      skip_reason = "filtered out";
    }
    ++config->test_tap_index;

    /* Print selected test names before execution so a crashing case remains identifiable. */
    if (config->run == TTEST_TEST_RUN__ && !config->use_tap && !skipped && !filtered) {
      ttest_indent__(stdout, step->level);
      printf("%s\n", step->name);
      fflush(stdout);
    }

    if (!skipped && !filtered) {
      if (config->error) {
        free(config->error);
        config->error = NULL;
      }
      config->location = NULL;
      config->info_buffer[0] = '\0';
      config->info_len = 0;
      config->skip_subsequent = false;

      if (step->flags & ttest_node_flags_benchmark__) {
        ttest_bench_reset__(config);
      }

      for (size_t i = 0; i < ttest_array_size__(step->before_each_nodes); ++i) {
        ttest_node__ *hook =
            TTEST_CAST(ttest_node__ *, step->before_each_nodes->values[i]);
        ttest_execute_target__(config, hook->id);
        if (config->error) break;
      }

      if (!config->error) {
        double start_time = ttest_get_time_ms__();
        ttest_execute_target__(config, step->id);
        double end_time = ttest_get_time_ms__();
        step->execution_time_ms = end_time - start_time;
      }

      for (size_t i = 0; i < ttest_array_size__(step->after_each_nodes); ++i) {
        ttest_node__ *hook = TTEST_CAST(ttest_node__ *, step->after_each_nodes->values[i]);
        ttest_execute_cleanup_target__(config, hook->id);
      }
    }

    if (skipped || filtered) {
      step->executed = false;
      step->passed = false;
      step->result = filtered ? TTEST_RESULT_FILTERED__ : TTEST_RESULT_SKIPPED__;
      step->skip_reason = skip_reason;
      ttest_report_skip__(config, step);
    } else if (config->error == NULL) {
      /* Test passed */
      step->executed = true;
      bool is_expected_fail = (step->flags & ttest_node_flags_expected_fail__);
      if (is_expected_fail) {
        step->passed = false;
        step->result = TTEST_RESULT_UNEXPECTED_PASS__;
        step->failure_message = strdup("Expected to fail but passed");
        ttest_report_unexpected_pass__(config, step);
      } else {
        step->passed = true;
        step->result = TTEST_RESULT_PASSED__;
        ttest_report_pass__(config, step);
      }
    } else {
      /* Test failed */
      step->executed = true;
      bool is_expected_fail = (step->flags & ttest_node_flags_expected_fail__);
      if (is_expected_fail) {
        step->passed = true; /* Expected failure counts as pass */
        step->result = TTEST_RESULT_EXPECTED_FAIL__;
        ttest_report_expected_fail__(config, step);
      } else {
        step->passed = false;
        step->result = TTEST_RESULT_FAILED__;
        step->failure_message = strdup(config->error);
        step->failure_location =
            strdup(config->location ? config->location : "at unknown location");
        ttest_report_fail__(config, step);
      }
      free(config->error);
      config->error = NULL;
    }
  } else if (!skipped) {
    if (config->error) {
      free(config->error);
      config->error = NULL;
    }
    config->location = NULL;
    config->info_buffer[0] = '\0';
    config->info_len = 0;
    ttest_execute_target__(config, step->id);
    if (config->error) {
      step->executed = true;
      step->passed = false;
      step->result = TTEST_RESULT_FAILED__;
      step->failure_message = strdup(config->error);
      step->failure_location =
          strdup(config->location ? config->location : "at unknown location");
      if (config->use_tap) {
        ++config->failed_test_count;
        printf("Bail out! fixture %s failed: %s\n", step->name, config->error);
      } else {
        ttest_report_fail__(config, step);
      }
      free(config->error);
      config->error = NULL;
    } else {
      step->executed = true;
      step->passed = true;
      step->result = TTEST_RESULT_PASSED__;
    }
  }
}

static char *ttest_vformat__(const char *format, va_list va) {
  va_list va2;
  va_copy(va2, va);
  int len = vsnprintf(NULL, 0, format, va2);
  va_end(va2);
  if (len < 0) {
    fprintf(stderr, "tinytest: format error while building message\n");
    abort();
  }

  char *result = TTEST_CAST(char *, malloc((size_t)len + 1));
  if (!result) {
    perror("malloc(result)");
    abort();
  }
  int written = vsnprintf(result, (size_t)len + 1, format, va);
  if (written < 0 || written > len) {
    free(result);
    fprintf(stderr, "tinytest: format error while writing message\n");
    abort();
  }
  return result;
}

static char *ttest_format__(const char *format, ...) {
  va_list va;
  va_start(va, format);
  char *buf = ttest_vformat__(format, va);
  va_end(va);
  return buf;
}

static bool ttest_is_supported_term__(void) {
  bool result;
  const char *term = getenv("TERM");
  result = term && strcmp(term, "") != 0;
#ifndef _WIN32
  return result;
#else
  if (result) {
    return 1;
  }

  /* Attempt to enable virtual terminal processing on Windows.
   * See: https://msdn.microsoft.com/en-us/library/windows/desktop/mt638032(v=vs.85).aspx */
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut == INVALID_HANDLE_VALUE) {
    return 0;
  }

  DWORD dwMode = 0;
  if (!GetConsoleMode(hOut, &dwMode)) {
    return 0;
  }

  dwMode |= 0x4; /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */
  if (!SetConsoleMode(hOut, dwMode)) {
    return 0;
  }

  return 1;
#endif
}

static inline int ttest_is_xml_char__(unsigned char c) {
  return c == '\t' || c == '\n' || c == '\r' || c >= 0x20;
}

static const char *ttest_skip_ansi_escape__(const char *p) {
  if (!p || p[0] != '\x1B') return p;
  ++p;
  if (*p == '[') {
    ++p;
    while (*p && (((unsigned char)*p >= 0x30 && (unsigned char)*p <= 0x3F) ||
                  ((unsigned char)*p >= 0x20 && (unsigned char)*p <= 0x2F))) {
      ++p;
    }
    if (*p && (unsigned char)*p >= 0x40 && (unsigned char)*p <= 0x7E) {
      ++p;
    }
    return p;
  }
  if (*p == ']') {
    ++p;
    while (*p && *p != '\a') {
      if (p[0] == '\x1B' && p[1] == '\\') {
        p += 2;
        return p;
      }
      ++p;
    }
    if (*p == '\a') ++p;
    return p;
  }
  if (*p) ++p;
  return p;
}

static void ttest_tap_failure_diagnostic__(const char *message) {
  /* Append a single-line, ANSI-free diagnostic to a TAP failure line. */
  if (!message || !message[0]) return;
  printf(" # ");
  for (const char *p = message; *p;) {
    if (*p == '\n' || *p == '\r') {
      fputc(' ', stdout);
      ++p;
      continue;
    }
    if (*p == '\x1B') {
      p = ttest_skip_ansi_escape__(p);
      continue;
    }
    fputc(*p, stdout);
    ++p;
  }
}

static void ttest_xml_escape__(FILE *f, const char *str) {
  if (!str) return;
  for (const char *p = str; *p;) {
    unsigned char c = (unsigned char)*p;
    if (c == 0x1B) {
      p = ttest_skip_ansi_escape__(p);
      continue;
    }
    if (!ttest_is_xml_char__(c)) {
      ++p;
      continue;
    }
    switch (*p) {
    case '&':
      fprintf(f, "&amp;");
      break;
    case '<':
      fprintf(f, "&lt;");
      break;
    case '>':
      fprintf(f, "&gt;");
      break;
    case '"':
      fprintf(f, "&quot;");
      break;
    case '\'':
      fprintf(f, "&apos;");
      break;
    default:
      fputc(*p, f);
      break;
    }
    ++p;
  }
}

static ttest_result__ ttest_generate_junit__(ttest_config_type__ *config, ttest_array__ *steps,
                                             size_t test_count) {
  FILE *f = fopen(config->junit_file, "w");
  if (!f) {
    return ttest_result_error__(TTEST_ERR_IO__, "could not open JUnit output file");
  }

  /* Get current timestamp in ISO 8601 format */
  time_t now = time(NULL);
  struct tm *tm_info = gmtime(&now);
  if (!tm_info) {
    fclose(f);
    return ttest_result_error__(TTEST_ERR_TIME__, "could not build JUnit timestamp");
  }
  char timestamp[32];
  if (strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", tm_info) == 0) {
    fclose(f);
    return ttest_result_error__(TTEST_ERR_TIME__, "could not format JUnit timestamp");
  }

  /* Count skipped tests */
  size_t skipped_count = 0;
  for (size_t i = 0; i < steps->size; ++i) {
    ttest_test_step__ *step = TTEST_CAST(ttest_test_step__ *, steps->values[i]);
    if (step->type == TTEST_NODE_TEST__ && ttest_test_result_is_skip__(step->result)) {
      ++skipped_count;
    }
  }

  fprintf(f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
  fprintf(f,
          "<testsuites name=\"BDD Test Run\" tests=\"%zu\" failures=\"%zu\" errors=\"0\" "
          "skipped=\"%zu\" time=\"0\" timestamp=\"%s\">\n",
          test_count, config->failed_test_count, skipped_count, timestamp);

  fprintf(f,
          "  <testsuite name=\"tinytest\" tests=\"%zu\" failures=\"%zu\" errors=\"0\" "
          "skipped=\"%zu\" time=\"0\" timestamp=\"%s\">\n",
          test_count, config->failed_test_count, skipped_count, timestamp);

  for (size_t i = 0; i < steps->size; ++i) {
    ttest_test_step__ *step = TTEST_CAST(ttest_test_step__ *, steps->values[i]);
    if (step->type == TTEST_NODE_TEST__) {
      fprintf(f, "    <testcase name=\"");
      ttest_xml_escape__(f, step->name);
      fprintf(f, "\" classname=\"");
      /* Use full hierarchical path if available, otherwise use a generic name */
      if (step->full_path) {
        ttest_xml_escape__(f, step->full_path);
      } else {
        ttest_xml_escape__(f, "tinytest");
      }
      fprintf(f, "\" time=\"%.6f\"", step->execution_time_ms / 1000.0); /* Convert ms to seconds */

      bool is_skip = ttest_test_result_is_skip__(step->result);
      bool is_fail = ttest_test_result_is_fail__(step->result);

      if (is_skip) {
        fprintf(f, ">\n");
        fprintf(f, "      <skipped message=\"");
        ttest_xml_escape__(f, ttest_skip_message__(step));
        fprintf(f, "\" />\n");
        fprintf(f, "    </testcase>\n");
      } else if (is_fail) {
        /* Test failed */
        fprintf(f, ">\n");
        fprintf(f, "      <failure message=\"");
        if (step->failure_message) {
          ttest_xml_escape__(f, step->failure_message);
        } else {
          fprintf(f, "Test failed");
        }
        fprintf(f, "\" type=\"AssertionError\">");
        if (step->failure_location) {
          fprintf(f, "\n");
          ttest_xml_escape__(f, step->failure_location);
        }
        if (step->failure_message) {
          fprintf(f, "\n");
          ttest_xml_escape__(f, step->failure_message);
        }
        fprintf(f, "\n      </failure>\n");
        fprintf(f, "    </testcase>\n");
      } else {
        /* Test passed */
        fprintf(f, " />\n");
      }
    }
  }

  fprintf(f, "  </testsuite>\n");
  fprintf(f, "</testsuites>\n");
  if (ferror(f)) {
    fclose(f);
    return ttest_result_error__(TTEST_ERR_IO__, "could not write complete JUnit output");
  }
  if (fclose(f) != 0) {
    return ttest_result_error__(TTEST_ERR_IO__, "could not close JUnit output file");
  }
  return ttest_result_ok__();
}

#ifdef __cplusplus
}
#endif

/* main() must not be in extern "C" block */
#ifndef TINYTEST_NO_MAIN
int main(int argc, char **argv) {
  #ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  #endif

  double ttest_start_time__ = ttest_get_time_ms__();
  struct ttest_config_type__ config;
  memset(&config, 0, sizeof(config));
  config.run = TTEST_INIT_RUN__;

  /* Parse command-line arguments */
  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "--tap") == 0) {
      config.use_tap = 1;
    } else if (strcmp(argv[i], "--color") == 0) {
      config.use_color = 1;
    } else if (strcmp(argv[i], "--no-color") == 0) {
      config.use_color = 0;
    } else if (strcmp(argv[i], "--junit") == 0) {
      if (i + 1 < argc) {
        config.junit_file = argv[++i];
      } else {
        fprintf(stderr, "Error: --junit requires a filename argument\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--list") == 0 || strcmp(argv[i], "-l") == 0) {
      config.list_only = 1;
    } else if (strcmp(argv[i], "--filter") == 0 || strcmp(argv[i], "-f") == 0) {
      if (i + 1 < argc) {
        config.filter = argv[++i];
      } else {
        fprintf(stderr, "Error: --filter requires a pattern argument\n");
        return 1;
      }
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      printf("Usage: %s [options]\n", argv[0]);
      printf("Options:\n");
      printf("  --tap              Output in TAP (Test Anything Protocol) format\n");
      printf("  --color            Force colored output\n");
      printf("  --no-color         Disable colored output\n");
      printf("  --junit <file>     Generate JUnit XML report to specified file\n");
      printf("  --list, -l         List all test names without running them\n");
      printf("  --filter, -f <pat> Run only tests whose name contains <pat>\n");
      printf("  --help, -h         Show this help message\n");
      return 0;
    } else {
      fprintf(stderr, "Error: unknown option '%s'\n", argv[i]);
      fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
      return 2;
    }
  }

  const char *tap_env = getenv("TT_USE_TAP");
  if (!config.use_tap &&
      (TT_USE_TAP || (tap_env && strcmp(tap_env, "") != 0 && strcmp(tap_env, "0") != 0))) {
    config.use_tap = 1;
  }

  if (!config.use_tap && !config.use_color && TT_USE_COLOR && TTEST_IS_ATTY__() &&
      ttest_is_supported_term__()) {
    config.use_color = 1;
  }

  if (ttest_spec_count__ == 0) {
    fprintf(stderr, "tinytest: no specs registered\n");
    return 1;
  }

  ttest_array__ *all_steps = ttest_array_create__();
  ttest_array__ *all_step_arrays = ttest_array_create__();
  ttest_array__ *all_roots = ttest_array_create__();
  ttest_array__ *all_nodes = ttest_array_create__();
  ttest_array__ *all_stacks = ttest_array_create__();
  ttest_array__ *all_specs = ttest_array_create__();

  bool has_focus_any = false;
  size_t total_test_count = 0;

  for (size_t s = 0; s < ttest_spec_count__; ++s) {
    ttest_spec_entry__ *spec = ttest_get_spec_entry__(s);
    if (!spec) continue;

    config.run = TTEST_INIT_RUN__;
    config.id = 0;
    config.has_focus_nodes = false;
    config.nodes = ttest_array_create__();
    config.node_stack = ttest_array_create__();

    ttest_node__ *root =
        ttest_node_create__(-1, spec->name, TTEST_NODE_GROUP__, ttest_node_flags_none__);
    ttest_array_push__(config.node_stack, root);

    ttest_current_spec_fn__ = spec->fn;
    ttest_test_main__(&config);

    if (config.has_focus_nodes) {
      has_focus_any = true;
    }

    ttest_array__ *steps = ttest_array_create__();
    ttest_node_flatten__(&config, root, steps);

    enum { TTEST_MAX_GROUP_DEPTH__ = 128 };
    ttest_test_step__ *group_stack[TTEST_MAX_GROUP_DEPTH__];
    int stack_depth = 0;

    for (size_t i = 0; i < steps->size; ++i) {
      ttest_test_step__ *step = TTEST_CAST(ttest_test_step__ *, steps->values[i]);

      while (stack_depth > 0 && group_stack[stack_depth - 1]->level >= step->level) {
        stack_depth--;
      }

      if (step->type == TTEST_NODE_GROUP__) {
        if (stack_depth < TTEST_MAX_GROUP_DEPTH__) {
          group_stack[stack_depth++] = step;
        }
      } else if (step->type == TTEST_NODE_TEST__) {
        step->full_path = ttest_build_full_path__(group_stack, stack_depth, step->name);
        ++total_test_count;
      }
    }

    ttest_array_push__(all_roots, root);
    ttest_array_push__(all_nodes, config.nodes);
    ttest_array_push__(all_stacks, config.node_stack);
    ttest_array_push__(all_step_arrays, steps);
    ttest_array_push__(all_specs, spec);

    for (size_t i = 0; i < steps->size; ++i) {
      ttest_array_push__(all_steps, steps->values[i]);
    }
  }

  if (config.list_only) {
    for (size_t i = 0; i < all_steps->size; ++i) {
      ttest_test_step__ *step = TTEST_CAST(ttest_test_step__ *, all_steps->values[i]);
      if (step->type == TTEST_NODE_GROUP__) {
        ttest_indent__(stdout, step->level);
        printf("%s\n", step->name);
      } else if (step->type == TTEST_NODE_TEST__) {
        ttest_indent__(stdout, step->level);
        const char *tag = "";
        if (step->flags & ttest_node_flags_skip__) tag = " [skip]";
        else if (step->flags & ttest_node_flags_focus__) tag = " [focus]";
        else if (step->flags & ttest_node_flags_expected_fail__) tag = " [should_fail]";
        printf("%s%s\n", step->name, tag);
      }
    }

    for (size_t i = 0; i < all_nodes->size; ++i) {
      ttest_array__ *nodes = TTEST_CAST(ttest_array__ *, all_nodes->values[i]);
      for (size_t j = 0; j < nodes->size; ++j) {
        ttest_node_free__(TTEST_CAST(ttest_node__ *, nodes->values[j]));
      }
      ttest_array_free__(nodes);
      ttest_array_free__(TTEST_CAST(ttest_array__ *, all_stacks->values[i]));
      ttest_node__ *root = TTEST_CAST(ttest_node__ *, all_roots->values[i]);
      root->name = NULL;
      ttest_node_free__(root);
    }
    for (size_t i = 0; i < all_steps->size; ++i) {
      ttest_test_step_free__(TTEST_CAST(ttest_test_step__ *, all_steps->values[i]));
    }
    for (size_t i = 0; i < all_step_arrays->size; ++i) {
      ttest_array_free__(TTEST_CAST(ttest_array__ *, all_step_arrays->values[i]));
    }
    ttest_array_free__(all_roots);
    ttest_array_free__(all_nodes);
    ttest_array_free__(all_stacks);
    ttest_array_free__(all_step_arrays);
    ttest_array_free__(all_specs);
    ttest_array_free__(all_steps);
    ttest_cleanup_specs__();
    return 0;
  }

  if (config.use_tap) {
    printf("TAP version 13\n1..%zu\n", total_test_count);
  }

  config.run = TTEST_TEST_RUN__;
  config.has_focus_nodes = has_focus_any;

  for (size_t i = 0; i < all_step_arrays->size; ++i) {
    ttest_array__ *steps = TTEST_CAST(ttest_array__ *, all_step_arrays->values[i]);
    ttest_spec_entry__ *spec = TTEST_CAST(ttest_spec_entry__ *, all_specs->values[i]);
    ttest_current_spec_fn__ = spec ? spec->fn : NULL;
    config.node_stack = TTEST_CAST(ttest_array__ *, all_stacks->values[i]);
    config.nodes = TTEST_CAST(ttest_array__ *, all_nodes->values[i]);
    for (size_t j = 0; j < steps->size; ++j) {
      ttest_test_step__ *step = TTEST_CAST(ttest_test_step__ *, steps->values[j]);
      config.node_stack->size = 1;
      config.id = 0;
      if (config.error) {
        free(config.error);
        config.error = NULL;
      }
      config.location = NULL;
      config.current_test = step;
      ttest_run__(&config);
    }
  }

  double ttest_end_time__ = ttest_get_time_ms__();
  double ttest_duration__ = (ttest_end_time__ - ttest_start_time__) / 1000.0;

  size_t passed_count = 0;
  size_t skipped_count = 0;
  size_t filtered_count = 0;
  size_t todo_count = 0;
  for (size_t i = 0; i < all_steps->size; ++i) {
    ttest_test_step__ *step = TTEST_CAST(ttest_test_step__ *, all_steps->values[i]);
    if (step->type != TTEST_NODE_TEST__) continue;
    switch (ttest_test_result_category__(step->result)) {
      case TTEST_RESULT_CATEGORY_PASS__:
        passed_count++;
        break;
      case TTEST_RESULT_CATEGORY_SKIP__:
        skipped_count++;
        break;
      case TTEST_RESULT_CATEGORY_FILTER__:
        filtered_count++;
        break;
      case TTEST_RESULT_CATEGORY_TODO__:
        todo_count++;
        break;
      default:
        break;
    }
  }

  if (!config.use_tap) {
    const char *c_rst = config.use_color ? TTEST_COLOR_RESET__ : "";
    const char *c_red = config.use_color ? TTEST_COLOR_RED__ : "";
    const char *c_grn = config.use_color ? TTEST_COLOR_GREEN__ : "";
    const char *c_ylw = config.use_color ? TTEST_COLOR_YELLOW__ : "";
    const char *c_bld = config.use_color ? TTEST_COLOR_BOLD__ : "";

    printf("\n==All Tests Summary==\n");
    printf("Total tests %s[PASSED]:\t%zu%s\n", c_grn, passed_count, c_rst);
    printf("Total tests %s[FAILED]:\t%zu%s\n", c_red, config.failed_test_count, c_rst);
    if (config.warn_count > 0) {
      printf("Total tests %s[WARNED]:\t%zu%s\n", c_ylw, config.warn_count, c_rst);
    }
    printf("Total tests %s[SKIPPED]:\t%zu%s\n", c_ylw, skipped_count, c_rst);
    printf("Total tests %s[FILTERED]:\t%zu%s\n", c_ylw, filtered_count, c_rst);
    printf("Total tests %s[TODO]:   \t%zu%s\n", c_bld, todo_count, c_rst);
    printf("Assertions: %s%zu passed%s, %s%zu failed%s\n", c_grn,
           config.assertion_count - config.assertion_failed_count, c_rst,
           config.assertion_failed_count > 0 ? c_red : "", config.assertion_failed_count, c_rst);

    printf("%zu passed, %zu failed, %zu skipped, %zu filtered, %zu todo, %zu assertions. "
           "Finished in %f sec.\n",
           passed_count, config.failed_test_count, skipped_count, filtered_count, todo_count,
           config.assertion_count, ttest_duration__);

    if (config.failed_test_count == 0 && todo_count == 0) {
      printf("%sAll tests passed in %f sec.%s\n\n", c_grn, ttest_duration__, c_rst);
    } else {
      printf("\n");
    }
  }

  /* Generate JUnit XML report if requested - must be done before freeing steps */
  int exit_code = config.failed_test_count > 0 ? 1 : 0;
  if (config.junit_file) {
    ttest_result__ junit_result = ttest_generate_junit__(&config, all_steps, total_test_count);
    if (!junit_result.ok) {
      fprintf(stderr, "Error: %s: %s\n", junit_result.message, config.junit_file);
      exit_code = 1;
    }
  }

  for (size_t i = 0; i < all_nodes->size; ++i) {
    ttest_array__ *nodes = TTEST_CAST(ttest_array__ *, all_nodes->values[i]);
    for (size_t j = 0; j < nodes->size; ++j) {
      ttest_node_free__(TTEST_CAST(ttest_node__ *, nodes->values[j]));
    }
    ttest_array_free__(nodes);
    ttest_array_free__(TTEST_CAST(ttest_array__ *, all_stacks->values[i]));
    ttest_node__ *root = TTEST_CAST(ttest_node__ *, all_roots->values[i]);
    root->name = NULL;
    ttest_node_free__(root);
  }
  for (size_t i = 0; i < all_step_arrays->size; ++i) {
    ttest_array_free__(TTEST_CAST(ttest_array__ *, all_step_arrays->values[i]));
  }
  ttest_array_free__(all_roots);
  ttest_array_free__(all_nodes);
  ttest_array_free__(all_stacks);
  ttest_array_free__(all_step_arrays);
  ttest_array_free__(all_specs);
  for (size_t i = 0; i < all_steps->size; ++i) {
    ttest_test_step_free__(TTEST_CAST(ttest_test_step__ *, all_steps->values[i]));
  }
  ttest_array_free__(all_steps);
  ttest_cleanup_specs__();

  return exit_code;
}
#endif /* TINYTEST_NO_MAIN */

#if defined(__GNUC__) || defined(__clang__)
  #define TTEST_UNUSED_PARAM__ __attribute__((unused))
#else
  #define TTEST_UNUSED_PARAM__
#endif

#define spec(name)                                                                                 \
  static void TTEST_CAT2(ttest_spec_fn_,                                                           \
                         __LINE__)(TTEST_UNUSED_PARAM__ ttest_config_type__ * ttest_config__);     \
  TTEST_CONSTRUCTOR__(TTEST_CAT2(ttest_spec_reg_, __LINE__)) {                                     \
    ttest_register_spec__((name), TTEST_CAT2(ttest_spec_fn_, __LINE__));                           \
  }                                                                                                \
  static void TTEST_CAT2(ttest_spec_fn_,                                                           \
                         __LINE__)(TTEST_UNUSED_PARAM__ ttest_config_type__ * ttest_config__)

#define TTEST_CAT(a, b) a##b
#define TTEST_CAT2(a, b) TTEST_CAT(a, b)

#define TTEST_NODE_IMPL__(flags, node_list, type, format_name, ...)                                \
  for (bool TTEST_CAT2(ttest_has_run_, __LINE__) = 0;                                              \
       (!TTEST_CAT2(ttest_has_run_, __LINE__) &&                                                   \
        ttest_enter_node__(flags, ttest_active_config__, (type),                                   \
                           offsetof(struct ttest_node__, node_list), (format_name), __VA_ARGS__)); \
       ttest_exit_node__(ttest_active_config__), TTEST_CAT2(ttest_has_run_, __LINE__) = 1)

/* A single name argument is a literal string and is never interpreted as a
 * printf format, so names containing '%' are safe. Two or more arguments
 * select printf formatting (e.g. it("row %zu", i)). */
#define TTEST_NODE_DISPATCH_ONE__(flags, node_list, type, name)                                    \
  TTEST_NODE_IMPL__(flags, node_list, type, false, name)
#define TTEST_NODE_DISPATCH__(flags, node_list, type, ...)                                         \
  TTEST_NODE_IMPL__(flags, node_list, type, true, __VA_ARGS__)

#define TTEST_NODE__(flags, node_list, type, ...)                                                  \
  TTEST_OVERLOAD__(TTEST_NODE_DISPATCH_, TTEST_COUNT_ARGS__(__VA_ARGS__))(flags, node_list, type,   \
                                                                          __VA_ARGS__)

#define describe(...)                                                                              \
  TTEST_NODE__(ttest_node_flags_none__, list_children, TTEST_NODE_GROUP__, __VA_ARGS__)
#define group(...) describe(__VA_ARGS__)
#define suite(...) spec(__VA_ARGS__)
#define it(...) TTEST_NODE__(ttest_node_flags_none__, list_children, TTEST_NODE_TEST__, __VA_ARGS__)
#define bench(...)                                                                                 \
  TTEST_NODE__(ttest_node_flags_benchmark__, list_children, TTEST_NODE_TEST__, __VA_ARGS__)
#define it_only(...)                                                                               \
  TTEST_NODE__(ttest_node_flags_focus__, list_children, TTEST_NODE_TEST__, __VA_ARGS__)
#define fit(...) it_only(__VA_ARGS__)
#define it_skip(...)                                                                               \
  TTEST_NODE__(ttest_node_flags_skip__, list_children, TTEST_NODE_TEST__, __VA_ARGS__)
#define xit(...) it_skip(__VA_ARGS__)
#define it_should_fail(...)                                                                        \
  TTEST_NODE__(ttest_node_flags_expected_fail__, list_children, TTEST_NODE_TEST__, __VA_ARGS__)
#define before_each()                                                                              \
  TTEST_NODE__(ttest_node_flags_none__, list_before_each, TTEST_NODE_INTERIM__, "before_each")
#define after_each()                                                                               \
  TTEST_NODE__(ttest_node_flags_none__, list_after_each, TTEST_NODE_INTERIM__, "after_each")

#ifndef TT_NO_CONTEXT_KEYWORD
  #define context(name) describe(name)
#endif

/* TT_invoke: ##__VA_ARGS__ elides the comma when ... is empty.
 * This is a GCC/Clang extension, standardised as __VA_OPT__(,) in C23 and C++20.
 * MSVC traditional preprocessor implements the same comma-elision extension
 * natively; /Zc:preprocessor mode requires C23/C++20 standard __VA_OPT__. */
#if (defined(__cplusplus) && __cplusplus >= 202002L) || \
    (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
  #define TT_invoke(func, ...) func(ttest_active_config__ __VA_OPT__(,) __VA_ARGS__)
#else
  #define TT_invoke(func, ...) func(ttest_active_config__, ##__VA_ARGS__)
#endif

#define TTEST_MACRO__(M, ...) TTEST_OVERLOAD__(M, TTEST_COUNT_ARGS__(__VA_ARGS__))(__VA_ARGS__)
#define TTEST_OVERLOAD__(macro_name, suffix) TTEST_EXPAND_OVERLOAD__(macro_name, suffix)
#define TTEST_EXPAND_OVERLOAD__(macro_name, suffix) macro_name##suffix

#define TTEST_COUNT_ARGS__(...) TTEST_PATTERN_MATCH__(__VA_ARGS__, _, _, _, _, _, _, _, _, _, ONE__)
#define TTEST_PATTERN_MATCH__(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, N, ...) N

#define TTEST_STRING_HELPER__(x) #x
#define TTEST_STRING__(x) TTEST_STRING_HELPER__(x)
/* Two-level stringify: outer macro forces argument expansion before # operator. */
#define TTEST_STRINGIZE_LINE__ TTEST_STRING__(__LINE__)
#define __STRING__LINE__ TTEST_STRINGIZE_LINE__ /* backward-compatible alias */

#define TTEST_FMT_COLOR__ TTEST_COLOR_RED__ "Check failed:" TTEST_COLOR_RESET__ " %s"
#define TTEST_FMT_PLAIN__ "Check failed: %s"

static inline int ttest_eval_bool__(int v) { return v; }

/* Internal implementation that takes file and line.
 * Diagnostic guards suppress -Wshadow (internal ttest_xxx__ locals) and
 * -Wunused-value (!!(condition) double-negation pattern) at expansion sites. */
#define TTEST_CHECK_IMPL__(condition, file, line, ...)                                              \
  TTEST_DIAG_PUSH__                                                                                \
  TTEST_DIAG_IGNORE_SHADOW__                                                                       \
  TTEST_DIAG_IGNORE_UNUSED_VALUE__                                                                 \
  do {                                                                                             \
    if (!ttest_eval_bool__(!!(condition))) {                                                       \
      if (ttest_active_config__) {                                                                 \
        bool ttest_expected_fail__ = (ttest_active_config__->current_test &&                               \
                                     (ttest_active_config__->current_test->flags &                         \
                                      ttest_node_flags_expected_fail__));                              \
        ++ttest_active_config__->assertion_count;                                                  \
        if (!ttest_expected_fail__) {                                                              \
          ++ttest_active_config__->assertion_failed_count;                                         \
        }                                                                                          \
        if (ttest_active_config__->run == TTEST_TEST_RUN__ && !ttest_active_config__->error) {        \
          char *ttest_message__ = ttest_format__(__VA_ARGS__);                                     \
          const char *fmt =                                                                        \
              ttest_active_config__->use_color ? TTEST_FMT_COLOR__ : TTEST_FMT_PLAIN__;            \
          snprintf(ttest_active_config__->location_buf,                                            \
                   sizeof(ttest_active_config__->location_buf), "at %s:%s", file, line);           \
          ttest_active_config__->location = ttest_active_config__->location_buf;                   \
          size_t bufflen = strlen(fmt) + strlen(ttest_message__) + 1;                              \
          ttest_active_config__->error = TTEST_CAST(char *, calloc(bufflen, sizeof(char)));        \
          if (ttest_active_config__->use_color) {                                                  \
            snprintf(ttest_active_config__->error, bufflen, TTEST_FMT_COLOR__, ttest_message__);   \
          } else {                                                                                 \
            snprintf(ttest_active_config__->error, bufflen, TTEST_FMT_PLAIN__, ttest_message__);   \
          }                                                                                        \
          free(ttest_message__);                                                                   \
          ttest_longjmp_fail__(ttest_active_config__);                                             \
        }                                                                                          \
      }                                                                                            \
    } else {                                                                                       \
      if (ttest_active_config__) ++ttest_active_config__->assertion_count;                         \
    }                                                                                              \
  } while (0)                                                                                      \
  TTEST_DIAG_POP__

/* Wrapper that captures __FILE__ and __LINE__ */
#define TTEST_CHECK__(condition, ...)                                                              \
  TTEST_CHECK_IMPL__(condition, __FILE__, __STRING__LINE__, __VA_ARGS__)

#define TTEST_CHECK_ONE__(condition)                                                               \
  TTEST_CHECK_IMPL__(condition, __FILE__, __STRING__LINE__, #condition)

#define check(...) TTEST_MACRO__(TTEST_CHECK_, __VA_ARGS__)

#define TTEST_INT_COMPARE__(emitter, actual, expected, op, fmt)                                    \
  do {                                                                                             \
    int ttest_a__ = TTEST_CAST(int, (actual));                                                     \
    int ttest_e__ = TTEST_CAST(int, (expected));                                                   \
    emitter(ttest_a__ op ttest_e__, fmt, ttest_e__, ttest_a__);                                    \
  } while (0)

#define TTEST_UINT_COMPARE__(emitter, actual, expected, op, fmt)                                   \
  do {                                                                                             \
    unsigned ttest_a__ = TTEST_CAST(unsigned, (actual));                                           \
    unsigned ttest_e__ = TTEST_CAST(unsigned, (expected));                                         \
    emitter(ttest_a__ op ttest_e__, fmt, ttest_e__, ttest_a__);                                    \
  } while (0)

#define TTEST_SIZE_COMPARE__(emitter, actual, expected, op, fmt)                                   \
  do {                                                                                             \
    size_t ttest_a__ = TTEST_CAST(size_t, (actual));                                               \
    size_t ttest_e__ = TTEST_CAST(size_t, (expected));                                             \
    emitter(ttest_a__ op ttest_e__, fmt, ttest_e__, ttest_a__);                                    \
  } while (0)

#define TTEST_LLONG_COMPARE__(emitter, actual, expected, op, fmt)                                  \
  do {                                                                                             \
    long long ttest_a__ = TTEST_CAST(long long, (actual));                                         \
    long long ttest_e__ = TTEST_CAST(long long, (expected));                                       \
    emitter(ttest_a__ op ttest_e__, fmt, ttest_e__, ttest_a__);                                    \
  } while (0)

#define TTEST_ULLONG_COMPARE__(emitter, actual, expected, op, fmt)                                 \
  do {                                                                                             \
    unsigned long long ttest_a__ = TTEST_CAST(unsigned long long, (actual));                       \
    unsigned long long ttest_e__ = TTEST_CAST(unsigned long long, (expected));                     \
    emitter(ttest_a__ op ttest_e__, fmt, ttest_e__, ttest_a__);                                    \
  } while (0)

#define TTEST_DOUBLE_COMPARE__(emitter, actual, expected, op, fmt)                                 \
  do {                                                                                             \
    double ttest_a__ = TTEST_CAST(double, (actual));                                               \
    double ttest_e__ = TTEST_CAST(double, (expected));                                             \
    emitter(ttest_a__ op ttest_e__, fmt, ttest_e__, ttest_a__);                                    \
  } while (0)

/* --- Typed assertion macros --- */

/* Integer comparisons */
#define check_int_eq(actual, expected)                                                             \
  TTEST_INT_COMPARE__(TTEST_CHECK__, actual, expected, ==, "expected %d but got %d")
#define check_int_eq_warn(actual, expected)                                                        \
  TTEST_INT_COMPARE__(TTEST_WARN__, actual, expected, ==, "expected %d but got %d")

#define check_int_ne(actual, expected)                                                             \
  TTEST_INT_COMPARE__(TTEST_CHECK__, actual, expected, !=, "expected != %d but got %d")
#define check_int_ne_warn(actual, expected)                                                        \
  TTEST_INT_COMPARE__(TTEST_WARN__, actual, expected, !=, "expected != %d but got %d")

#define check_int_gt(actual, expected)                                                             \
  TTEST_INT_COMPARE__(TTEST_CHECK__, actual, expected, >, "expected > %d but got %d")
#define check_int_gt_warn(actual, expected)                                                        \
  TTEST_INT_COMPARE__(TTEST_WARN__, actual, expected, >, "expected > %d but got %d")

#define check_int_ge(actual, expected)                                                             \
  TTEST_INT_COMPARE__(TTEST_CHECK__, actual, expected, >=, "expected >= %d but got %d")
#define check_int_ge_warn(actual, expected)                                                        \
  TTEST_INT_COMPARE__(TTEST_WARN__, actual, expected, >=, "expected >= %d but got %d")

#define check_int_lt(actual, expected)                                                             \
  TTEST_INT_COMPARE__(TTEST_CHECK__, actual, expected, <, "expected < %d but got %d")
#define check_int_lt_warn(actual, expected)                                                        \
  TTEST_INT_COMPARE__(TTEST_WARN__, actual, expected, <, "expected < %d but got %d")

#define check_int_le(actual, expected)                                                             \
  TTEST_INT_COMPARE__(TTEST_CHECK__, actual, expected, <=, "expected <= %d but got %d")
#define check_int_le_warn(actual, expected)                                                        \
  TTEST_INT_COMPARE__(TTEST_WARN__, actual, expected, <=, "expected <= %d but got %d")

/* Unsigned integer comparisons */
#define check_uint_eq(actual, expected)                                                            \
  TTEST_UINT_COMPARE__(TTEST_CHECK__, actual, expected, ==, "expected %u but got %u")
#define check_uint_eq_warn(actual, expected)                                                       \
  TTEST_UINT_COMPARE__(TTEST_WARN__, actual, expected, ==, "expected %u but got %u")

#define check_uint_ne(actual, expected)                                                            \
  TTEST_UINT_COMPARE__(TTEST_CHECK__, actual, expected, !=, "expected != %u but got %u")
#define check_uint_ne_warn(actual, expected)                                                       \
  TTEST_UINT_COMPARE__(TTEST_WARN__, actual, expected, !=, "expected != %u but got %u")

/* Size_t comparisons */
#define check_size_eq(actual, expected)                                                            \
  TTEST_SIZE_COMPARE__(TTEST_CHECK__, actual, expected, ==, "expected %zu but got %zu")
#define check_size_eq_warn(actual, expected)                                                       \
  TTEST_SIZE_COMPARE__(TTEST_WARN__, actual, expected, ==, "expected %zu but got %zu")

#define check_size_ne(actual, expected)                                                            \
  TTEST_SIZE_COMPARE__(TTEST_CHECK__, actual, expected, !=, "expected != %zu but got %zu")
#define check_size_ne_warn(actual, expected)                                                       \
  TTEST_SIZE_COMPARE__(TTEST_WARN__, actual, expected, !=, "expected != %zu but got %zu")

#define check_size_gt(actual, expected)                                                            \
  TTEST_SIZE_COMPARE__(TTEST_CHECK__, actual, expected, >, "expected > %zu but got %zu")
#define check_size_gt_warn(actual, expected)                                                       \
  TTEST_SIZE_COMPARE__(TTEST_WARN__, actual, expected, >, "expected > %zu but got %zu")

#define check_size_ge(actual, expected)                                                            \
  TTEST_SIZE_COMPARE__(TTEST_CHECK__, actual, expected, >=, "expected >= %zu but got %zu")
#define check_size_ge_warn(actual, expected)                                                       \
  TTEST_SIZE_COMPARE__(TTEST_WARN__, actual, expected, >=, "expected >= %zu but got %zu")

#define check_size_lt(actual, expected)                                                            \
  TTEST_SIZE_COMPARE__(TTEST_CHECK__, actual, expected, <, "expected < %zu but got %zu")
#define check_size_lt_warn(actual, expected)                                                       \
  TTEST_SIZE_COMPARE__(TTEST_WARN__, actual, expected, <, "expected < %zu but got %zu")

#define check_size_le(actual, expected)                                                            \
  TTEST_SIZE_COMPARE__(TTEST_CHECK__, actual, expected, <=, "expected <= %zu but got %zu")
#define check_size_le_warn(actual, expected)                                                       \
  TTEST_SIZE_COMPARE__(TTEST_WARN__, actual, expected, <=, "expected <= %zu but got %zu")

/* Long / 64-bit comparisons */
#define check_long_eq(actual, expected)                                                            \
  TTEST_LLONG_COMPARE__(TTEST_CHECK__, actual, expected, ==, "expected %lld but got %lld")

/* Explicit 64-bit comparisons. check_int_* and check_uint_* operate on
 * 32-bit values; passing int64_t/uint64_t there silently truncates the
 * upper bits. */
#define check_ll_eq(actual, expected)                                                              \
  TTEST_LLONG_COMPARE__(TTEST_CHECK__, actual, expected, ==, "expected %lld but got %lld")
#define check_ll_eq_warn(actual, expected)                                                         \
  TTEST_LLONG_COMPARE__(TTEST_WARN__, actual, expected, ==, "expected %lld but got %lld")
#define check_ll_ne(actual, expected)                                                              \
  TTEST_LLONG_COMPARE__(TTEST_CHECK__, actual, expected, !=, "expected != %lld but got %lld")
#define check_ll_ne_warn(actual, expected)                                                         \
  TTEST_LLONG_COMPARE__(TTEST_WARN__, actual, expected, !=, "expected != %lld but got %lld")
#define check_ll_gt(actual, expected)                                                              \
  TTEST_LLONG_COMPARE__(TTEST_CHECK__, actual, expected, >, "expected > %lld but got %lld")
#define check_ll_gt_warn(actual, expected)                                                         \
  TTEST_LLONG_COMPARE__(TTEST_WARN__, actual, expected, >, "expected > %lld but got %lld")
#define check_ll_ge(actual, expected)                                                              \
  TTEST_LLONG_COMPARE__(TTEST_CHECK__, actual, expected, >=, "expected >= %lld but got %lld")
#define check_ll_ge_warn(actual, expected)                                                         \
  TTEST_LLONG_COMPARE__(TTEST_WARN__, actual, expected, >=, "expected >= %lld but got %lld")
#define check_ll_lt(actual, expected)                                                              \
  TTEST_LLONG_COMPARE__(TTEST_CHECK__, actual, expected, <, "expected < %lld but got %lld")
#define check_ll_lt_warn(actual, expected)                                                         \
  TTEST_LLONG_COMPARE__(TTEST_WARN__, actual, expected, <, "expected < %lld but got %lld")
#define check_ll_le(actual, expected)                                                              \
  TTEST_LLONG_COMPARE__(TTEST_CHECK__, actual, expected, <=, "expected <= %lld but got %lld")
#define check_ll_le_warn(actual, expected)                                                         \
  TTEST_LLONG_COMPARE__(TTEST_WARN__, actual, expected, <=, "expected <= %lld but got %lld")
#define check_ull_eq(actual, expected)                                                             \
  TTEST_ULLONG_COMPARE__(TTEST_CHECK__, actual, expected, ==, "expected %llu but got %llu")
#define check_ull_eq_warn(actual, expected)                                                        \
  TTEST_ULLONG_COMPARE__(TTEST_WARN__, actual, expected, ==, "expected %llu but got %llu")
#define check_ull_ne(actual, expected)                                                             \
  TTEST_ULLONG_COMPARE__(TTEST_CHECK__, actual, expected, !=, "expected != %llu but got %llu")
#define check_ull_ne_warn(actual, expected)                                                        \
  TTEST_ULLONG_COMPARE__(TTEST_WARN__, actual, expected, !=, "expected != %llu but got %llu")

/* Float/double comparisons with epsilon */
#define check_float_eq(actual, expected, epsilon)                                                  \
  do {                                                                                             \
    double ttest_a__ = TTEST_CAST(double, (actual));                                               \
    double ttest_e__ = TTEST_CAST(double, (expected));                                             \
    double ttest_eps__ = TTEST_CAST(double, (epsilon));                                            \
    TTEST_CHECK__(fabs(ttest_a__ - ttest_e__) <= ttest_eps__, "expected %f (+/- %f) but got %f",   \
                  ttest_e__, ttest_eps__, ttest_a__);                                              \
  } while (0)
#define check_float_eq_warn(actual, expected, epsilon)                                             \
  do {                                                                                             \
    double ttest_a__ = TTEST_CAST(double, (actual));                                               \
    double ttest_e__ = TTEST_CAST(double, (expected));                                             \
    double ttest_eps__ = TTEST_CAST(double, (epsilon));                                            \
    TTEST_WARN__(fabs(ttest_a__ - ttest_e__) <= ttest_eps__, "expected %f (+/- %f) but got %f",    \
                 ttest_e__, ttest_eps__, ttest_a__);                                               \
  } while (0)

#define check_float_ne(actual, expected, epsilon)                                                  \
  do {                                                                                             \
    double ttest_a__ = TTEST_CAST(double, (actual));                                               \
    double ttest_e__ = TTEST_CAST(double, (expected));                                             \
    double ttest_eps__ = TTEST_CAST(double, (epsilon));                                            \
    TTEST_CHECK__(fabs(ttest_a__ - ttest_e__) > ttest_eps__, "expected != %f (+/- %f) but got %f", \
                  ttest_e__, ttest_eps__, ttest_a__);                                              \
  } while (0)
#define check_float_ne_warn(actual, expected, epsilon)                                             \
  do {                                                                                             \
    double ttest_a__ = TTEST_CAST(double, (actual));                                               \
    double ttest_e__ = TTEST_CAST(double, (expected));                                             \
    double ttest_eps__ = TTEST_CAST(double, (epsilon));                                            \
    TTEST_WARN__(fabs(ttest_a__ - ttest_e__) > ttest_eps__, "expected != %f (+/- %f) but got %f",  \
                 ttest_e__, ttest_eps__, ttest_a__);                                               \
  } while (0)

#define check_float_gt(actual, expected)                                                           \
  TTEST_DOUBLE_COMPARE__(TTEST_CHECK__, actual, expected, >, "expected > %f but got %f")
#define check_float_gt_warn(actual, expected)                                                      \
  TTEST_DOUBLE_COMPARE__(TTEST_WARN__, actual, expected, >, "expected > %f but got %f")

#define check_float_lt(actual, expected)                                                           \
  TTEST_DOUBLE_COMPARE__(TTEST_CHECK__, actual, expected, <, "expected < %f but got %f")
#define check_float_lt_warn(actual, expected)                                                      \
  TTEST_DOUBLE_COMPARE__(TTEST_WARN__, actual, expected, <, "expected < %f but got %f")

#define check_float_ge(actual, expected)                                                           \
  TTEST_DOUBLE_COMPARE__(TTEST_CHECK__, actual, expected, >=, "expected >= %f but got %f")
#define check_float_ge_warn(actual, expected)                                                      \
  TTEST_DOUBLE_COMPARE__(TTEST_WARN__, actual, expected, >=, "expected >= %f but got %f")

#define check_float_le(actual, expected)                                                           \
  TTEST_DOUBLE_COMPARE__(TTEST_CHECK__, actual, expected, <=, "expected <= %f but got %f")
#define check_float_le_warn(actual, expected)                                                      \
  TTEST_DOUBLE_COMPARE__(TTEST_WARN__, actual, expected, <=, "expected <= %f but got %f")

/* Relative tolerance: |actual - expected| <= rel * |expected| */
#define check_float_within_rel(actual, expected, rel)                                              \
  do {                                                                                             \
    double ttest_a__ = TTEST_CAST(double, (actual));                                               \
    double ttest_e__ = TTEST_CAST(double, (expected));                                             \
    double ttest_rel__ = TTEST_CAST(double, (rel));                                                \
    double ttest_delta__ = fabs(ttest_a__ - ttest_e__);                                            \
    TTEST_CHECK__(ttest_delta__ <= ttest_rel__ * fabs(ttest_e__),                                  \
                  "expected %f within %f%% of %f, delta was %f", ttest_a__, ttest_rel__ * 100.0,   \
                  ttest_e__, ttest_delta__);                                                       \
  } while (0)
#define check_float_within_rel_warn(actual, expected, rel)                                         \
  do {                                                                                             \
    double ttest_a__ = TTEST_CAST(double, (actual));                                               \
    double ttest_e__ = TTEST_CAST(double, (expected));                                             \
    double ttest_rel__ = TTEST_CAST(double, (rel));                                                \
    double ttest_delta__ = fabs(ttest_a__ - ttest_e__);                                            \
    TTEST_WARN__(ttest_delta__ <= ttest_rel__ * fabs(ttest_e__),                                   \
                 "expected %f within %f%% of %f, delta was %f", ttest_a__, ttest_rel__ * 100.0,    \
                 ttest_e__, ttest_delta__);                                                        \
  } while (0)

/* Absolute tolerance — same as check_float_eq but named for clarity
 */
#define check_float_within_abs(actual, expected, margin) check_float_eq(actual, expected, margin)
#define check_float_within_abs_warn(actual, expected, margin)                                      \
  check_float_eq_warn(actual, expected, margin)

/* Double precision comparisons (aliases to float/double checks with correct casting) */
#define check_double_eq(actual, expected, epsilon) check_float_eq(actual, expected, epsilon)
#define check_double_eq_warn(actual, expected, epsilon)                                            \
  check_float_eq_warn(actual, expected, epsilon)

#define check_double_ne(actual, expected, epsilon) check_float_ne(actual, expected, epsilon)
#define check_double_ne_warn(actual, expected, epsilon)                                            \
  check_float_ne_warn(actual, expected, epsilon)

#define check_double_gt(actual, expected) check_float_gt(actual, expected)
#define check_double_gt_warn(actual, expected) check_float_gt_warn(actual, expected)

#define check_double_lt(actual, expected) check_float_lt(actual, expected)
#define check_double_lt_warn(actual, expected) check_float_lt_warn(actual, expected)

#define check_double_ge(actual, expected) check_float_ge(actual, expected)
#define check_double_ge_warn(actual, expected) check_float_ge_warn(actual, expected)

#define check_double_le(actual, expected) check_float_le(actual, expected)
#define check_double_le_warn(actual, expected) check_float_le_warn(actual, expected)

#define check_double_within_rel(actual, expected, rel) check_float_within_rel(actual, expected, rel)
#define check_double_within_rel_warn(actual, expected, rel)                                        \
  check_float_within_rel_warn(actual, expected, rel)

#define check_double_within_abs(actual, expected, margin)                                          \
  check_float_within_abs(actual, expected, margin)
#define check_double_within_abs_warn(actual, expected, margin)                                     \
  check_float_within_abs_warn(actual, expected, margin)

/* gtest-style compatibility aliases */
#define check_close(actual, expected, epsilon)                                                      \
  check_float_eq((actual), (expected), (epsilon))
#define check_close_warn(actual, expected, epsilon)                                                \
  check_float_eq_warn((actual), (expected), (epsilon))
#define check_almost_equal(actual, expected, epsilon)                                              \
  check_float_eq((actual), (expected), (epsilon))
#define check_almost_equal_warn(actual, expected, epsilon)                                          \
  check_float_eq_warn((actual), (expected), (epsilon))

/* String comparisons */
static inline int ttest_str_eq__(const char *a, const char *b) {
  return a && b && strcmp(a, b) == 0;
}
static inline int ttest_str_ne__(const char *a, const char *b) {
  return !a || !b || strcmp(a, b) != 0;
}
static inline int ttest_str_contains__(const char *haystack, const char *needle) {
  return haystack && needle && strstr(haystack, needle) != NULL;
}
static inline int ttest_str_starts_with__(const char *s, const char *prefix) {
  return s && prefix && strncmp(s, prefix, strlen(prefix)) == 0;
}
static inline int ttest_str_ends_with__(const char *s, const char *suffix) {
  size_t slen;
  size_t suflen;
  if (!s || !suffix) return 0;
  slen = strlen(s);
  suflen = strlen(suffix);
  return slen >= suflen && strcmp(s + slen - suflen, suffix) == 0;
}

static inline const char *ttest_cstr_or_null__(const char *s) { return s ? s : "(null)"; }

#define check_str_eq(actual, expected)                                                             \
  do {                                                                                             \
    const char *ttest_a__ = (const char *)(actual);                                                \
    const char *ttest_e__ = (const char *)(expected);                                              \
    TTEST_CHECK__(ttest_str_eq__(ttest_a__, ttest_e__), "expected \"%s\" but got \"%s\"",          \
                  ttest_cstr_or_null__(ttest_e__), ttest_cstr_or_null__(ttest_a__));               \
  } while (0)
#define check_str_eq_warn(actual, expected)                                                        \
  do {                                                                                             \
    const char *ttest_a__ = (const char *)(actual);                                                \
    const char *ttest_e__ = (const char *)(expected);                                              \
    TTEST_WARN__(ttest_str_eq__(ttest_a__, ttest_e__), "expected \"%s\" but got \"%s\"",           \
                 ttest_cstr_or_null__(ttest_e__), ttest_cstr_or_null__(ttest_a__));                \
  } while (0)

#define check_str_ne(actual, expected)                                                             \
  do {                                                                                             \
    const char *ttest_a__ = (const char *)(actual);                                                \
    const char *ttest_e__ = (const char *)(expected);                                              \
    TTEST_CHECK__(ttest_str_ne__(ttest_a__, ttest_e__), "expected != \"%s\" but got \"%s\"",       \
                  ttest_cstr_or_null__(ttest_e__), ttest_cstr_or_null__(ttest_a__));               \
  } while (0)
#define check_str_ne_warn(actual, expected)                                                        \
  do {                                                                                             \
    const char *ttest_a__ = (const char *)(actual);                                                \
    const char *ttest_e__ = (const char *)(expected);                                              \
    TTEST_WARN__(ttest_str_ne__(ttest_a__, ttest_e__), "expected != \"%s\" but got \"%s\"",        \
                 ttest_cstr_or_null__(ttest_e__), ttest_cstr_or_null__(ttest_a__));                \
  } while (0)

#define check_str_contains(haystack, needle)                                                       \
  do {                                                                                             \
    const char *ttest_h__ = (const char *)(haystack);                                              \
    const char *ttest_n__ = (const char *)(needle);                                                \
    TTEST_CHECK__(ttest_str_contains__(ttest_h__, ttest_n__), "expected \"%s\" to contain \"%s\"", \
                  ttest_cstr_or_null__(ttest_h__), ttest_cstr_or_null__(ttest_n__));               \
  } while (0)
#define check_str_contains_warn(haystack, needle)                                                  \
  do {                                                                                             \
    const char *ttest_h__ = (const char *)(haystack);                                              \
    const char *ttest_n__ = (const char *)(needle);                                                \
    TTEST_WARN__(ttest_str_contains__(ttest_h__, ttest_n__), "expected \"%s\" to contain \"%s\"",  \
                 ttest_cstr_or_null__(ttest_h__), ttest_cstr_or_null__(ttest_n__));                \
  } while (0)

#if !defined(__cplusplus)
#define check_contains(haystack, needle) check_str_contains((haystack), (needle))
#define check_contains_warn(haystack, needle) check_str_contains_warn((haystack), (needle))
#endif

#define check_str_starts_with(str, prefix)                                                         \
  do {                                                                                             \
    const char *ttest_s__ = (const char *)(str);                                                   \
    const char *ttest_p__ = (const char *)(prefix);                                                \
    TTEST_CHECK__(ttest_str_starts_with__(ttest_s__, ttest_p__),                                   \
                  "expected \"%s\" to start with \"%s\"", ttest_cstr_or_null__(ttest_s__),         \
                  ttest_cstr_or_null__(ttest_p__));                                                \
  } while (0)
#define check_str_starts_with_warn(str, prefix)                                                    \
  do {                                                                                             \
    const char *ttest_s__ = (const char *)(str);                                                   \
    const char *ttest_p__ = (const char *)(prefix);                                                \
    TTEST_WARN__(ttest_str_starts_with__(ttest_s__, ttest_p__),                                    \
                 "expected \"%s\" to start with \"%s\"", ttest_cstr_or_null__(ttest_s__),          \
                 ttest_cstr_or_null__(ttest_p__));                                                 \
  } while (0)

#define check_str_ends_with(str, suffix)                                                           \
  do {                                                                                             \
    const char *ttest_s__ = (const char *)(str);                                                   \
    const char *ttest_x__ = (const char *)(suffix);                                                \
    TTEST_CHECK__(ttest_str_ends_with__(ttest_s__, ttest_x__),                                     \
                  "expected \"%s\" to end with \"%s\"", ttest_cstr_or_null__(ttest_s__),           \
                  ttest_cstr_or_null__(ttest_x__));                                                \
  } while (0)
#define check_str_ends_with_warn(str, suffix)                                                      \
  do {                                                                                             \
    const char *ttest_s__ = (const char *)(str);                                                   \
    const char *ttest_x__ = (const char *)(suffix);                                                \
    TTEST_WARN__(ttest_str_ends_with__(ttest_s__, ttest_x__),                                      \
                 "expected \"%s\" to end with \"%s\"", ttest_cstr_or_null__(ttest_s__),            \
                 ttest_cstr_or_null__(ttest_x__));                                                 \
  } while (0)

/* Memory comparisons */
#define check_mem_eq(actual, expected, len)                                                        \
  do {                                                                                             \
    const void *ttest_a__ = (const void *)(actual);                                                \
    const void *ttest_e__ = (const void *)(expected);                                              \
    size_t ttest_n__ = TTEST_CAST(size_t, (len));                                                  \
    TTEST_CHECK__(memcmp(ttest_a__, ttest_e__, ttest_n__) == 0, "memory mismatch at %zu bytes",    \
                  ttest_n__);                                                                      \
  } while (0)
#define check_mem_eq_warn(actual, expected, len)                                                   \
  do {                                                                                             \
    const void *ttest_a__ = (const void *)(actual);                                                \
    const void *ttest_e__ = (const void *)(expected);                                              \
    size_t ttest_n__ = TTEST_CAST(size_t, (len));                                                  \
    TTEST_WARN__(memcmp(ttest_a__, ttest_e__, ttest_n__) == 0, "memory mismatch at %zu bytes",     \
                 ttest_n__);                                                                       \
  } while (0)

#define check_mem_ne(actual, expected, len)                                                        \
  do {                                                                                             \
    const void *ttest_a__ = (const void *)(actual);                                                \
    const void *ttest_e__ = (const void *)(expected);                                              \
    size_t ttest_n__ = TTEST_CAST(size_t, (len));                                                  \
    TTEST_CHECK__(memcmp(ttest_a__, ttest_e__, ttest_n__) != 0,                                    \
                  "expected memory to differ at %zu bytes", ttest_n__);                            \
  } while (0)
#define check_mem_ne_warn(actual, expected, len)                                                   \
  do {                                                                                             \
    const void *ttest_a__ = (const void *)(actual);                                                \
    const void *ttest_e__ = (const void *)(expected);                                              \
    size_t ttest_n__ = TTEST_CAST(size_t, (len));                                                  \
    TTEST_WARN__(memcmp(ttest_a__, ttest_e__, ttest_n__) != 0,                                     \
                 "expected memory to differ at %zu bytes", ttest_n__);                             \
  } while (0)

/* Pointer checks */
#define check_not_null(ptr)                                                                        \
  do {                                                                                             \
    const void *ttest_ptr__ = (const void *)(ptr);                                                 \
    TTEST_CHECK__(ttest_ptr__ != NULL, "expected non-null but got NULL");                          \
  } while (0)
#define check_not_null_warn(ptr)                                                                   \
  do {                                                                                             \
    const void *ttest_ptr__ = (const void *)(ptr);                                                 \
    TTEST_WARN__(ttest_ptr__ != NULL, "expected non-null but got NULL");                           \
  } while (0)

#define check_null(ptr)                                                                            \
  do {                                                                                             \
    const void *ttest_ptr__ = (const void *)(ptr);                                                 \
    TTEST_CHECK__(ttest_ptr__ == NULL, "expected NULL but got %p", ttest_ptr__);                   \
  } while (0)
#define check_null_warn(ptr)                                                                       \
  do {                                                                                             \
    const void *ttest_ptr__ = (const void *)(ptr);                                                 \
    TTEST_WARN__(ttest_ptr__ == NULL, "expected NULL but got %p", ttest_ptr__);                    \
  } while (0)
#define check_is_null(ptr) check_null((ptr))
#define check_is_null_warn(ptr) check_null_warn((ptr))

/* Hex comparisons */
#define check_hex_eq(actual, expected)                                                             \
  TTEST_UINT_COMPARE__(TTEST_CHECK__, actual, expected, ==, "expected 0x%x but got 0x%x")

#define check_hex64_eq(actual, expected)                                                           \
  do {                                                                                             \
    unsigned long long ttest_a__ = TTEST_CAST(unsigned long long, (actual));                       \
    unsigned long long ttest_e__ = TTEST_CAST(unsigned long long, (expected));                     \
    TTEST_CHECK__(ttest_a__ == ttest_e__, "expected 0x%llx but got 0x%llx", ttest_e__, ttest_a__); \
  } while (0)

/* Boolean assertions */
#define check_true(actual) TTEST_CHECK__((actual), "expected true but got false")

#define check_false(actual) TTEST_CHECK__(!(actual), "expected false but got true")

/* Pointer address comparisons */
#define check_ptr_eq(actual, expected)                                                             \
  do {                                                                                             \
    const void *ttest_a__ = (const void *)(actual);                                                \
    const void *ttest_e__ = (const void *)(expected);                                              \
    TTEST_CHECK__(ttest_a__ == ttest_e__, "expected %p but got %p", ttest_e__, ttest_a__);         \
  } while (0)

#define check_ptr_ne(actual, expected)                                                             \
  do {                                                                                             \
    const void *ttest_a__ = (const void *)(actual);                                                \
    const void *ttest_e__ = (const void *)(expected);                                              \
    TTEST_CHECK__(ttest_a__ != ttest_e__, "expected != %p but got %p", ttest_e__, ttest_a__);      \
  } while (0)

/* Range assertion: lo <= actual <= hi */
#define check_int_range(actual, lo, hi)                                                            \
  do {                                                                                             \
    int ttest_a__ = TTEST_CAST(int, (actual));                                                     \
    int ttest_lo__ = TTEST_CAST(int, (lo));                                                        \
    int ttest_hi__ = TTEST_CAST(int, (hi));                                                        \
    TTEST_CHECK__(ttest_a__ >= ttest_lo__ && ttest_a__ <= ttest_hi__,                              \
                  "expected %d in range [%d, %d]", ttest_a__, ttest_lo__, ttest_hi__);             \
  } while (0)

/* Float special value assertions */
#define check_float_nan(actual)                                                                    \
  do {                                                                                             \
    double ttest_a__ = TTEST_CAST(double, (actual));                                               \
    TTEST_CHECK__(isnan(ttest_a__), "expected NaN but got %f", ttest_a__);                         \
  } while (0)
#define check_float_nan_warn(actual)                                                               \
  do {                                                                                             \
    double ttest_a__ = TTEST_CAST(double, (actual));                                               \
    TTEST_WARN__(isnan(ttest_a__), "expected NaN but got %f", ttest_a__);                          \
  } while (0)

#define check_float_inf(actual)                                                                    \
  do {                                                                                             \
    double ttest_a__ = TTEST_CAST(double, (actual));                                               \
    TTEST_CHECK__(isinf(ttest_a__), "expected Inf but got %f", ttest_a__);                         \
  } while (0)
#define check_float_inf_warn(actual)                                                               \
  do {                                                                                             \
    double ttest_a__ = TTEST_CAST(double, (actual));                                               \
    TTEST_WARN__(isinf(ttest_a__), "expected Inf but got %f", ttest_a__);                          \
  } while (0)

#define check_double_nan(actual) check_float_nan(actual)
#define check_double_nan_warn(actual) check_float_nan_warn(actual)

#define check_double_inf(actual) check_float_inf(actual)
#define check_double_inf_warn(actual) check_float_inf_warn(actual)

/* Bitmask assertion: (val & mask) == mask */
#define check_bits(actual, mask)                                                                   \
  do {                                                                                             \
    unsigned long long ttest_a__ = TTEST_CAST(unsigned long long, (actual));                       \
    unsigned long long ttest_m__ = TTEST_CAST(unsigned long long, (mask));                         \
    unsigned long long ttest_got__ = ttest_a__ & ttest_m__;                                        \
    TTEST_CHECK__(ttest_got__ == ttest_m__, "expected bits 0x%llx set in 0x%llx, got 0x%llx",      \
                  ttest_m__, ttest_a__, ttest_got__);                                              \
  } while (0)

/* --- Array comparison helpers --- */

static inline bool ttest_int_array_eq__(const int *actual, const int *expected, size_t n,
                                        size_t *fail_idx) {
  for (size_t i = 0; i < n; i++) {
    if (actual[i] != expected[i]) {
      *fail_idx = i;
      return false;
    }
  }
  return true;
}

static inline bool ttest_uint8_array_eq__(const unsigned char *actual,
                                          const unsigned char *expected, size_t n,
                                          size_t *fail_idx) {
  for (size_t i = 0; i < n; i++) {
    if (actual[i] != expected[i]) {
      *fail_idx = i;
      return false;
    }
  }
  return true;
}

static inline bool ttest_size_array_eq__(const size_t *actual, const size_t *expected, size_t n,
                                         size_t *fail_idx) {
  for (size_t i = 0; i < n; i++) {
    if (actual[i] != expected[i]) {
      *fail_idx = i;
      return false;
    }
  }
  return true;
}

static inline bool ttest_float_array_eq__(const float *actual, const float *expected, size_t n,
                                          double eps, size_t *fail_idx) {
  for (size_t i = 0; i < n; i++) {
    double d = TTEST_CAST(double, actual[i]) - TTEST_CAST(double, expected[i]);
    /* Match scalar check_float_eq semantics: NaN never compares equal. */
    if (!(fabs(d) <= eps)) {
      *fail_idx = i;
      return false;
    }
  }
  return true;
}

static inline bool ttest_double_array_eq__(const double *actual, const double *expected, size_t n,
                                           double eps, size_t *fail_idx) {
  for (size_t i = 0; i < n; i++) {
    double d = actual[i] - expected[i];
    /* Match scalar check_float_eq semantics: NaN never compares equal. */
    if (!(fabs(d) <= eps)) {
      *fail_idx = i;
      return false;
    }
  }
  return true;
}

static inline bool ttest_ptr_array_eq__(const void *const *actual, const void *const *expected,
                                        size_t n, size_t *fail_idx) {
  for (size_t i = 0; i < n; i++) {
    if (actual[i] != expected[i]) {
      *fail_idx = i;
      return false;
    }
  }
  return true;
}

static inline bool ttest_str_array_eq__(const char *const *actual, const char *const *expected,
                                        size_t n, size_t *fail_idx) {
  for (size_t i = 0; i < n; i++) {
    if (actual[i] == NULL && expected[i] == NULL) continue;
    if (actual[i] == NULL || expected[i] == NULL || strcmp(actual[i], expected[i]) != 0) {
      *fail_idx = i;
      return false;
    }
  }
  return true;
}

/* --- Array assertion macros --- */

#define check_int_array_eq(actual, expected, n)                                                    \
  do {                                                                                             \
    size_t ttest_fi__ = 0;                                                                         \
    if (!ttest_int_array_eq__((const int *)(actual), (const int *)(expected), (n), &ttest_fi__)) { \
      TTEST_CHECK__(0, "array mismatch at [%zu]: expected %d but got %d", ttest_fi__,              \
                    ((const int *)(expected))[ttest_fi__], ((const int *)(actual))[ttest_fi__]);   \
    }                                                                                              \
  } while (0)
#define check_int_array_eq_warn(actual, expected, n)                                               \
  do {                                                                                             \
    size_t ttest_fi__ = 0;                                                                         \
    if (!ttest_int_array_eq__((const int *)(actual), (const int *)(expected), (n), &ttest_fi__)) { \
      TTEST_WARN__(0, "array mismatch at [%zu]: expected %d but got %d", ttest_fi__,               \
                   ((const int *)(expected))[ttest_fi__], ((const int *)(actual))[ttest_fi__]);    \
    }                                                                                              \
  } while (0)

#define check_uint8_array_eq(actual, expected, n)                                                  \
  do {                                                                                             \
    size_t ttest_fi__ = 0;                                                                         \
    if (!ttest_uint8_array_eq__((const unsigned char *)(actual),                                   \
                                (const unsigned char *)(expected), (n), &ttest_fi__)) {            \
      TTEST_CHECK__(0, "array mismatch at [%zu]: expected 0x%02x but got 0x%02x", ttest_fi__,      \
                    ((const unsigned char *)(expected))[ttest_fi__],                               \
                    ((const unsigned char *)(actual))[ttest_fi__]);                                \
    }                                                                                              \
  } while (0)
#define check_uint8_array_eq_warn(actual, expected, n)                                             \
  do {                                                                                             \
    size_t ttest_fi__ = 0;                                                                         \
    if (!ttest_uint8_array_eq__((const unsigned char *)(actual),                                   \
                                (const unsigned char *)(expected), (n), &ttest_fi__)) {            \
      TTEST_WARN__(0, "array mismatch at [%zu]: expected 0x%02x but got 0x%02x", ttest_fi__,       \
                   ((const unsigned char *)(expected))[ttest_fi__],                                \
                   ((const unsigned char *)(actual))[ttest_fi__]);                                 \
    }                                                                                              \
  } while (0)

#define check_size_array_eq(actual, expected, n)                                                   \
  do {                                                                                             \
    size_t ttest_fi__ = 0;                                                                         \
    if (!ttest_size_array_eq__((const size_t *)(actual), (const size_t *)(expected), (n),          \
                               &ttest_fi__)) {                                                     \
      TTEST_CHECK__(0, "array mismatch at [%zu]: expected %zu but got %zu", ttest_fi__,            \
                    ((const size_t *)(expected))[ttest_fi__],                                      \
                    ((const size_t *)(actual))[ttest_fi__]);                                       \
    }                                                                                              \
  } while (0)
#define check_size_array_eq_warn(actual, expected, n)                                              \
  do {                                                                                             \
    size_t ttest_fi__ = 0;                                                                         \
    if (!ttest_size_array_eq__((const size_t *)(actual), (const size_t *)(expected), (n),          \
                               &ttest_fi__)) {                                                     \
      TTEST_WARN__(0, "array mismatch at [%zu]: expected %zu but got %zu", ttest_fi__,             \
                   ((const size_t *)(expected))[ttest_fi__],                                       \
                   ((const size_t *)(actual))[ttest_fi__]);                                        \
    }                                                                                              \
  } while (0)

#define check_float_array_eq(actual, expected, n, epsilon)                                         \
  do {                                                                                             \
    const float *ttest_a__ = (const float *)(actual);                                              \
    const float *ttest_e__ = (const float *)(expected);                                            \
    size_t ttest_n__ = TTEST_CAST(size_t, (n));                                                    \
    double ttest_eps__ = TTEST_CAST(double, (epsilon));                                            \
    size_t ttest_fi__ = 0;                                                                         \
    if (!ttest_float_array_eq__(ttest_a__, ttest_e__, ttest_n__, ttest_eps__, &ttest_fi__)) {     \
      TTEST_CHECK__(0, "array mismatch at [%zu]: expected %f but got %f (+/- %f)", ttest_fi__,     \
                    TTEST_CAST(double, ttest_e__[ttest_fi__]),                                     \
                    TTEST_CAST(double, ttest_a__[ttest_fi__]), ttest_eps__);                       \
    }                                                                                              \
  } while (0)
#define check_float_array_eq_warn(actual, expected, n, epsilon)                                    \
  do {                                                                                             \
    const float *ttest_a__ = (const float *)(actual);                                              \
    const float *ttest_e__ = (const float *)(expected);                                            \
    size_t ttest_n__ = TTEST_CAST(size_t, (n));                                                    \
    double ttest_eps__ = TTEST_CAST(double, (epsilon));                                            \
    size_t ttest_fi__ = 0;                                                                         \
    if (!ttest_float_array_eq__(ttest_a__, ttest_e__, ttest_n__, ttest_eps__, &ttest_fi__)) {     \
      TTEST_WARN__(0, "array mismatch at [%zu]: expected %f but got %f (+/- %f)", ttest_fi__,      \
                   TTEST_CAST(double, ttest_e__[ttest_fi__]),                                      \
                   TTEST_CAST(double, ttest_a__[ttest_fi__]), ttest_eps__);                        \
    }                                                                                              \
  } while (0)

#define check_double_array_eq(actual, expected, n, epsilon)                                        \
  do {                                                                                             \
    const double *ttest_a__ = (const double *)(actual);                                            \
    const double *ttest_e__ = (const double *)(expected);                                          \
    size_t ttest_n__ = TTEST_CAST(size_t, (n));                                                    \
    double ttest_eps__ = TTEST_CAST(double, (epsilon));                                            \
    size_t ttest_fi__ = 0;                                                                         \
    if (!ttest_double_array_eq__(ttest_a__, ttest_e__, ttest_n__, ttest_eps__, &ttest_fi__)) {    \
      TTEST_CHECK__(0, "array mismatch at [%zu]: expected %f but got %f (+/- %f)", ttest_fi__,     \
                    ttest_e__[ttest_fi__], ttest_a__[ttest_fi__], ttest_eps__);                   \
    }                                                                                              \
  } while (0)
#define check_double_array_eq_warn(actual, expected, n, epsilon)                                   \
  do {                                                                                             \
    const double *ttest_a__ = (const double *)(actual);                                            \
    const double *ttest_e__ = (const double *)(expected);                                          \
    size_t ttest_n__ = TTEST_CAST(size_t, (n));                                                    \
    double ttest_eps__ = TTEST_CAST(double, (epsilon));                                            \
    size_t ttest_fi__ = 0;                                                                         \
    if (!ttest_double_array_eq__(ttest_a__, ttest_e__, ttest_n__, ttest_eps__, &ttest_fi__)) {    \
      TTEST_WARN__(0, "array mismatch at [%zu]: expected %f but got %f (+/- %f)", ttest_fi__,      \
                   ttest_e__[ttest_fi__], ttest_a__[ttest_fi__], ttest_eps__);                    \
    }                                                                                              \
  } while (0)

#define check_ptr_array_eq(actual, expected, n)                                                    \
  do {                                                                                             \
    size_t ttest_fi__ = 0;                                                                         \
    if (!ttest_ptr_array_eq__((const void *const *)(actual), (const void *const *)(expected), (n), \
                              &ttest_fi__)) {                                                      \
      TTEST_CHECK__(0, "array mismatch at [%zu]: expected %p but got %p", ttest_fi__,              \
                    ((const void *const *)(expected))[ttest_fi__],                                 \
                    ((const void *const *)(actual))[ttest_fi__]);                                  \
    }                                                                                              \
  } while (0)
#define check_ptr_array_eq_warn(actual, expected, n)                                               \
  do {                                                                                             \
    size_t ttest_fi__ = 0;                                                                         \
    if (!ttest_ptr_array_eq__((const void *const *)(actual), (const void *const *)(expected), (n), \
                              &ttest_fi__)) {                                                      \
      TTEST_WARN__(0, "array mismatch at [%zu]: expected %p but got %p", ttest_fi__,               \
                   ((const void *const *)(expected))[ttest_fi__],                                  \
                   ((const void *const *)(actual))[ttest_fi__]);                                   \
    }                                                                                              \
  } while (0)

#define check_str_array_eq(actual, expected, n)                                                    \
  do {                                                                                             \
    size_t ttest_fi__ = 0;                                                                         \
    if (!ttest_str_array_eq__((const char *const *)(actual), (const char *const *)(expected), (n), \
                              &ttest_fi__)) {                                                      \
      TTEST_CHECK__(0, "array mismatch at [%zu]: expected \"%s\" but got \"%s\"", ttest_fi__,      \
                    ((const char *const *)(expected))[ttest_fi__]                                  \
                        ? ((const char *const *)(expected))[ttest_fi__]                            \
                        : "(null)",                                                                \
                    ((const char *const *)(actual))[ttest_fi__]                                    \
                        ? ((const char *const *)(actual))[ttest_fi__]                              \
                        : "(null)");                                                               \
    }                                                                                              \
  } while (0)
#define check_str_array_eq_warn(actual, expected, n)                                               \
  do {                                                                                             \
    size_t ttest_fi__ = 0;                                                                         \
    if (!ttest_str_array_eq__((const char *const *)(actual), (const char *const *)(expected), (n), \
                              &ttest_fi__)) {                                                      \
      TTEST_WARN__(0, "array mismatch at [%zu]: expected \"%s\" but got \"%s\"", ttest_fi__,       \
                   ((const char *const *)(expected))[ttest_fi__]                                   \
                       ? ((const char *const *)(expected))[ttest_fi__]                             \
                       : "(null)",                                                                 \
                   ((const char *const *)(actual))[ttest_fi__]                                     \
                       ? ((const char *const *)(actual))[ttest_fi__]                               \
                       : "(null)");                                                                \
    }                                                                                              \
  } while (0)

/* --- Non-fatal assertion --- */
/* Internal implementation that takes file and line.
 * Diagnostic guards mirror TTEST_CHECK_IMPL__: suppress -Wshadow and
 * -Wunused-value at expansion sites. */
#define TTEST_WARN_IMPL__(condition, file, line, ...)                                              \
  TTEST_DIAG_PUSH__                                                                                \
  TTEST_DIAG_IGNORE_SHADOW__                                                                       \
  TTEST_DIAG_IGNORE_UNUSED_VALUE__                                                                 \
  do {                                                                                             \
    if (ttest_active_config__) {                                                                   \
      if (!ttest_eval_bool__(!!(condition))) {                                                     \
        /* Warnings are diagnostics, not assertions; they must not count as   \
         * passed assertions in the summary. */                                \
        char *ttest_message__ = ttest_format__(__VA_ARGS__);                                       \
        ++ttest_active_config__->warn_count;                                                       \
        ttest_indent__(stdout, ttest_active_config__->current_test                                 \
                                   ? ttest_active_config__->current_test->level + 1                \
                                   : 1);                                                           \
        if (ttest_active_config__->use_color) {                                                    \
          printf(TTEST_COLOR_YELLOW__ "Warning:" TTEST_COLOR_RESET__ " %s", ttest_message__);      \
        } else {                                                                                   \
          printf("Warning: %s", ttest_message__);                                                  \
        }                                                                                          \
        printf(" at %s:%s\n", file, line);                                                         \
        free(ttest_message__);                                                                     \
      }                                                                                            \
    }                                                                                              \
  } while (0)                                                                                      \
  TTEST_DIAG_POP__

/* Wrapper that captures __FILE__ and __LINE__ */
#define TTEST_WARN__(condition, ...)                                                               \
  TTEST_WARN_IMPL__(condition, __FILE__, __STRING__LINE__, __VA_ARGS__)

#define TTEST_WARN_ONE__(condition)                                                                \
  TTEST_WARN_IMPL__(condition, __FILE__, __STRING__LINE__, #condition)

#define check_warn(...) TTEST_MACRO__(TTEST_WARN_, __VA_ARGS__)

static inline void ttest_fail_framework__(ttest_config_type__ *config, const char *file,
                                          const char *line, const char *format, ...) {
  if (!config || config->run != TTEST_TEST_RUN__ || !config->current_test) {
    fprintf(stderr, "tinytest: framework failure outside an active test\n");
    abort();
  }

  va_list va;
  va_start(va, format);
  char *message = ttest_vformat__(format, va);
  va_end(va);

  ++config->assertion_count;
  ++config->assertion_failed_count;
  snprintf(config->location_buf, sizeof(config->location_buf), "at %s:%s", file, line);
  config->location = config->location_buf;

  const char *prefix = "Framework error: ";
  size_t bufflen = strlen(prefix) + strlen(message) + 1;
  config->error = TTEST_CAST(char *, calloc(bufflen, sizeof(char)));
  if (!config->error) {
    free(message);
    perror("calloc(config->error)");
    abort();
  }
  snprintf(config->error, bufflen, "%s%s", prefix, message);
  free(message);
  ttest_longjmp_fail__(config);
}

static inline bool ttest_bench_require_work__(ttest_config_type__ *config, const char *title,
                                              size_t samples, size_t operations_per_sample,
                                              size_t bytes_per_sample, bool tracks_bytes,
                                              const char *file, const char *line) {
  const char *safe_title = title ? title : "(null)";
  if (samples == 0) {
    ttest_fail_framework__(config, file, line, "benchmark \"%s\" requires at least one sample",
                           safe_title);
    return false;
  }
  if (operations_per_sample == 0) {
    ttest_fail_framework__(config, file, line,
                           "benchmark \"%s\" requires at least one operation per sample",
                           safe_title);
    return false;
  }
  if (tracks_bytes && bytes_per_sample == 0) {
    ttest_fail_framework__(config, file, line,
                           "benchmark \"%s\" requires at least one byte per sample", safe_title);
    return false;
  }
  return true;
}

/* --- Info context --- */
#define info(...)                                                                                  \
  do {                                                                                             \
    char *ttest_info_msg__ = ttest_format__(__VA_ARGS__);                                          \
    size_t ttest_info_msg_len__ = strlen(ttest_info_msg__);                                        \
    if (ttest_active_config__->info_len + ttest_info_msg_len__ + 2 <                               \
        sizeof(ttest_active_config__->info_buffer)) {                                              \
      if (ttest_active_config__->info_len > 0) {                                                   \
        ttest_active_config__->info_buffer[ttest_active_config__->info_len++] = ' ';               \
      }                                                                                            \
      memcpy(ttest_active_config__->info_buffer + ttest_active_config__->info_len,                 \
             ttest_info_msg__, ttest_info_msg_len__);                                              \
      ttest_active_config__->info_len += ttest_info_msg_len__;                                     \
      ttest_active_config__->info_buffer[ttest_active_config__->info_len] = '\0';                  \
    }                                                                                              \
    free(ttest_info_msg__);                                                                        \
  } while (0)

#define capture(var, fmt) info(#var "=" fmt, (var))

/* --- BDD keywords (given/when/then) --- */
#define given(...) describe("Given " __VA_ARGS__)
#define when(...) describe("When " __VA_ARGS__)
#define then(...) it("Then " __VA_ARGS__)
#define and_given(...) describe("And given " __VA_ARGS__)
#define and_when(...) describe("And when " __VA_ARGS__)
#define and_then(...) it("And then " __VA_ARGS__)

/* --- Section (TDD SECTION equivalent) --- */
#define section(...) describe(__VA_ARGS__)

/* --- Benchmarking --- */
/* Usage:
 *   benchmark_batch("one operation", samples) { code; }
 *   benchmark_ops("batched operations", samples, operations_per_sample) { code; }
 *   benchmark_bytes("buffer scan", samples, bytes_per_sample) { code; }
 *   benchmark_io("packet batch", samples, operations_per_sample, bytes_per_sample) { code; }
 */
/* Compatibility shim: benchmark titles are no longer configurable. */
#define benchmark_titles(name_title, input_title, iters_title, avg_title, ns_title, min_title,     \
                         max_title, ops_title, size_title, bw_title)                               \
  if (1)

#define TTEST_BENCHMARK_IMPL__(title, sample_count, operation_count, byte_count, track_bytes)      \
  for (                                                                                            \
      struct {                                                                                     \
        int __done;                                                                                \
        size_t __samples;                                                                          \
        size_t __operations_per_sample;                                                            \
        size_t __bytes_per_sample;                                                                 \
        bool __tracks_bytes;                                                                       \
        double __min;                                                                              \
        double __max;                                                                              \
        double __sum;                                                                              \
        const char *__title;                                                                       \
      } ttest_bm__ = {0,                                                                           \
                      TTEST_CAST(size_t, (sample_count)),                                          \
                      TTEST_CAST(size_t, (operation_count)),                                       \
                      TTEST_CAST(size_t, (byte_count)),                                            \
                      TTEST_CAST(bool, (track_bytes)),                                             \
                      1e18,                                                                        \
                      0.0,                                                                         \
                      0.0,                                                                         \
                      (title)};                                                                    \
      !ttest_bm__.__done &&                                                                        \
      ttest_bench_require_work__(                                                                  \
          ttest_active_config__, ttest_bm__.__title, ttest_bm__.__samples,                         \
          ttest_bm__.__operations_per_sample, ttest_bm__.__bytes_per_sample,                       \
          ttest_bm__.__tracks_bytes, __FILE__, __STRING__LINE__);                                  \
      ttest_bm__.__done = 1,                                                                       \
        ttest_bench_print__(                                                                       \
            ttest_active_config__, ttest_bm__.__title, ttest_bm__.__samples, ttest_bm__.__sum,     \
            ttest_bm__.__min, ttest_bm__.__max, ttest_bm__.__operations_per_sample,                \
            ttest_bm__.__bytes_per_sample, ttest_bm__.__tracks_bytes,                              \
            ttest_active_config__->current_test ? ttest_active_config__->current_test->level + 1   \
                                                : 1,                                               \
            ttest_active_config__->use_color))                                                     \
    for (size_t ttest_bm_i__ = 0; ttest_bm_i__ < ttest_bm__.__samples; ++ttest_bm_i__)             \
      for (struct {                                                                                \
             int __done;                                                                           \
             double __started_ms;                                                                  \
             double __elapsed_ms;                                                                  \
           } ttest_bm_timer__ = {0, ttest_get_time_ms__(), 0.0};                                  \
           !ttest_bm_timer__.__done;                                                               \
           ttest_bm_timer__.__done = 1,                                                            \
                 ttest_bm_timer__.__elapsed_ms =                                                   \
                     ttest_get_time_ms__() - ttest_bm_timer__.__started_ms,                        \
                 ttest_bm__.__sum += ttest_bm_timer__.__elapsed_ms,                                \
                 ttest_bm__.__min = ttest_bm_timer__.__elapsed_ms < ttest_bm__.__min               \
                                        ? ttest_bm_timer__.__elapsed_ms                             \
                                        : ttest_bm__.__min,                                        \
                 ttest_bm__.__max = ttest_bm_timer__.__elapsed_ms > ttest_bm__.__max               \
                                        ? ttest_bm_timer__.__elapsed_ms                             \
                                        : ttest_bm__.__max)

#define benchmark_batch(title, samples) TTEST_BENCHMARK_IMPL__(title, samples, 1, 0, false)
#define benchmark_ops(title, samples, operations_per_sample)                                       \
  TTEST_BENCHMARK_IMPL__(title, samples, operations_per_sample, 0, false)
#define benchmark_bytes(title, samples, bytes_per_sample)                                          \
  TTEST_BENCHMARK_IMPL__(title, samples, 1, bytes_per_sample, true)
#define benchmark_io(title, samples, operations_per_sample, bytes_per_sample)                       \
  TTEST_BENCHMARK_IMPL__(title, samples, operations_per_sample, bytes_per_sample, true)

/* Source-compatible legacy alias. Prefer an explicit benchmark_* macro in new code. */
#define benchmark(title, samples, operations_per_sample)                                           \
  benchmark_ops(title, samples, operations_per_sample)

#ifndef __cplusplus

/* --- C11 _Generic generic assertions for C mode --- */
#if !defined(TTEST_HAS_C11_GENERIC__)
  /* _Generic is part of C11 (§6.5.1.1) and is implemented by GCC, Clang,
   * and MSVC cl.exe starting with VS 2019 v16.8 (_MSC_VER >= 1928) when
   * compiled with /std:c11 or /std:c17 (__STDC_VERSION__ >= 201112L). */
  #if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    #if !defined(_MSC_VER) || (_MSC_VER >= 1928)
      #define TTEST_HAS_C11_GENERIC__ 1
    #endif
  #endif
#endif

#if defined(TTEST_HAS_C11_GENERIC__)

static inline void ttest_c11_check_eq_int(long long actual, long long expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(actual == expected, file, line, "expected %lld but got %lld", expected, actual);
}
static inline void ttest_c11_check_eq_int_warn(long long actual, long long expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(actual == expected, file, line, "expected %lld but got %lld", expected, actual);
}

static inline void ttest_c11_check_eq_uint(unsigned long long actual, unsigned long long expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(actual == expected, file, line, "expected %llu but got %llu", expected, actual);
}
static inline void ttest_c11_check_eq_uint_warn(unsigned long long actual, unsigned long long expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(actual == expected, file, line, "expected %llu but got %llu", expected, actual);
}

static inline void ttest_c11_check_eq_bool(bool actual, bool expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__((expected ? 1 : 0) == (actual ? 1 : 0), file, line, "expected %d but got %d",
                     (expected ? 1 : 0), (actual ? 1 : 0));
}
static inline void ttest_c11_check_eq_bool_warn(bool actual, bool expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__((expected ? 1 : 0) == (actual ? 1 : 0), file, line, "expected %d but got %d",
                    (expected ? 1 : 0), (actual ? 1 : 0));
}

static inline void ttest_c11_check_eq_double(double actual, double expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(fabs(actual - expected) <= 1e-9, file, line, "expected %f (+/- 1e-9) but got %f", expected, actual);
}
static inline void ttest_c11_check_eq_double_warn(double actual, double expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(fabs(actual - expected) <= 1e-9, file, line, "expected %f (+/- 1e-9) but got %f", expected, actual);
}

static inline void ttest_c11_check_eq_long_double(long double actual, long double expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(fabsl(actual - expected) <= 1e-18L, file, line, "expected %Lf (+/- 1e-18) but got %Lf", expected, actual);
}
static inline void ttest_c11_check_eq_long_double_warn(long double actual, long double expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(fabsl(actual - expected) <= 1e-18L, file, line, "expected %Lf (+/- 1e-18) but got %Lf", expected, actual);
}

/* float: dedicated handler uses fabsf and a float-appropriate tolerance (8 * FLT_EPSILON ~ 1e-6).
 * Routing float to the double handler was imprecise: fabs promotes to double,
 * and the 1e-9 threshold is tighter than meaningful float resolution. */
static inline void ttest_c11_check_eq_float(float actual, float expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(fabsf(actual - expected) <= 8.0f * FLT_EPSILON * (fabsf(actual) > 1.0f ? fabsf(actual) : 1.0f),
                     file, line, "expected %f (+/- 8*FLT_EPSILON*|v|) but got %f", expected, actual);
}
static inline void ttest_c11_check_eq_float_warn(float actual, float expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(fabsf(actual - expected) <= 8.0f * FLT_EPSILON * (fabsf(actual) > 1.0f ? fabsf(actual) : 1.0f),
                    file, line, "expected %f (+/- 8*FLT_EPSILON*|v|) but got %f", expected, actual);
}

static inline void ttest_c11_check_eq_str(const char *actual, const char *expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(ttest_str_eq__(actual, expected), file, line, "expected \"%s\" but got \"%s\"",
                     ttest_cstr_or_null__(expected), ttest_cstr_or_null__(actual));
}
static inline void ttest_c11_check_eq_str_warn(const char *actual, const char *expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(ttest_str_eq__(actual, expected), file, line, "expected \"%s\" but got \"%s\"",
                    ttest_cstr_or_null__(expected), ttest_cstr_or_null__(actual));
}

static inline void ttest_c11_check_eq_ptr(const void *actual, const void *expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(actual == expected, file, line, "expected %p but got %p", expected, actual);
}
static inline void ttest_c11_check_eq_ptr_warn(const void *actual, const void *expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(actual == expected, file, line, "expected %p but got %p", expected, actual);
}

static inline void ttest_c11_check_ne_int(long long actual, long long expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(actual != expected, file, line, "expected != %lld but got %lld", expected, actual);
}
static inline void ttest_c11_check_ne_int_warn(long long actual, long long expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(actual != expected, file, line, "expected != %lld but got %lld", expected, actual);
}

static inline void ttest_c11_check_ne_uint(unsigned long long actual, unsigned long long expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(actual != expected, file, line, "expected != %llu but got %llu", expected, actual);
}
static inline void ttest_c11_check_ne_uint_warn(unsigned long long actual, unsigned long long expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(actual != expected, file, line, "expected != %llu but got %llu", expected, actual);
}

static inline void ttest_c11_check_ne_bool(bool actual, bool expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__((expected ? 1 : 0) != (actual ? 1 : 0), file, line, "expected != %d but got %d",
                     (expected ? 1 : 0), (actual ? 1 : 0));
}
static inline void ttest_c11_check_ne_bool_warn(bool actual, bool expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__((expected ? 1 : 0) != (actual ? 1 : 0), file, line, "expected != %d but got %d",
                    (expected ? 1 : 0), (actual ? 1 : 0));
}

static inline void ttest_c11_check_ne_double(double actual, double expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(fabs(actual - expected) > 1e-9, file, line, "expected != %f (+/- 1e-9) but got %f", expected, actual);
}
static inline void ttest_c11_check_ne_double_warn(double actual, double expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(fabs(actual - expected) > 1e-9, file, line, "expected != %f (+/- 1e-9) but got %f", expected, actual);
}

static inline void ttest_c11_check_ne_long_double(long double actual, long double expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(fabsl(actual - expected) > 1e-18L, file, line, "expected != %Lf (+/- 1e-18) but got %Lf", expected, actual);
}
static inline void ttest_c11_check_ne_long_double_warn(long double actual, long double expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(fabsl(actual - expected) > 1e-18L, file, line, "expected != %Lf (+/- 1e-18) but got %Lf", expected, actual);
}

/* float: dedicated ne handler mirrors check_eq_float, using relative tolerance. */
static inline void ttest_c11_check_ne_float(float actual, float expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(fabsf(actual - expected) > 8.0f * FLT_EPSILON * (fabsf(actual) > 1.0f ? fabsf(actual) : 1.0f),
                     file, line, "expected != %f (+/- 8*FLT_EPSILON*|v|) but got %f", expected, actual);
}
static inline void ttest_c11_check_ne_float_warn(float actual, float expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(fabsf(actual - expected) > 8.0f * FLT_EPSILON * (fabsf(actual) > 1.0f ? fabsf(actual) : 1.0f),
                    file, line, "expected != %f (+/- 8*FLT_EPSILON*|v|) but got %f", expected, actual);
}

static inline void ttest_c11_check_ne_str(const char *actual, const char *expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(ttest_str_ne__(actual, expected), file, line, "expected != \"%s\" but got \"%s\"",
                     ttest_cstr_or_null__(expected), ttest_cstr_or_null__(actual));
}
static inline void ttest_c11_check_ne_str_warn(const char *actual, const char *expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(ttest_str_ne__(actual, expected), file, line, "expected != \"%s\" but got \"%s\"",
                    ttest_cstr_or_null__(expected), ttest_cstr_or_null__(actual));
}

static inline void ttest_c11_check_ne_ptr(const void *actual, const void *expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(actual != expected, file, line, "expected != %p but got %p", expected, actual);
}
static inline void ttest_c11_check_ne_ptr_warn(const void *actual, const void *expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(actual != expected, file, line, "expected != %p but got %p", expected, actual);
}

static inline void ttest_c11_check_gt_int(long long actual, long long expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(actual > expected, file, line, "expected > %lld but got %lld", expected, actual);
}
static inline void ttest_c11_check_gt_int_warn(long long actual, long long expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(actual > expected, file, line, "expected > %lld but got %lld", expected, actual);
}

static inline void ttest_c11_check_gt_uint(unsigned long long actual, unsigned long long expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(actual > expected, file, line, "expected > %llu but got %llu", expected, actual);
}
static inline void ttest_c11_check_gt_uint_warn(unsigned long long actual, unsigned long long expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(actual > expected, file, line, "expected > %llu but got %llu", expected, actual);
}

static inline void ttest_c11_check_gt_double(double actual, double expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(actual > expected, file, line, "expected > %f but got %f", expected, actual);
}
static inline void ttest_c11_check_gt_double_warn(double actual, double expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(actual > expected, file, line, "expected > %f but got %f", expected, actual);
}

static inline void ttest_c11_check_gt_long_double(long double actual, long double expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(actual > expected, file, line, "expected > %Lf but got %Lf", expected, actual);
}
static inline void ttest_c11_check_gt_long_double_warn(long double actual, long double expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(actual > expected, file, line, "expected > %Lf but got %Lf", expected, actual);
}

static inline void ttest_c11_check_lt_int(long long actual, long long expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(actual < expected, file, line, "expected < %lld but got %lld", expected, actual);
}
static inline void ttest_c11_check_lt_int_warn(long long actual, long long expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(actual < expected, file, line, "expected < %lld but got %lld", expected, actual);
}

static inline void ttest_c11_check_lt_uint(unsigned long long actual, unsigned long long expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(actual < expected, file, line, "expected < %llu but got %llu", expected, actual);
}
static inline void ttest_c11_check_lt_uint_warn(unsigned long long actual, unsigned long long expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(actual < expected, file, line, "expected < %llu but got %llu", expected, actual);
}

static inline void ttest_c11_check_lt_double(double actual, double expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(actual < expected, file, line, "expected < %f but got %f", expected, actual);
}
static inline void ttest_c11_check_lt_double_warn(double actual, double expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(actual < expected, file, line, "expected < %f but got %f", expected, actual);
}

static inline void ttest_c11_check_lt_long_double(long double actual, long double expected, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(actual < expected, file, line, "expected < %Lf but got %Lf", expected, actual);
}
static inline void ttest_c11_check_lt_long_double_warn(long double actual, long double expected, const char *file, const char *line) {
  TTEST_WARN_IMPL__(actual < expected, file, line, "expected < %Lf but got %Lf", expected, actual);
}

static inline void ttest_c11_check_in_range_int(long long actual, long long min, long long max, const char *file,
                                                const char *line) {
  TTEST_CHECK_IMPL__(actual >= min && actual <= max, file, line,
                     "expected %lld in range [%lld, %lld], got %lld", min, max, actual);
}
static inline void ttest_c11_check_in_range_int_warn(long long actual, long long min, long long max, const char *file,
                                                     const char *line) {
  TTEST_WARN_IMPL__(actual >= min && actual <= max, file, line,
                    "expected %lld in range [%lld, %lld], got %lld", min, max, actual);
}

static inline void ttest_c11_check_in_range_uint(unsigned long long actual, unsigned long long min,
                                                unsigned long long max, const char *file, const char *line) {
  TTEST_CHECK_IMPL__(actual >= min && actual <= max, file, line,
                     "expected %llu in range [%llu, %llu], got %llu", min, max, actual);
}
static inline void ttest_c11_check_in_range_uint_warn(unsigned long long actual, unsigned long long min,
                                                     unsigned long long max, const char *file, const char *line) {
  TTEST_WARN_IMPL__(actual >= min && actual <= max, file, line,
                    "expected %llu in range [%llu, %llu], got %llu", min, max, actual);
}

static inline void ttest_c11_check_in_range_double(double actual, double min, double max, const char *file,
                                                  const char *line) {
  TTEST_CHECK_IMPL__(actual >= min && actual <= max, file, line,
                     "expected %f in range [%f, %f], got %f", min, max, actual);
}
static inline void ttest_c11_check_in_range_double_warn(double actual, double min, double max, const char *file,
                                                       const char *line) {
  TTEST_WARN_IMPL__(actual >= min && actual <= max, file, line,
                    "expected %f in range [%f, %f], got %f", min, max, actual);
}

static inline void ttest_c11_check_in_range_long_double(long double actual, long double min, long double max,
                                                       const char *file, const char *line) {
  TTEST_CHECK_IMPL__(actual >= min && actual <= max, file, line,
                     "expected %Lf in range [%Lf, %Lf], got %Lf", min, max, actual);
}
static inline void ttest_c11_check_in_range_long_double_warn(long double actual, long double min, long double max,
                                                            const char *file, const char *line) {
  TTEST_WARN_IMPL__(actual >= min && actual <= max, file, line,
                    "expected %Lf in range [%Lf, %Lf], got %Lf", min, max, actual);
}

#define check_equal(actual, expected) \
  _Generic((actual), \
    char:               ttest_c11_check_eq_int, \
    signed char:        ttest_c11_check_eq_int, \
    unsigned char:      ttest_c11_check_eq_uint, \
    short:              ttest_c11_check_eq_int, \
    unsigned short:     ttest_c11_check_eq_uint, \
    int:                ttest_c11_check_eq_int, \
    unsigned int:       ttest_c11_check_eq_uint, \
    long:               ttest_c11_check_eq_int, \
    unsigned long:      ttest_c11_check_eq_uint, \
    long long:          ttest_c11_check_eq_int, \
    unsigned long long: ttest_c11_check_eq_uint, \
    bool:               ttest_c11_check_eq_bool, \
    float:              ttest_c11_check_eq_float, \
    double:             ttest_c11_check_eq_double, \
    long double:        ttest_c11_check_eq_long_double, \
    char*:              ttest_c11_check_eq_str, \
    const char*:        ttest_c11_check_eq_str, \
    void*:              ttest_c11_check_eq_ptr, \
    const void*:        ttest_c11_check_eq_ptr, \
    default:            ttest_c11_check_eq_int \
  )((actual), (expected), __FILE__, __STRING__LINE__)

#define check_equal_warn(actual, expected) \
  _Generic((actual), \
    char:               ttest_c11_check_eq_int_warn, \
    signed char:        ttest_c11_check_eq_int_warn, \
    unsigned char:      ttest_c11_check_eq_uint_warn, \
    short:              ttest_c11_check_eq_int_warn, \
    unsigned short:     ttest_c11_check_eq_uint_warn, \
    int:                ttest_c11_check_eq_int_warn, \
    unsigned int:       ttest_c11_check_eq_uint_warn, \
    long:               ttest_c11_check_eq_int_warn, \
    unsigned long:      ttest_c11_check_eq_uint_warn, \
    long long:          ttest_c11_check_eq_int_warn, \
    unsigned long long: ttest_c11_check_eq_uint_warn, \
    bool:               ttest_c11_check_eq_bool_warn, \
    float:              ttest_c11_check_eq_float_warn, \
    double:             ttest_c11_check_eq_double_warn, \
    long double:        ttest_c11_check_eq_long_double_warn, \
    char*:              ttest_c11_check_eq_str_warn, \
    const char*:        ttest_c11_check_eq_str_warn, \
    void*:              ttest_c11_check_eq_ptr_warn, \
    const void*:        ttest_c11_check_eq_ptr_warn, \
    default:            ttest_c11_check_eq_int_warn \
  )((actual), (expected), __FILE__, __STRING__LINE__)

#define check_not_equal(actual, expected) \
  _Generic((actual), \
    char:               ttest_c11_check_ne_int, \
    signed char:        ttest_c11_check_ne_int, \
    unsigned char:      ttest_c11_check_ne_uint, \
    short:              ttest_c11_check_ne_int, \
    unsigned short:     ttest_c11_check_ne_uint, \
    int:                ttest_c11_check_ne_int, \
    unsigned int:       ttest_c11_check_ne_uint, \
    long:               ttest_c11_check_ne_int, \
    unsigned long:      ttest_c11_check_ne_uint, \
    long long:          ttest_c11_check_ne_int, \
    unsigned long long: ttest_c11_check_ne_uint, \
    bool:               ttest_c11_check_ne_bool, \
    float:              ttest_c11_check_ne_float, \
    double:             ttest_c11_check_ne_double, \
    long double:        ttest_c11_check_ne_long_double, \
    char*:              ttest_c11_check_ne_str, \
    const char*:        ttest_c11_check_ne_str, \
    void*:              ttest_c11_check_ne_ptr, \
    const void*:        ttest_c11_check_ne_ptr, \
    default:            ttest_c11_check_ne_int \
  )((actual), (expected), __FILE__, __STRING__LINE__)

#define check_not_equal_warn(actual, expected) \
  _Generic((actual), \
    char:               ttest_c11_check_ne_int_warn, \
    signed char:        ttest_c11_check_ne_int_warn, \
    unsigned char:      ttest_c11_check_ne_uint_warn, \
    short:              ttest_c11_check_ne_int_warn, \
    unsigned short:     ttest_c11_check_ne_uint_warn, \
    int:                ttest_c11_check_ne_int_warn, \
    unsigned int:       ttest_c11_check_ne_uint_warn, \
    long:               ttest_c11_check_ne_int_warn, \
    unsigned long:      ttest_c11_check_ne_uint_warn, \
    long long:          ttest_c11_check_ne_int_warn, \
    unsigned long long: ttest_c11_check_ne_uint_warn, \
    bool:               ttest_c11_check_ne_bool_warn, \
    float:              ttest_c11_check_ne_float_warn, \
    double:             ttest_c11_check_ne_double_warn, \
    long double:        ttest_c11_check_ne_long_double_warn, \
    char*:              ttest_c11_check_ne_str_warn, \
    const char*:        ttest_c11_check_ne_str_warn, \
    void*:              ttest_c11_check_ne_ptr_warn, \
    const void*:        ttest_c11_check_ne_ptr_warn, \
    default:            ttest_c11_check_ne_int_warn \
  )((actual), (expected), __FILE__, __STRING__LINE__)

#define check_greater(actual, expected) \
  _Generic((actual), \
    char:               ttest_c11_check_gt_int, \
    signed char:        ttest_c11_check_gt_int, \
    unsigned char:      ttest_c11_check_gt_uint, \
    short:              ttest_c11_check_gt_int, \
    unsigned short:     ttest_c11_check_gt_uint, \
    int:                ttest_c11_check_gt_int, \
    unsigned int:       ttest_c11_check_gt_uint, \
    long:               ttest_c11_check_gt_int, \
    unsigned long:      ttest_c11_check_gt_uint, \
    long long:          ttest_c11_check_gt_int, \
    unsigned long long: ttest_c11_check_gt_uint, \
    bool:               ttest_c11_check_gt_int, \
    float:              ttest_c11_check_gt_double, \
    double:             ttest_c11_check_gt_double, \
    long double:        ttest_c11_check_gt_long_double, \
    default:            ttest_c11_check_gt_int \
  )((actual), (expected), __FILE__, __STRING__LINE__)

#define check_greater_warn(actual, expected) \
  _Generic((actual), \
    char:               ttest_c11_check_gt_int_warn, \
    signed char:        ttest_c11_check_gt_int_warn, \
    unsigned char:      ttest_c11_check_gt_uint_warn, \
    short:              ttest_c11_check_gt_int_warn, \
    unsigned short:     ttest_c11_check_gt_uint_warn, \
    int:                ttest_c11_check_gt_int_warn, \
    unsigned int:       ttest_c11_check_gt_uint_warn, \
    long:               ttest_c11_check_gt_int_warn, \
    unsigned long:      ttest_c11_check_gt_uint_warn, \
    long long:          ttest_c11_check_gt_int_warn, \
    unsigned long long: ttest_c11_check_gt_uint_warn, \
    bool:               ttest_c11_check_gt_int_warn, \
    float:              ttest_c11_check_gt_double_warn, \
    double:             ttest_c11_check_gt_double_warn, \
    long double:        ttest_c11_check_gt_long_double_warn, \
    default:            ttest_c11_check_gt_int_warn \
  )((actual), (expected), __FILE__, __STRING__LINE__)

#define check_less(actual, expected) \
  _Generic((actual), \
    char:               ttest_c11_check_lt_int, \
    signed char:        ttest_c11_check_lt_int, \
    unsigned char:      ttest_c11_check_lt_uint, \
    short:              ttest_c11_check_lt_int, \
    unsigned short:     ttest_c11_check_lt_uint, \
    int:                ttest_c11_check_lt_int, \
    unsigned int:       ttest_c11_check_lt_uint, \
    long:               ttest_c11_check_lt_int, \
    unsigned long:      ttest_c11_check_lt_uint, \
    long long:          ttest_c11_check_lt_int, \
    unsigned long long: ttest_c11_check_lt_uint, \
    bool:               ttest_c11_check_lt_int, \
    float:              ttest_c11_check_lt_double, \
    double:             ttest_c11_check_lt_double, \
    long double:        ttest_c11_check_lt_long_double, \
    default:            ttest_c11_check_lt_int \
  )((actual), (expected), __FILE__, __STRING__LINE__)

#define check_less_warn(actual, expected) \
  _Generic((actual), \
    char:               ttest_c11_check_lt_int_warn, \
    signed char:        ttest_c11_check_lt_int_warn, \
    unsigned char:      ttest_c11_check_lt_uint_warn, \
    short:              ttest_c11_check_lt_int_warn, \
    unsigned short:     ttest_c11_check_lt_uint_warn, \
    int:                ttest_c11_check_lt_int_warn, \
    unsigned int:       ttest_c11_check_lt_uint_warn, \
    long:               ttest_c11_check_lt_int_warn, \
    unsigned long:      ttest_c11_check_lt_uint_warn, \
    long long:          ttest_c11_check_lt_int_warn, \
    unsigned long long: ttest_c11_check_lt_uint_warn, \
    bool:               ttest_c11_check_lt_int_warn, \
    float:              ttest_c11_check_lt_double_warn, \
    double:             ttest_c11_check_lt_double_warn, \
    long double:        ttest_c11_check_lt_long_double_warn, \
    default:            ttest_c11_check_lt_int_warn \
  )((actual), (expected), __FILE__, __STRING__LINE__)

#define check_in_range(actual, min, max) \
  _Generic((actual), \
    char:               ttest_c11_check_in_range_int, \
    signed char:        ttest_c11_check_in_range_int, \
    unsigned char:      ttest_c11_check_in_range_uint, \
    short:              ttest_c11_check_in_range_int, \
    unsigned short:     ttest_c11_check_in_range_uint, \
    int:                ttest_c11_check_in_range_int, \
    unsigned int:       ttest_c11_check_in_range_uint, \
    long:               ttest_c11_check_in_range_int, \
    unsigned long:      ttest_c11_check_in_range_uint, \
    long long:          ttest_c11_check_in_range_int, \
    unsigned long long: ttest_c11_check_in_range_uint, \
    bool:               ttest_c11_check_in_range_int, \
    float:              ttest_c11_check_in_range_double, \
    double:             ttest_c11_check_in_range_double, \
    long double:        ttest_c11_check_in_range_long_double, \
    default:            ttest_c11_check_in_range_int \
  )((actual), (min), (max), __FILE__, __STRING__LINE__)

#define check_in_range_warn(actual, min, max) \
  _Generic((actual), \
    char:               ttest_c11_check_in_range_int_warn, \
    signed char:        ttest_c11_check_in_range_int_warn, \
    unsigned char:      ttest_c11_check_in_range_uint_warn, \
    short:              ttest_c11_check_in_range_int_warn, \
    unsigned short:     ttest_c11_check_in_range_uint_warn, \
    int:                ttest_c11_check_in_range_int_warn, \
    unsigned int:       ttest_c11_check_in_range_uint_warn, \
    long:               ttest_c11_check_in_range_int_warn, \
    unsigned long:      ttest_c11_check_in_range_uint_warn, \
    long long:          ttest_c11_check_in_range_int_warn, \
    unsigned long long: ttest_c11_check_in_range_uint_warn, \
    bool:               ttest_c11_check_in_range_int_warn, \
    float:              ttest_c11_check_in_range_double_warn, \
    double:             ttest_c11_check_in_range_double_warn, \
    long double:        ttest_c11_check_in_range_long_double_warn, \
    default:            ttest_c11_check_in_range_int_warn \
  )((actual), (min), (max), __FILE__, __STRING__LINE__)

#define check_between(actual, min, max) \
  check_in_range((actual), (min), (max))
#define check_between_warn(actual, min, max) \
  check_in_range_warn((actual), (min), (max))

#else

/* Pre-C11 C fallback */
#define check_equal(actual, expected) check_int_eq(actual, expected)
#define check_equal_warn(actual, expected) check_int_eq_warn(actual, expected)
#define check_not_equal(actual, expected) check_int_ne(actual, expected)
#define check_not_equal_warn(actual, expected) check_int_ne_warn(actual, expected)
#define check_greater(actual, expected) check_int_gt(actual, expected)
#define check_greater_warn(actual, expected) check_int_gt_warn(actual, expected)
#define check_less(actual, expected) check_int_lt(actual, expected)
#define check_less_warn(actual, expected) check_int_lt_warn(actual, expected)
#define check_in_range(actual, min, max) check_int_range((actual), (min), (max))
#define check_between(actual, min, max) check_in_range((actual), (min), (max))

#endif /* C11 check */
#endif /* !__cplusplus */

/* Use before_all()/after_all() as the cross-language names for one-time setup/teardown hooks. */
#define before_all()                                                                               \
  TTEST_NODE__(ttest_node_flags_none__, list_before, TTEST_NODE_INTERIM__, "before")
#define after_all() TTEST_NODE__(ttest_node_flags_none__, list_after, TTEST_NODE_INTERIM__, "after")

#ifdef _MSC_VER
  #pragma warning(pop)
#endif

#endif /* TINYTEST_H */
