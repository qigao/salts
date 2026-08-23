#ifndef TURBO_ERROR_H
#define TURBO_ERROR_H

#include "platform.h"
#include <turbo/error_codes.h>
#include <cmeta/enum.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

Enum(turbo_error_domain_t,
     (TURBO_ERROR_DOMAIN_NONE, 0, "none"),
     (TURBO_ERROR_DOMAIN_TURBO, 1, "turbo"),
     (TURBO_ERROR_DOMAIN_CUSTOM, 2, "custom"),
     (TURBO_ERROR_DOMAIN_POSIX, 3, "posix"),
     (TURBO_ERROR_DOMAIN_WIN32, 4, "win32"),
     (TURBO_ERROR_DOMAIN_UNKNOWN, 5, "unknown"));

/* Compatibility surface for the pre-CMeta domain enum API. */
static inline size_t turbo_error_domain_count(void) {
  return turbo_error_domain_t_meta()->count;
}

static inline const char *turbo_error_domain_to_string(turbo_error_domain_t value) {
  const char *text = turbo_error_domain_t_to_string(value);
  return text ? text : "unknown";
}

static inline int turbo_error_domain_from_string(const char *text,
                                                 turbo_error_domain_t *out) {
  turbo_error_domain_t parsed;
  const char *parsed_text;

  if (!text || !out || !turbo_error_domain_t_from_string(text, &parsed)) return -1;
  parsed_text = turbo_error_domain_t_to_string(parsed);
  if (!parsed_text || strcmp(parsed_text, text) != 0) return -1;
  *out = parsed;
  return 0;
}

static inline bool turbo_error_domain_is_valid(turbo_error_domain_t value) {
  return turbo_error_domain_t_to_string(value) != NULL;
}

static inline bool turbo_error_domain_equals(turbo_error_domain_t lhs,
                                             turbo_error_domain_t rhs) {
  return lhs == rhs;
}

#define TURBO_ERROR_CUSTOM_DOMAIN_MIN 1
#define TURBO_ERROR_CUSTOM_DOMAIN_MAX 32767
#define TURBO_ERROR_CUSTOM_LOCAL_MAX 65535
#define TURBO_ERROR_CUSTOM(domain, local)                                                           \
  (-(int)(((((domain) & 0x7fff) << 16) | ((local) & 0xffff))))
#define TURBO_ERROR_CUSTOM_DOMAIN(code) (((-(code)) >> 16) & 0x7fff)
#define TURBO_ERROR_CUSTOM_LOCAL(code) ((-(code)) & 0xffff)

typedef struct {
  int code;
  int custom_domain;
  turbo_error_domain_t domain;
  const char *domain_name;
  const char *name;
  const char *message;
} turbo_error_info_t;

typedef struct {
  int code;
  const char *name;
  const char *message;
} turbo_error_entry_t;

typedef struct {
  int domain;
  const char *domain_name;
  const turbo_error_entry_t *entries;
  size_t count;
} turbo_error_domain_desc_t;

typedef struct {
  bool ok;
  int code;
  const char *message;
} turbo_result_t;

/** Return human-readable text for TURBO_*, negative errno, or negative Win32 error codes. */
TURBO_C_API const char *turbo_strerror(int err);

/** Return structured metadata for an error code. */
TURBO_C_API turbo_error_info_t turbo_error_info(int err);

/**
 * Register a custom error domain.
 *
 * The descriptor and entry table must remain alive until process exit or until
 * turbo_error_unregister_domain() is called. Domain ids must be in
 * [TURBO_ERROR_CUSTOM_DOMAIN_MIN, TURBO_ERROR_CUSTOM_DOMAIN_MAX].
 */
TURBO_C_API int turbo_error_register_domain(const turbo_error_domain_desc_t *domain);

/** Remove a previously registered custom domain. */
TURBO_C_API int turbo_error_unregister_domain(int domain);

static inline turbo_result_t turbo_result_ok(void) {
  turbo_result_t r;
  r.ok = true;
  r.code = TURBO_OK;
  r.message = "success";
  return r;
}

static inline turbo_result_t turbo_result_err(int code) {
  turbo_result_t r;
  r.ok = false;
  r.code = code;
  r.message = turbo_strerror(code);
  return r;
}

static inline turbo_result_t turbo_result_from_code(int code) {
  return code == TURBO_OK ? turbo_result_ok() : turbo_result_err(code);
}

static inline bool turbo_result_is_ok(turbo_result_t r) { return r.ok; }
static inline bool turbo_result_is_err(turbo_result_t r) { return !r.ok; }

#ifdef __cplusplus
}
#endif

#endif /* TURBO_ERROR_H */
