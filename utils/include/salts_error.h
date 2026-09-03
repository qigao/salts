#ifndef SALTS_ERROR_H
#define SALTS_ERROR_H

#include "platform.h"
#include <salts/error_codes.h>
#include <cmeta/enum.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

Enum(salts_error_domain_t,
     (SALTS_ERROR_DOMAIN_NONE, 0, "none"),
     (SALTS_ERROR_DOMAIN_SALTS, 1, "salts"),
     (SALTS_ERROR_DOMAIN_CUSTOM, 2, "custom"),
     (SALTS_ERROR_DOMAIN_POSIX, 3, "posix"),
     (SALTS_ERROR_DOMAIN_WIN32, 4, "win32"),
     (SALTS_ERROR_DOMAIN_UNKNOWN, 5, "unknown"));

/* Compatibility surface for the pre-CMeta domain enum API. */
static inline size_t salts_error_domain_count(void) {
  return salts_error_domain_t_meta()->count;
}

static inline const char *salts_error_domain_to_string(salts_error_domain_t value) {
  const char *text = salts_error_domain_t_to_string(value);
  return text ? text : "unknown";
}

static inline int salts_error_domain_from_string(const char *text,
                                                 salts_error_domain_t *out) {
  salts_error_domain_t parsed;
  const char *parsed_text;

  if (!text || !out || !salts_error_domain_t_from_string(text, &parsed)) return -1;
  parsed_text = salts_error_domain_t_to_string(parsed);
  if (!parsed_text || strcmp(parsed_text, text) != 0) return -1;
  *out = parsed;
  return 0;
}

static inline bool salts_error_domain_is_valid(salts_error_domain_t value) {
  return salts_error_domain_t_to_string(value) != NULL;
}

static inline bool salts_error_domain_equals(salts_error_domain_t lhs,
                                             salts_error_domain_t rhs) {
  return lhs == rhs;
}

#define SALTS_ERROR_CUSTOM_DOMAIN_MIN 1
#define SALTS_ERROR_CUSTOM_DOMAIN_MAX 32767
#define SALTS_ERROR_CUSTOM_LOCAL_MAX 65535
#define SALTS_ERROR_CUSTOM(domain, local)                                                           \
  (-(int)(((((domain) & 0x7fff) << 16) | ((local) & 0xffff))))
#define SALTS_ERROR_CUSTOM_DOMAIN(code) (((-(code)) >> 16) & 0x7fff)
#define SALTS_ERROR_CUSTOM_LOCAL(code) ((-(code)) & 0xffff)

typedef struct {
  int code;
  int custom_domain;
  salts_error_domain_t domain;
  const char *domain_name;
  const char *name;
  const char *message;
} salts_error_info_t;

typedef struct {
  int code;
  const char *name;
  const char *message;
} salts_error_entry_t;

typedef struct {
  int domain;
  const char *domain_name;
  const salts_error_entry_t *entries;
  size_t count;
} salts_error_domain_desc_t;

typedef struct {
  bool ok;
  int code;
  const char *message;
} salts_result_t;

/** Return human-readable text for SALTS_*, negative errno, or negative Win32 error codes. */
SALTS_C_API const char *salts_strerror(int err);

/** Return structured metadata for an error code. */
SALTS_C_API salts_error_info_t salts_error_info(int err);

/**
 * Register a custom error domain.
 *
 * The descriptor and entry table must remain alive until process exit or until
 * salts_error_unregister_domain() is called. Domain ids must be in
 * [SALTS_ERROR_CUSTOM_DOMAIN_MIN, SALTS_ERROR_CUSTOM_DOMAIN_MAX].
 */
SALTS_C_API int salts_error_register_domain(const salts_error_domain_desc_t *domain);

/** Remove a previously registered custom domain. */
SALTS_C_API int salts_error_unregister_domain(int domain);

static inline salts_result_t salts_result_ok(void) {
  salts_result_t r;
  r.ok = true;
  r.code = SALTS_OK;
  r.message = "success";
  return r;
}

static inline salts_result_t salts_result_err(int code) {
  salts_result_t r;
  r.ok = false;
  r.code = code;
  r.message = salts_strerror(code);
  return r;
}

static inline salts_result_t salts_result_from_code(int code) {
  return code == SALTS_OK ? salts_result_ok() : salts_result_err(code);
}

static inline bool salts_result_is_ok(salts_result_t r) { return r.ok; }
static inline bool salts_result_is_err(salts_result_t r) { return !r.ok; }

#ifdef __cplusplus
}
#endif

#endif /* SALTS_ERROR_H */
