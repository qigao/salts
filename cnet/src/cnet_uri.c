#include "cnet_uri.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int cnet_uri_bounded_length(const char *text, size_t *out_length) {
  size_t length;
  if (text == NULL || out_length == NULL) return TURBO_EINVAL;
  for (length = 0u; length <= CNET_URI_MAX_BYTES; ++length) {
    if (text[length] == '\0') {
      *out_length = length;
      return TURBO_OK;
    }
  }
  return TURBO_ERANGE;
}

static bool cnet_uri_component_valid(const char *text, size_t length, bool host) {
  size_t index;
  if (text == NULL || length == 0u) return false;
  for (index = 0u; index < length; ++index) {
    const unsigned char value = (unsigned char)text[index];
    if (value <= 0x20u || value == 0x7fu || value == '?' || value == '#' || value == '@' ||
        (host && value == '/'))
      return false;
    if (host && (value == '[' || value == ']')) return false;
  }
  return true;
}

static int cnet_uri_parse_port(const char *text, size_t length, uint16_t *out_port) {
  uint32_t value = 0u;
  size_t index;
  if (text == NULL || out_port == NULL || length == 0u) return TURBO_EINVAL;
  for (index = 0u; index < length; ++index) {
    const unsigned char digit = (unsigned char)text[index];
    if (digit < '0' || digit > '9') return TURBO_EINVAL;
    value = value * 10u + (uint32_t)(digit - '0');
    if (value > UINT16_MAX) return TURBO_ERANGE;
  }
  if (value == 0u) return TURBO_ERANGE;
  *out_port = (uint16_t)value;
  return TURBO_OK;
}

static int cnet_uri_parse_network(const char *authority, size_t length, cnet_uri_scheme scheme,
                                  cnet_uri *out_uri) {
  const char *host = authority;
  const char *port;
  size_t host_length;
  size_t port_length;
  int status;

  if (length == 0u) return TURBO_EINVAL;
  if (authority[0] == '[') {
    const char *closing = (const char *)memchr(authority + 1u, ']', length - 1u);
    if (closing == NULL || closing == authority + 1u ||
        (size_t)(closing - authority) + 1u >= length || closing[1] != ':')
      return TURBO_EINVAL;
    host = authority + 1u;
    host_length = (size_t)(closing - host);
    port = closing + 2u;
  } else {
    const char *separator = (const char *)memchr(authority, ':', length);
    if (separator == NULL || separator == authority) return TURBO_EINVAL;
    host_length = (size_t)(separator - authority);
    port = separator + 1u;
  }
  port_length = length - (size_t)(port - authority);
  if (host_length >= CNET_URI_HOST_CAPACITY || !cnet_uri_component_valid(host, host_length, true))
    return host_length >= CNET_URI_HOST_CAPACITY ? TURBO_ERANGE : TURBO_EINVAL;
  status = cnet_uri_parse_port(port, port_length, &out_uri->port);
  if (status != TURBO_OK) return status;
  memcpy(out_uri->host, host, host_length);
  out_uri->host[host_length] = '\0';
  out_uri->scheme = scheme;
  return TURBO_OK;
}

int cnet_uri_parse(const char *text, cnet_uri *out_uri) {
  static const char tcp_prefix[] = "tcp://";
  static const char udp_prefix[] = "udp://";
  static const char pipe_prefix[] = "pipe://";
  cnet_uri parsed = {0};
  size_t length = 0u;
  int status;

  if (out_uri == NULL) return TURBO_EINVAL;
  *out_uri = (cnet_uri){0};
  status = cnet_uri_bounded_length(text, &length);
  if (status != TURBO_OK) return status;
  if (length >= sizeof(tcp_prefix) - 1u && memcmp(text, tcp_prefix, sizeof(tcp_prefix) - 1u) == 0)
    status = cnet_uri_parse_network(text + sizeof(tcp_prefix) - 1u,
                                    length - (sizeof(tcp_prefix) - 1u), CNET_URI_TCP, &parsed);
  else if (length >= sizeof(udp_prefix) - 1u &&
           memcmp(text, udp_prefix, sizeof(udp_prefix) - 1u) == 0)
    status = cnet_uri_parse_network(text + sizeof(udp_prefix) - 1u,
                                    length - (sizeof(udp_prefix) - 1u), CNET_URI_UDP, &parsed);
  else if (length >= sizeof(pipe_prefix) - 1u &&
           memcmp(text, pipe_prefix, sizeof(pipe_prefix) - 1u) == 0) {
    const char *path = text + sizeof(pipe_prefix) - 1u;
    const size_t path_length = length - (sizeof(pipe_prefix) - 1u);
    if (path_length == 0u || !cnet_uri_component_valid(path, path_length, false))
      status = TURBO_EINVAL;
    else if (path_length >= CNET_URI_PATH_CAPACITY) status = TURBO_ERANGE;
    else {
      parsed.scheme = CNET_URI_PIPE;
      memcpy(parsed.path, path, path_length);
      parsed.path[path_length] = '\0';
      status = TURBO_OK;
    }
  } else {
    status = TURBO_ENOTSUP;
  }
  if (status == TURBO_OK) *out_uri = parsed;
  return status;
}
