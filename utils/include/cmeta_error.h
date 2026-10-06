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

Enum(cmeta_error_domain_t,
     (SALTS_ERROR_DOMAIN_NONE, 0, "none"),
     (SALTS_ERROR_DOMAIN_SALTS, 1, "salts"),
     (SALTS_ERROR_DOMAIN_CUSTOM, 2, "custom"),
     (SALTS_ERROR_DOMAIN_POSIX, 3, "posix"),
     (SALTS_ERROR_DOMAIN_WIN32, 4, "win32"),
     (SALTS_ERROR_DOMAIN_UNKNOWN, 5, "unknown"));

/* Compatibility surface for the pre-CMeta domain enum API. */
static inline size_t cmeta_error_domain_count(void) {
  return cmeta_error_domain_t_meta()->count;
}

static inline const char *cmeta_error_domain_to_string(cmeta_error_domain_t value) {
  const char *text = cmeta_error_domain_t_to_string(value);
  return text ? text : "unknown";
}

static inline int cmeta_error_domain_from_string(const char *text,
                                                 cmeta_error_domain_t *out) {
  cmeta_error_domain_t parsed;
  const char *parsed_text;

  if (!text || !out || !cmeta_error_domain_t_from_string(text, &parsed)) return -1;
  parsed_text = cmeta_error_domain_t_to_string(parsed);
  if (!parsed_text || strcmp(parsed_text, text) != 0) return -1;
  *out = parsed;
  return 0;
}

static inline bool cmeta_error_domain_is_valid(cmeta_error_domain_t value) {
  return cmeta_error_domain_t_to_string(value) != NULL;
}

static inline bool cmeta_error_domain_equals(cmeta_error_domain_t lhs,
                                             cmeta_error_domain_t rhs) {
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
  cmeta_error_domain_t domain;
  const char *domain_name;
  const char *name;
  const char *message;
} cmeta_error_info_t;

typedef struct {
  int code;
  const char *name;
  const char *message;
} cmeta_error_entry_t;

typedef struct {
  int domain;
  const char *domain_name;
  const cmeta_error_entry_t *entries;
  size_t count;
} cmeta_error_domain_desc_t;

typedef struct {
  bool ok;
  int code;
  const char *message;
} cmeta_result_t;

/** Return human-readable text for SALTS_*, negative errno, or negative Win32 error codes. */
SALTS_C_API const char *cmeta_strerror(int err);

/** Return structured metadata for an error code. */
SALTS_C_API cmeta_error_info_t cmeta_error_info(int err);

/**
 * Register a custom error domain.
 *
 * The descriptor and entry table must remain alive until process exit or until
 * cmeta_error_unregister_domain() is called. Domain ids must be in
 * [SALTS_ERROR_CUSTOM_DOMAIN_MIN, SALTS_ERROR_CUSTOM_DOMAIN_MAX].
 */
SALTS_C_API int cmeta_error_register_domain(const cmeta_error_domain_desc_t *domain);

/** Remove a previously registered custom domain. */
SALTS_C_API int cmeta_error_unregister_domain(int domain);

static inline cmeta_result_t cmeta_result_ok(void) {
  cmeta_result_t r;
  r.ok = true;
  r.code = SALTS_OK;
  r.message = "success";
  return r;
}

static inline cmeta_result_t cmeta_result_err(int code) {
  cmeta_result_t r;
  r.ok = false;
  r.code = code;
  r.message = cmeta_strerror(code);
  return r;
}

static inline cmeta_result_t cmeta_result_from_code(int code) {
  return code == SALTS_OK ? cmeta_result_ok() : cmeta_result_err(code);
}

static inline bool cmeta_result_is_ok(cmeta_result_t r) { return r.ok; }
static inline bool cmeta_result_is_err(cmeta_result_t r) { return !r.ok; }

#ifdef __cplusplus
}
#endif

#endif /* SALTS_ERROR_H */
