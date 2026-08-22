/**
 * @file tlog.h
 * @brief High-performance async logger with multi-sink support
 *
 * Architecture:
 * - Multiple sinks (console, file, custom callback)
 * - Async mode with lock-free ring buffer
 * - Memory pool for zero-alloc hot path
 * - Simple vtable-based sink interface
 */

#ifndef tlog_h
#define tlog_h

#include "platform.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <cmeta/enum.h>
#include <cmeta/struct.h>
#include "fmt.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_MESSAGE_SIZE 4096

// =============================================================================
// Log Levels
// =============================================================================

Enum(turbo_log_level_t,
     (TURBO_LOG_LEVEL_DEBUG, 0, "DEBUG"),
     (TURBO_LOG_LEVEL_INFO, 1, "INFO"),
     (TURBO_LOG_LEVEL_WARN, 2, "WARN"),
     (TURBO_LOG_LEVEL_ERROR, 3, "ERROR"),
     (TURBO_LOG_LEVEL_FATAL, 4, "FATAL"));

// =============================================================================
// Log Entry (passed to sinks)
// =============================================================================

/**
 * Log entry passed to sinks.
 *
 * component, file, and message are borrowed for the callback duration. The
 * reflected descriptor does not own or extend the lifetime of these strings.
 */
Struct(turbo_log_entry_t,
    (turbo_log_level_t, level),
    (uint64_t, timestamp_ms),
    (uint32_t, thread_id),
    (const char *, component),
    (const char *, file),
    (int, line),
    (const char *, message),
    (size_t, message_len)
);

// =============================================================================
// Sink Interface (vtable pattern)
// =============================================================================

typedef struct turbo_log_sink_s turbo_log_sink_t;

/* Low-level sink vtable callbacks are kept for ABI/source compatibility.
 * New custom sinks should use turbo_sink_custom_create().
 */
typedef void (*turbo_sink_write_fn)(turbo_log_sink_t *sink, const turbo_log_entry_t *entry);
typedef void (*turbo_sink_flush_fn)(turbo_log_sink_t *sink);
typedef void (*turbo_sink_destroy_fn)(turbo_log_sink_t *sink);
typedef int (*turbo_sink_filter_fn)(const turbo_log_entry_t *entry, void *user_data);
typedef void (*turbo_sink_custom_write_fn)(const turbo_log_entry_t *entry, void *user_data);
typedef void (*turbo_sink_custom_flush_fn)(void *user_data);
typedef void (*turbo_sink_custom_destroy_fn)(void *user_data);

typedef enum {
  TURBO_SINK_BORROWED = 0,
  TURBO_SINK_OWNED = 1
} turbo_sink_ownership_t;

TURBO_C_API int turbo_sink_set_min_level(turbo_log_sink_t *sink, turbo_log_level_t level);
TURBO_C_API turbo_log_level_t turbo_sink_get_min_level(const turbo_log_sink_t *sink);
TURBO_C_API int turbo_sink_set_user_data(turbo_log_sink_t *sink, void *user_data);
TURBO_C_API void *turbo_sink_get_user_data(const turbo_log_sink_t *sink);

// =============================================================================
// Built-in Sinks
// =============================================================================

/**
 * @brief Format pattern placeholders:
 *   {time}      - Timestamp (YYYY-MM-DD HH:MM:SS)
 *   {time_ms}   - Timestamp with milliseconds
 *   {level}     - Log level (DEBUG, INFO, WARN, ERROR, FATAL)
 *   {component} - Component name
 *   {file}      - Source file name
 *   {line}      - Line number
 *   {thread}    - Thread ID
 *   {message}   - Log message
 *
 * Example: "[{time}] [{level}] [{component}] {message}"
 * Default: "[{time}] [{level}] {message}"
 */
#define TURBO_LOG_DEFAULT_PATTERN "[{time}] [{level}] {message}"

#ifndef TURBO_LOG_CAPTURE_SOURCE
  #ifdef NDEBUG
    #define TURBO_LOG_CAPTURE_SOURCE 0
  #else
    #define TURBO_LOG_CAPTURE_SOURCE 1
  #endif
#endif

#if TURBO_LOG_CAPTURE_SOURCE
  #define TURBO_LOG_SOURCE_FILE __FILE__
  #define TURBO_LOG_SOURCE_LINE __LINE__
  #define TURBO_LOG_FULL_PATTERN                                                                   \
    "[{time_ms}] [{level}] [{thread}] [{component}] ({file}:{line}) {message}"
#else
  #define TURBO_LOG_SOURCE_FILE NULL
  #define TURBO_LOG_SOURCE_LINE 0
  #define TURBO_LOG_FULL_PATTERN "[{time_ms}] [{level}] [{thread}] [{component}] {message}"
#endif

/**
 * @brief Console sink options
 */
typedef struct {
  FILE *output;        // stdout/stderr (default: stdout)
  int use_colors;      // ANSI colors (default: 1)
  const char *pattern; // Format pattern (default: TURBO_LOG_DEFAULT_PATTERN)
                       // TURBO_LOG_FULL_PATTERN includes file:line only when
                       // TURBO_LOG_CAPTURE_SOURCE is enabled.
} turbo_console_sink_opts_t;

/**
 * @brief File sink options
 */
typedef struct {
  const char *path;    // Log file path
  size_t max_size;     // Max file size before rotation (0 = no limit)
  int max_files;       // Max rotated files to keep (0 = no rotation)
  int append;          // Append to existing file (default: 1)
  const char *pattern; // Format pattern (default: "[{time}] [{level}] {message}")
} turbo_file_sink_opts_t;

/**
 * @brief Callback sink - custom log handling
 */
typedef void (*turbo_log_callback_fn)(const turbo_log_entry_t *entry, void *user_data);

typedef struct {
  turbo_sink_custom_write_fn write;
  turbo_sink_custom_flush_fn flush;
  turbo_sink_custom_destroy_fn destroy;
  void *user_data;
} turbo_sink_custom_opts_t;

/**
 * @brief Create console sink (stdout/stderr with optional colors)
 */
TURBO_C_API turbo_log_sink_t *turbo_sink_console_create(const turbo_console_sink_opts_t *opts);

/**
 * @brief Create file sink with optional rotation
 *
 * Uses turbo_fs_pwrite for atomic append operations without mutex locks
 * on the POSIX write path. Windows uses serialized positional I/O behind
 * turbo_fs_pwrite to preserve the same offset semantics with CRT file handles.
 * Rotation is protected by mutex but happens rarely.
 *
 * Best for: All file logging scenarios, especially high-concurrency
 */
TURBO_C_API turbo_log_sink_t *turbo_sink_file_create(const turbo_file_sink_opts_t *opts);

/**
 * @brief Create callback sink for custom handling
 */
TURBO_C_API turbo_log_sink_t *turbo_sink_callback_create(turbo_log_callback_fn callback,
                                                       void *user_data);

/**
 * @brief Create an opaque custom sink with optional flush/destroy callbacks.
 *
 * Ownership of opts->user_data remains with the returned sink only after this
 * function succeeds. On failure, the caller still owns opts->user_data.
 */
TURBO_C_API turbo_log_sink_t *turbo_sink_custom_create(const turbo_sink_custom_opts_t *opts);

typedef struct {
  turbo_log_level_t min_level;
  turbo_log_level_t max_level;
  const char *component;            // Optional exact component match
  turbo_sink_filter_fn predicate;   // Optional extra predicate, non-zero means allow
  void *predicate_user_data;
} turbo_sink_filter_opts_t;
#define TURBO_SINK_FILTER_OPTS_DEFAULT \
  { TURBO_LOG_LEVEL_DEBUG, TURBO_LOG_LEVEL_FATAL, NULL, NULL, NULL }

/**
 * @brief Create a decorator sink that filters entries before forwarding to inner.
 *
 * Pass NULL for default DEBUG..FATAL filtering, or initialize opts with
 * TURBO_SINK_FILTER_OPTS_DEFAULT before overriding selected fields.
 * Ownership of inner is transferred only after this function succeeds.
 */
TURBO_C_API turbo_log_sink_t *turbo_sink_filter_create(turbo_log_sink_t *inner,
                                                     turbo_sink_ownership_t ownership,
                                                     const turbo_sink_filter_opts_t *opts);

/**
 * @brief Create a decorator sink that formats entries before forwarding to inner.
 *
 * The inner sink receives an entry whose message points to a decorator-owned
 * stack buffer valid only for the duration of the inner write call.
 * Ownership of inner is transferred only after this function succeeds.
 */
TURBO_C_API turbo_log_sink_t *turbo_sink_format_create(turbo_log_sink_t *inner,
                                                     turbo_sink_ownership_t ownership,
                                                     const char *pattern);

typedef struct {
  uint64_t entries_seen;
  uint64_t entries_forwarded;
  uint64_t entries_filtered;
  uint64_t bytes_forwarded;
} turbo_sink_metrics_t;

/**
 * @brief Create a decorator sink that records metrics and forwards to inner.
 *
 * The returned sink may be added to a logger like any other sink. When ownership
 * is TURBO_SINK_OWNED, destroying the decorator also destroys inner.
 * Ownership of inner is transferred only after this function succeeds.
 */
TURBO_C_API turbo_log_sink_t *turbo_sink_metrics_create(turbo_log_sink_t *inner,
                                                      turbo_sink_ownership_t ownership);

/**
 * @brief Read metrics from a sink created by turbo_sink_metrics_create.
 * @return 0 on success, -1 if sink is not a metrics decorator or args are invalid.
 */
TURBO_C_API int turbo_sink_metrics_snapshot(turbo_log_sink_t *sink, turbo_sink_metrics_t *out);

/**
 * @brief Destroy a sink
 */
TURBO_C_API void turbo_sink_destroy(turbo_log_sink_t *sink);

// =============================================================================
// Logger
// =============================================================================

typedef struct tlog_s tlog_t;

/**
 * @brief Logger configuration
 */
typedef struct {
  turbo_log_level_t min_level; // Global minimum level
  size_t buffer_size;          // Ring buffer size for async (default: 64KB)
  size_t pool_size;            // Memory pool size (default: 32KB)
} tlog_config_t;

/**
 * @brief Create logger with configuration
 */
TURBO_C_API tlog_t *tlog_create(const tlog_config_t *config);

/**
 * @brief Destroy logger and all attached sinks
 */
TURBO_C_API void tlog_destroy(tlog_t *logger);

/**
 * @brief Add sink to logger and transfer ownership
 * @param logger Target logger
 * @param sink Sink to add (ownership transferred on success)
 * @return 0 on success, -1 on failure
 * 
 * OWNERSHIP SEMANTICS:
 * - On SUCCESS (returns 0): Logger takes ownership. DO NOT call turbo_sink_destroy(sink).
 *                           The sink will be destroyed automatically with the logger.
 * - On FAILURE (returns -1): Caller retains ownership. MUST call turbo_sink_destroy(sink)
 *                           to avoid memory leak.
 * 
 * THREAD SAFETY: NOT thread-safe. Do not call concurrently with other tlog_*
 *                functions on the same logger.
 * 
 * CORRECT USAGE:
 *   turbo_log_sink_t *sink = turbo_sink_console_create(NULL);
 *   if (!sink) return -1;
 *   
 *   if (tlog_add_sink(logger, sink) != 0) {
 *     turbo_sink_destroy(sink);  // ← Cleanup on failure
 *     return -1;
 *   }
 *   // sink is now owned by logger, will be destroyed with logger
 * 
 * INCORRECT USAGE:
 *   turbo_log_sink_t *sink = turbo_sink_console_create(NULL);
 *   tlog_add_sink(logger, sink);  // ❌ Ignores error
 *   turbo_sink_destroy(sink);     // ❌ Double-free if add succeeded!
 * 
 * MULTIPLE SINKS:
 *   // Each sink is independently owned after successful add
 *   turbo_log_sink_t *console = turbo_sink_console_create(NULL);
 *   turbo_log_sink_t *file = turbo_sink_file_create(&file_opts);
 *   
 *   if (tlog_add_sink(logger, console) != 0) {
 *     turbo_sink_destroy(console);
 *     turbo_sink_destroy(file);  // ← Still our responsibility
 *     return -1;
 *   }
 *   
 *   if (tlog_add_sink(logger, file) != 0) {
 *     turbo_sink_destroy(file);   // ← Still our responsibility
 *     // console already owned by logger, will be cleaned up
 *     return -1;
 *   }
 *   // Both sinks now owned by logger
 */
TURBO_C_API int tlog_add_sink(tlog_t *logger, turbo_log_sink_t *sink);

/**
 * @brief Remove sink from logger without destroying it
 *
 * Call tlog_flush(logger) before removing when logs already published before
 * removal must still be delivered to this sink.
 */
TURBO_C_API void tlog_remove_sink(tlog_t *logger, turbo_log_sink_t *sink);

/**
 * @brief Flush all sinks (blocks until async queue is drained)
 */
TURBO_C_API void tlog_flush(tlog_t *logger);

// =============================================================================
// Logging Functions
// =============================================================================

/**
 * @brief Log a pre-formatted string directly
 */
TURBO_C_API void turbo_log_str(tlog_t *logger, turbo_log_level_t level, const char *component,
                             const char *file, int line, const char *message, size_t message_len);

/**
 * @brief Log a message using typed arguments (auto-detects types for {})
 */
TURBO_C_API void turbo_log_typed(tlog_t *logger, turbo_log_level_t level,
                               const char *component, const char *file, int line, const char *fmt,
                               const fmt_arg_t *args, size_t arg_count);

// =============================================================================
// Level Control
// =============================================================================

/**
 * @brief Set logger minimum level.
 * @return 0 on success, -1 if logger is NULL or level is invalid.
 */
TURBO_C_API int tlog_set_level_ex(tlog_t *logger, turbo_log_level_t level);

/**
 * @brief Backward-compatible level setter. Invalid inputs are ignored.
 */
TURBO_C_API void tlog_set_level(tlog_t *logger, turbo_log_level_t level);
TURBO_C_API turbo_log_level_t tlog_get_level(const tlog_t *logger);

// =============================================================================
// Default Logger
// =============================================================================

TURBO_C_API void tlog_set_default(tlog_t *logger);
TURBO_C_API tlog_t *tlog_get_default(void);
TURBO_C_API tlog_t *tlog_peek_default(void);

// =============================================================================
// Statistics (for monitoring)
// =============================================================================

/**
 * @brief Get total logs written
 */
TURBO_C_API uint64_t tlog_get_written(const tlog_t *logger);

/**
 * @brief Get total logs dropped (due to backpressure in async mode)
 */
TURBO_C_API uint64_t tlog_get_dropped(const tlog_t *logger);

/**
 * @brief Get current async queue size
 */
TURBO_C_API int tlog_get_queue_size(const tlog_t *logger);

// =============================================================================
// Utility Functions
// =============================================================================

TURBO_C_API const char *turbo_log_level_name(turbo_log_level_t level);
TURBO_C_API turbo_log_level_t turbo_log_level_from_name(const char *name);

#ifdef __cplusplus
}
#endif

// =============================================================================
// Convenience Macros
// =============================================================================
//
// Raw-message forms have fixed arity and route directly to turbo_log_str.
// Formatted forms use the F suffix, require at least one formatting argument,
// and route to turbo_log_typed. This keeps strict-C11 call sites free of
// empty-__VA_ARGS__ detection and comma-elision extensions.

#define TURBO_LOG_RAW_IMPL(logger_expr, lvl, comp, message_expr)                                  \
  do {                                                                                             \
    tlog_t *_tlog_ptr = (logger_expr);                                                             \
    if (_tlog_ptr && (lvl) >= tlog_get_level(_tlog_ptr)) {                                        \
      const char *_tlog_message = (message_expr);                                                  \
      turbo_log_str(_tlog_ptr, (lvl), (comp), TURBO_LOG_SOURCE_FILE,                              \
                    TURBO_LOG_SOURCE_LINE, _tlog_message,                                          \
                    _tlog_message ? strlen(_tlog_message) : 0U);                                   \
    }                                                                                              \
  } while (0)

#ifdef __cplusplus

template <typename... Args>
inline void turbo_log_cpp_wrapper(tlog_t *logger, turbo_log_level_t level,
                                  const char *component, const char *file, int line,
                                  const char *pattern, const Args &...args) {
  static_assert(sizeof...(Args) > 0, "formatted logging requires at least one argument");
  const fmt_arg_t arg_array[] = {FMT_ARG(args)...};
  turbo_log_typed(logger, level, component, file, line, pattern, arg_array, sizeof...(Args));
}

#define TURBO_LOG_FORMAT_IMPL(logger_expr, lvl, comp, pattern, ...)                               \
  do {                                                                                             \
    tlog_t *_tlog_ptr = (logger_expr);                                                             \
    if (_tlog_ptr && (lvl) >= tlog_get_level(_tlog_ptr)) {                                        \
      turbo_log_cpp_wrapper(_tlog_ptr, (lvl), (comp), TURBO_LOG_SOURCE_FILE,                      \
                            TURBO_LOG_SOURCE_LINE, (pattern), __VA_ARGS__);                         \
    }                                                                                              \
  } while (0)

#else

#define TURBO_LOG_FORMAT_IMPL(logger_expr, lvl, comp, pattern, ...)                               \
  do {                                                                                             \
    tlog_t *_tlog_ptr = (logger_expr);                                                             \
    if (_tlog_ptr && (lvl) >= tlog_get_level(_tlog_ptr)) {                                        \
      turbo_log_typed(_tlog_ptr, (lvl), (comp), TURBO_LOG_SOURCE_FILE,                            \
                      TURBO_LOG_SOURCE_LINE, (pattern), FMT_ARGS(__VA_ARGS__),                     \
                      (size_t)FMT_ARG_COUNT(__VA_ARGS__));                                         \
    }                                                                                              \
  } while (0)

#endif

#define TURBO_LOG(logger, level, component, message)                                               \
  TURBO_LOG_RAW_IMPL((logger), (level), (component), (message))
#define TURBO_LOGF(logger, level, component, pattern, ...)                                         \
  TURBO_LOG_FORMAT_IMPL((logger), (level), (component), (pattern), __VA_ARGS__)

#define TURBO_LOG_DEBUG(logger, component, message)                                                \
  TURBO_LOG_RAW_IMPL((logger), TURBO_LOG_LEVEL_DEBUG, (component), (message))
#define TURBO_LOG_INFO(logger, component, message)                                                 \
  TURBO_LOG_RAW_IMPL((logger), TURBO_LOG_LEVEL_INFO, (component), (message))
#define TURBO_LOG_WARN(logger, component, message)                                                 \
  TURBO_LOG_RAW_IMPL((logger), TURBO_LOG_LEVEL_WARN, (component), (message))
#define TURBO_LOG_ERROR(logger, component, message)                                                \
  TURBO_LOG_RAW_IMPL((logger), TURBO_LOG_LEVEL_ERROR, (component), (message))
#define TURBO_LOG_FATAL(logger, component, message)                                                \
  TURBO_LOG_RAW_IMPL((logger), TURBO_LOG_LEVEL_FATAL, (component), (message))

#define TURBO_LOG_DEBUGF(logger, component, pattern, ...)                                          \
  TURBO_LOG_FORMAT_IMPL((logger), TURBO_LOG_LEVEL_DEBUG, (component), (pattern), __VA_ARGS__)
#define TURBO_LOG_INFOF(logger, component, pattern, ...)                                           \
  TURBO_LOG_FORMAT_IMPL((logger), TURBO_LOG_LEVEL_INFO, (component), (pattern), __VA_ARGS__)
#define TURBO_LOG_WARNF(logger, component, pattern, ...)                                           \
  TURBO_LOG_FORMAT_IMPL((logger), TURBO_LOG_LEVEL_WARN, (component), (pattern), __VA_ARGS__)
#define TURBO_LOG_ERRORF(logger, component, pattern, ...)                                          \
  TURBO_LOG_FORMAT_IMPL((logger), TURBO_LOG_LEVEL_ERROR, (component), (pattern), __VA_ARGS__)
#define TURBO_LOG_FATALF(logger, component, pattern, ...)                                          \
  TURBO_LOG_FORMAT_IMPL((logger), TURBO_LOG_LEVEL_FATAL, (component), (pattern), __VA_ARGS__)

#define TLOG_DEBUG(message)                                                                        \
  TURBO_LOG_RAW_IMPL(tlog_peek_default(), TURBO_LOG_LEVEL_DEBUG, NULL, (message))
#define TLOG_INFO(message)                                                                         \
  TURBO_LOG_RAW_IMPL(tlog_get_default(), TURBO_LOG_LEVEL_INFO, NULL, (message))
#define TLOG_WARN(message)                                                                         \
  TURBO_LOG_RAW_IMPL(tlog_get_default(), TURBO_LOG_LEVEL_WARN, NULL, (message))
#define TLOG_ERROR(message)                                                                        \
  TURBO_LOG_RAW_IMPL(tlog_get_default(), TURBO_LOG_LEVEL_ERROR, NULL, (message))
#define TLOG_FATAL(message)                                                                        \
  TURBO_LOG_RAW_IMPL(tlog_get_default(), TURBO_LOG_LEVEL_FATAL, NULL, (message))

#define TLOG_DEBUGF(pattern, ...)                                                                  \
  TURBO_LOG_FORMAT_IMPL(tlog_peek_default(), TURBO_LOG_LEVEL_DEBUG, NULL, (pattern), __VA_ARGS__)
#define TLOG_INFOF(pattern, ...)                                                                   \
  TURBO_LOG_FORMAT_IMPL(tlog_get_default(), TURBO_LOG_LEVEL_INFO, NULL, (pattern), __VA_ARGS__)
#define TLOG_WARNF(pattern, ...)                                                                   \
  TURBO_LOG_FORMAT_IMPL(tlog_get_default(), TURBO_LOG_LEVEL_WARN, NULL, (pattern), __VA_ARGS__)
#define TLOG_ERRORF(pattern, ...)                                                                  \
  TURBO_LOG_FORMAT_IMPL(tlog_get_default(), TURBO_LOG_LEVEL_ERROR, NULL, (pattern), __VA_ARGS__)
#define TLOG_FATALF(pattern, ...)                                                                  \
  TURBO_LOG_FORMAT_IMPL(tlog_get_default(), TURBO_LOG_LEVEL_FATAL, NULL, (pattern), __VA_ARGS__)

#endif // tlog_h