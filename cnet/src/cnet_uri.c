#include "cnet_uri.h"

#include <uri_parser.h>

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int cnet_uri_bounded_length(const char *text, size_t *out_length) {
  size_t length;
  if (text == NULL || out_length == NULL) return SALTS_EINVAL;
  for (length = 0u; length <= CNET_URI_MAX_BYTES; ++length) {
    if (text[length] == '\0') {
      *out_length = length;
      return SALTS_OK;
    }
  }
  return SALTS_ERANGE;
}

static cnet_uri_scheme cnet_uri_scheme_from_text(const char *scheme) {
  if (strcmp(scheme, "tcp") == 0) return CNET_URI_TCP;
  if (strcmp(scheme, "tls") == 0) return CNET_URI_TLS;
  if (strcmp(scheme, "udp") == 0) return CNET_URI_UDP;
  return CNET_URI_NONE;
}

static int cnet_uri_parse_network(const uri_t *generic, cnet_uri_scheme scheme, cnet_uri *out_uri) {
  const size_t host_length = strlen(generic->host);

  if (host_length == 0u || (generic->component_flags & URI_COMPONENT_USERINFO) ||
      generic->path[0] != '\0' || (generic->component_flags & URI_COMPONENT_QUERY) ||
      (generic->component_flags & URI_COMPONENT_FRAGMENT))
    return SALTS_EINVAL;
  if (!(generic->component_flags & URI_COMPONENT_PORT)) return SALTS_EINVAL;
  if ((generic->overflow_flags & URI_OVERFLOW_PORT) || generic->port <= 0 ||
      generic->port > UINT16_MAX)
    return SALTS_ERANGE;
  if (host_length >= CNET_URI_HOST_CAPACITY) return SALTS_ERANGE;

  out_uri->scheme = scheme;
  out_uri->port = (uint16_t)generic->port;
  memcpy(out_uri->host, generic->host, host_length + 1u);
  return SALTS_OK;
}

static bool cnet_uri_pipe_name_valid(const char *name, size_t length) {
  size_t index;
  if (name == NULL || length == 0u) return false;
  for (index = 0u; index < length; ++index) {
    const unsigned char value = (unsigned char)name[index];
    if (value <= 0x20u || value == 0x7fu || value == '?' || value == '#' || value == '@')
      return false;
  }
  return true;
}

static int cnet_uri_parse_pipe(const char *name, size_t name_length, cnet_uri *out_uri) {
  if (!cnet_uri_pipe_name_valid(name, name_length)) return SALTS_EINVAL;
  if (name_length >= CNET_URI_PATH_CAPACITY) return SALTS_ERANGE;
  out_uri->scheme = CNET_URI_PIPE;
  memcpy(out_uri->path, name, name_length);
  out_uri->path[name_length] = '\0';
  return SALTS_OK;
}

int cnet_uri_parse(const char *text, cnet_uri *out_uri) {
  static const char pipe_prefix[] = "pipe://";
  cnet_uri parsed = {0};
  uri_t generic;
  cnet_uri_scheme scheme;
  size_t length = 0u;
  int status;

  if (out_uri == NULL) return SALTS_EINVAL;
  *out_uri = (cnet_uri){0};
  status = cnet_uri_bounded_length(text, &length);
  if (status != SALTS_OK) return status;
  if (length >= sizeof(pipe_prefix) - 1u &&
      memcmp(text, pipe_prefix, sizeof(pipe_prefix) - 1u) == 0) {
    status = cnet_uri_parse_pipe(text + sizeof(pipe_prefix) - 1u,
                                 length - (sizeof(pipe_prefix) - 1u), &parsed);
    if (status == SALTS_OK) *out_uri = parsed;
    return status;
  }
  if (length == 0u) return SALTS_EINVAL;
  if (!uri_parse(text, &generic) || !generic.valid)
    return (generic.overflow_flags & URI_OVERFLOW_COMPONENT) ? SALTS_ERANGE : SALTS_EINVAL;

  scheme = cnet_uri_scheme_from_text(generic.scheme);
  if (scheme == CNET_URI_NONE) return SALTS_ENOTSUP;
  status = cnet_uri_parse_network(&generic, scheme, &parsed);
  if (status == SALTS_OK) *out_uri = parsed;
  return status;
}
