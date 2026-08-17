#ifndef TURBO_ERROR_H
#define TURBO_ERROR_H

#include "platform.h"
#include "enum_utils.h"
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TURBO_OK 0

#define TURBO_ERROR_DOMAIN_ITEMS(X) \
  X(TURBO_ERROR_DOMAIN_NONE, 0, "none") \
  X(TURBO_ERROR_DOMAIN_TURBO, 1, "turbo") \
  X(TURBO_ERROR_DOMAIN_CUSTOM, 2, "custom") \
  X(TURBO_ERROR_DOMAIN_POSIX, 3, "posix") \
  X(TURBO_ERROR_DOMAIN_WIN32, 4, "win32") \
  X(TURBO_ERROR_DOMAIN_UNKNOWN, 5, "unknown")

TURBO_ENUM_DECLARE(turbo_error_domain_t, turbo_error_domain, TURBO_ERROR_DOMAIN_ITEMS, "unknown")

#undef TURBO_ERROR_DOMAIN_ITEMS

#define TURBO_ERROR_CUSTOM_DOMAIN_MIN 1
#define TURBO_ERROR_CUSTOM_DOMAIN_MAX 32767
#define TURBO_ERROR_CUSTOM_LOCAL_MAX 65535
#define TURBO_ERROR_CUSTOM(domain, local)                                                           \
  (-(int)(((((domain) & 0x7fff) << 16) | ((local) & 0xffff))))
#define TURBO_ERROR_CUSTOM_DOMAIN(code) (((-(code)) >> 16) & 0x7fff)
#define TURBO_ERROR_CUSTOM_LOCAL(code) ((-(code)) & 0xffff)

// Mapping turbo errors (EAI + common networking/system errors)
#define TURBO_ERROR_CODE_ITEMS(X)                                                                 \
  X(TURBO_EAI_ADDRFAMILY, -3000, "TURBO_EAI_ADDRFAMILY", "address family for hostname not supported") \
  X(TURBO_EAI_AGAIN, -3001, "TURBO_EAI_AGAIN", "temporary DNS failure")                              \
  X(TURBO_EAI_BADFLAGS, -3002, "TURBO_EAI_BADFLAGS", "bad DNS flags")                                \
  X(TURBO_EAI_FAIL, -3003, "TURBO_EAI_FAIL", "non-recoverable DNS failure")                          \
  X(TURBO_EAI_FAMILY, -3004, "TURBO_EAI_FAMILY", "address family not supported by DNS result")        \
  X(TURBO_EAI_MEMORY, -3005, "TURBO_EAI_MEMORY", "out of memory during DNS lookup")                  \
  X(TURBO_EAI_NODATA, -3006, "TURBO_EAI_NODATA", "no DNS data")                                     \
  X(TURBO_EAI_NONAME, -3007, "TURBO_EAI_NONAME", "hostname not found")                               \
  X(TURBO_EAI_SERVICE, -3008, "TURBO_EAI_SERVICE", "service not available for socket type")           \
  X(TURBO_EAI_SOCKTYPE, -3009, "TURBO_EAI_SOCKTYPE", "socket type not supported")                    \
  X(TURBO_EAI_SYSTEM, -3010, "TURBO_EAI_SYSTEM", "system error during DNS lookup")                   \
  X(TURBO_EAI_CANCELED, -3011, "TURBO_EAI_CANCELED", "DNS lookup canceled")                          \
  X(TURBO_EADDRINUSE, -4000, "TURBO_EADDRINUSE", "address already in use")                           \
  X(TURBO_EADDRNOTAVAIL, -4001, "TURBO_EADDRNOTAVAIL", "address not available")                      \
  X(TURBO_EAFNOSUPPORT, -4002, "TURBO_EAFNOSUPPORT", "address family not supported")                \
  X(TURBO_EALREADY, -4003, "TURBO_EALREADY", "operation already in progress")                        \
  X(TURBO_EBADF, -4004, "TURBO_EBADF", "bad file descriptor")                                      \
  X(TURBO_EBUSY, -4005, "TURBO_EBUSY", "resource busy or locked")                                    \
  X(TURBO_ECANCELED, -4006, "TURBO_ECANCELED", "operation canceled")                                 \
  X(TURBO_ECHARSET, -4007, "TURBO_ECHARSET", "invalid character set")                                \
  X(TURBO_ECONNABORTED, -4008, "TURBO_ECONNABORTED", "connection aborted")                          \
  X(TURBO_ECONNREFUSED, -4009, "TURBO_ECONNREFUSED", "connection refused")                          \
  X(TURBO_ECONNRESET, -4010, "TURBO_ECONNRESET", "connection reset by peer")                        \
  X(TURBO_EDESTADDRREQ, -4011, "TURBO_EDESTADDRREQ", "destination address required")                \
  X(TURBO_EFAULT, -4012, "TURBO_EFAULT", "bad address")                                             \
  X(TURBO_EFBIG, -4013, "TURBO_EFBIG", "file too large")                                            \
  X(TURBO_EHOSTUNREACH, -4014, "TURBO_EHOSTUNREACH", "host is unreachable")                         \
  X(TURBO_EINTR, -4015, "TURBO_EINTR", "operation interrupted")                                     \
  X(TURBO_EINVAL, -4016, "TURBO_EINVAL", "invalid argument")                                        \
  X(TURBO_EIO, -4017, "TURBO_EIO", "I/O error")                                                     \
  X(TURBO_EISCONN, -4018, "TURBO_EISCONN", "socket is already connected")                           \
  X(TURBO_EISDIR, -4019, "TURBO_EISDIR", "is a directory")                                         \
  X(TURBO_ELOOP, -4020, "TURBO_ELOOP", "too many symbolic links")                                  \
  X(TURBO_EMFILE, -4021, "TURBO_EMFILE", "too many open files")                                     \
  X(TURBO_EMSGSIZE, -4022, "TURBO_EMSGSIZE", "message too long")                                    \
  X(TURBO_ENAMETOOLONG, -4023, "TURBO_ENAMETOOLONG", "name too long")                               \
  X(TURBO_ENETDOWN, -4024, "TURBO_ENETDOWN", "network is down")                                     \
  X(TURBO_ENETUNREACH, -4025, "TURBO_ENETUNREACH", "network is unreachable")                        \
  X(TURBO_ENFILE, -4026, "TURBO_ENFILE", "too many open files in system")                           \
  X(TURBO_ENOBUFS, -4027, "TURBO_ENOBUFS", "no buffer space available")                             \
  X(TURBO_ENODEV, -4028, "TURBO_ENODEV", "no such device")                                          \
  X(TURBO_ENOENT, -4029, "TURBO_ENOENT", "no such file or directory")                               \
  X(TURBO_ENOMEM, -4030, "TURBO_ENOMEM", "not enough memory")                                       \
  X(TURBO_ENONET, -4031, "TURBO_ENONET", "machine is not on the network")                           \
  X(TURBO_ENOPROTOOPT, -4032, "TURBO_ENOPROTOOPT", "protocol option not available")                 \
  X(TURBO_ENOSPC, -4033, "TURBO_ENOSPC", "no space left on device")                                 \
  X(TURBO_ENOSYS, -4034, "TURBO_ENOSYS", "function not implemented")                                \
  X(TURBO_ENOTCONN, -4035, "TURBO_ENOTCONN", "socket is not connected")                             \
  X(TURBO_ENOTDIR, -4036, "TURBO_ENOTDIR", "not a directory")                                       \
  X(TURBO_ENOTEMPTY, -4037, "TURBO_ENOTEMPTY", "directory not empty")                               \
  X(TURBO_ENOTSOCK, -4038, "TURBO_ENOTSOCK", "not a socket")                                        \
  X(TURBO_ENOTSUP, -4039, "TURBO_ENOTSUP", "operation not supported")                               \
  X(TURBO_EPERM, -4040, "TURBO_EPERM", "operation not permitted")                                   \
  X(TURBO_EPIPE, -4041, "TURBO_EPIPE", "broken pipe")                                               \
  X(TURBO_EPROTO, -4042, "TURBO_EPROTO", "protocol error")                                          \
  X(TURBO_EPROTONOSUPPORT, -4043, "TURBO_EPROTONOSUPPORT", "protocol not supported")                 \
  X(TURBO_EPROTOTYPE, -4044, "TURBO_EPROTOTYPE", "protocol wrong type for socket")                   \
  X(TURBO_ERANGE, -4045, "TURBO_ERANGE", "result too large")                                        \
  X(TURBO_EROFS, -4046, "TURBO_EROFS", "read-only file system")                                     \
  X(TURBO_ESHUTDOWN, -4047, "TURBO_ESHUTDOWN", "cannot send after transport endpoint shutdown")      \
  X(TURBO_ESPIPE, -4048, "TURBO_ESPIPE", "invalid seek")                                            \
  X(TURBO_ESRCH, -4049, "TURBO_ESRCH", "no such process")                                           \
  X(TURBO_ETIMEDOUT, -4050, "TURBO_ETIMEDOUT", "operation timed out")                               \
  X(TURBO_ETXTBSY, -4051, "TURBO_ETXTBSY", "text file busy")                                        \
  X(TURBO_EXDEV, -4052, "TURBO_EXDEV", "cross-device link")                                         \
  X(TURBO_UNKNOWN, -4053, "TURBO_UNKNOWN", "unknown error")                                          \
  X(TURBO_EOF, -4095, "TURBO_EOF", "end of file")

#define TURBO_ERROR_CODE_ITEM_DECL(name, value, name_text, message_text) name = value,
enum { TURBO_ERROR_CODE_ITEMS(TURBO_ERROR_CODE_ITEM_DECL) };
#undef TURBO_ERROR_CODE_ITEM_DECL

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
CXX_C_API const char *turbo_strerror(int err);

/** Return structured metadata for an error code. */
CXX_C_API turbo_error_info_t turbo_error_info(int err);

/**
 * Register a custom error domain.
 *
 * The descriptor and entry table must remain alive until process exit or until
 * turbo_error_unregister_domain() is called. Domain ids must be in
 * [TURBO_ERROR_CUSTOM_DOMAIN_MIN, TURBO_ERROR_CUSTOM_DOMAIN_MAX].
 */
CXX_C_API int turbo_error_register_domain(const turbo_error_domain_desc_t *domain);

/** Remove a previously registered custom domain. */
CXX_C_API int turbo_error_unregister_domain(int domain);

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
