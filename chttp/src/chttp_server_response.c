#include "chttp_server_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int chttp_server_response_ascii_equal(const char *left, const char *right) {
  unsigned char a;
  unsigned char b;
  if (left == NULL || right == NULL) return 0;
  do {
    a = (unsigned char)*left++;
    b = (unsigned char)*right++;
    if (a >= (unsigned char)'A' && a <= (unsigned char)'Z') a += 32u;
    if (b >= (unsigned char)'A' && b <= (unsigned char)'Z') b += 32u;
    if (a != b) return 0;
  } while (a != 0u);
  return 1;
}

static bool chttp_server_response_token(const char *value) {
  const unsigned char *cursor = (const unsigned char *)value;
  if (cursor == NULL || *cursor == 0u) return false;
  for (; *cursor != 0u; ++cursor) {
    const unsigned char ch = *cursor;
    if ((ch >= (unsigned char)'0' && ch <= (unsigned char)'9') ||
        (ch >= (unsigned char)'A' && ch <= (unsigned char)'Z') ||
        (ch >= (unsigned char)'a' && ch <= (unsigned char)'z'))
      continue;
    if (strchr("!#$%&'*+-.^_`|~", (int)ch) == NULL) return false;
  }
  return true;
}

static bool chttp_server_response_value(const char *value) {
  const unsigned char *cursor = (const unsigned char *)value;
  if (cursor == NULL) return false;
  for (; *cursor != 0u; ++cursor) {
    if ((*cursor < 32u && *cursor != (unsigned char)'\t') || *cursor == 127u) return false;
  }
  return true;
}

static int chttp_server_response_storage_copy(chttp_server_response_builder *builder,
                                              const char *value, char **out_value) {
  const size_t size = strlen(value) + 1u;
  if (builder->header_storage_used > builder->header_storage_capacity ||
      size > builder->header_storage_capacity - builder->header_storage_used)
    return TURBO_ENOBUFS;
  memcpy(builder->header_storage + builder->header_storage_used, value, size);
  *out_value = builder->header_storage + builder->header_storage_used;
  builder->header_storage_used += size;
  return TURBO_OK;
}

int chttp_server_response_builder_init(chttp_server_response_builder *builder,
                                       const chttp_server_config *config) {
  if (builder == NULL || config == NULL || config->max_response_header_count == 0u ||
      config->max_response_header_bytes == 0u || config->max_response_body_bytes == 0u)
    return TURBO_EINVAL;
  if (config->max_response_header_count > SIZE_MAX / sizeof(*builder->headers)) return TURBO_ERANGE;
  builder->headers =
      (chttp_header *)calloc(config->max_response_header_count, sizeof(*builder->headers));
  builder->header_storage = (char *)malloc(config->max_response_header_bytes);
  builder->body = (unsigned char *)malloc(config->max_response_body_bytes);
  if (builder->headers == NULL || builder->header_storage == NULL || builder->body == NULL) {
    chttp_server_response_builder_destroy(builder);
    return TURBO_ENOMEM;
  }
  builder->header_capacity = config->max_response_header_count;
  builder->header_storage_capacity = config->max_response_header_bytes;
  builder->body_capacity = config->max_response_body_bytes;
  chttp_server_response_builder_reset(builder);
  return TURBO_OK;
}

void chttp_server_response_builder_reset(chttp_server_response_builder *builder) {
  if (builder == NULL) return;
  builder->header_count = 0u;
  builder->header_storage_used = 0u;
  builder->header_wire_bytes = 0u;
  builder->body_size = 0u;
  builder->status_code = 0u;
  builder->replied = false;
}

void chttp_server_response_builder_destroy(chttp_server_response_builder *builder) {
  if (builder == NULL) return;
  free(builder->body);
  free(builder->header_storage);
  free(builder->headers);
  *builder = (chttp_server_response_builder){0};
}

int chttp_server_response_set_header(chttp_server_response *response, const char *name,
                                     const char *value) {
  chttp_server_response_builder *builder;
  size_t name_size;
  size_t value_size;
  size_t wire_size;
  size_t index;
  char *name_copy = NULL;
  char *value_copy = NULL;
  int status;
  if (response == NULL || response->impl == NULL || !chttp_server_response_token(name) ||
      !chttp_server_response_value(value))
    return TURBO_EINVAL;
  if (chttp_server_response_ascii_equal(name, "Content-Length") ||
      chttp_server_response_ascii_equal(name, "Connection") ||
      chttp_server_response_ascii_equal(name, "Transfer-Encoding"))
    return TURBO_EPERM;
  builder = (chttp_server_response_builder *)response->impl;
  name_size = strlen(name);
  value_size = strlen(value);
  if (name_size > SIZE_MAX - value_size - 4u) return TURBO_ERANGE;
  wire_size = name_size + value_size + 4u;
  for (index = 0u; index < builder->header_count; ++index) {
    if (chttp_server_response_ascii_equal(builder->headers[index].name, name)) {
      const size_t previous =
          strlen(builder->headers[index].name) + strlen(builder->headers[index].value) + 4u;
      if (builder->header_wire_bytes - previous > builder->header_storage_capacity ||
          wire_size > builder->header_storage_capacity - (builder->header_wire_bytes - previous))
        return TURBO_ENOBUFS;
      status = chttp_server_response_storage_copy(builder, value, &value_copy);
      if (status != TURBO_OK) return status;
      builder->headers[index].value = value_copy;
      builder->header_wire_bytes = builder->header_wire_bytes - previous + wire_size;
      return TURBO_OK;
    }
  }
  if (builder->header_count >= builder->header_capacity ||
      builder->header_wire_bytes > builder->header_storage_capacity ||
      wire_size > builder->header_storage_capacity - builder->header_wire_bytes)
    return TURBO_ENOBUFS;
  if (name_size > SIZE_MAX - value_size - 2u ||
      builder->header_storage_used > builder->header_storage_capacity ||
      name_size + value_size + 2u > builder->header_storage_capacity - builder->header_storage_used)
    return TURBO_ENOBUFS;
  status = chttp_server_response_storage_copy(builder, name, &name_copy);
  if (status != TURBO_OK) return status;
  status = chttp_server_response_storage_copy(builder, value, &value_copy);
  if (status != TURBO_OK) return status;
  builder->headers[builder->header_count++] = (chttp_header){name_copy, value_copy};
  builder->header_wire_bytes += wire_size;
  return TURBO_OK;
}

int chttp_server_reply(chttp_server_response *response, unsigned int status_code,
                       const char *content_type, const void *body, size_t body_size) {
  chttp_server_response_builder *builder;
  int status;
  if (response == NULL || response->impl == NULL || status_code < 200u || status_code > 599u ||
      (body_size != 0u && body == NULL) || (content_type != NULL && content_type[0] == '\0'))
    return TURBO_EINVAL;
  if ((status_code == 204u || status_code == 205u || status_code == 304u) && body_size != 0u)
    return TURBO_EINVAL;
  builder = (chttp_server_response_builder *)response->impl;
  if (builder->replied) return TURBO_EALREADY;
  if (body_size > builder->body_capacity) return TURBO_EMSGSIZE;
  if (content_type != NULL) {
    status = chttp_server_response_set_header(response, "Content-Type", content_type);
    if (status != TURBO_OK) return status;
  }
  if (body_size != 0u) memcpy(builder->body, body, body_size);
  builder->body_size = body_size;
  builder->status_code = status_code;
  builder->replied = true;
  return TURBO_OK;
}

static const char *chttp_server_reason(unsigned int status_code) {
  switch (status_code) {
  case 100u:
    return "Continue";
  case 200u:
    return "OK";
  case 201u:
    return "Created";
  case 202u:
    return "Accepted";
  case 204u:
    return "No Content";
  case 205u:
    return "Reset Content";
  case 304u:
    return "Not Modified";
  case 400u:
    return "Bad Request";
  case 404u:
    return "Not Found";
  case 405u:
    return "Method Not Allowed";
  case 413u:
    return "Content Too Large";
  case 414u:
    return "URI Too Long";
  case 417u:
    return "Expectation Failed";
  case 426u:
    return "Upgrade Required";
  case 431u:
    return "Request Header Fields Too Large";
  case 500u:
    return "Internal Server Error";
  case 501u:
    return "Not Implemented";
  case 505u:
    return "HTTP Version Not Supported";
  default:
    return "Unknown";
  }
}

static int chttp_server_output_append(unsigned char *output, size_t capacity, size_t *size,
                                      const void *data, size_t data_size) {
  if (*size > capacity || data_size > capacity - *size) return TURBO_EMSGSIZE;
  if (data_size != 0u) memcpy(output + *size, data, data_size);
  *size += data_size;
  return TURBO_OK;
}

int chttp_server_response_serialize(const chttp_server_response_builder *builder,
                                    const chttp_server_request_view *request, unsigned char *output,
                                    size_t output_capacity, size_t *inout_size) {
  char line[128];
  size_t initial_size;
  size_t index;
  size_t body_size;
  int line_size;
  int status = TURBO_OK;
  if (builder == NULL || request == NULL || output == NULL || inout_size == NULL ||
      !builder->replied)
    return TURBO_EINVAL;
  initial_size = *inout_size;
  body_size = request->method == CHTTP_METHOD_HEAD ? 0u : builder->body_size;
  line_size =
      snprintf(line, sizeof(line), "HTTP/%u.%u %u %s\r\n", request->http_major, request->http_minor,
               builder->status_code, chttp_server_reason(builder->status_code));
  if (line_size < 0 || (size_t)line_size >= sizeof(line)) status = TURBO_EMSGSIZE;
  if (status == TURBO_OK)
    status =
        chttp_server_output_append(output, output_capacity, inout_size, line, (size_t)line_size);
  for (index = 0u; status == TURBO_OK && index < builder->header_count; ++index) {
    const chttp_header *header = &builder->headers[index];
    status = chttp_server_output_append(output, output_capacity, inout_size, header->name,
                                        strlen(header->name));
    if (status == TURBO_OK)
      status = chttp_server_output_append(output, output_capacity, inout_size, ": ", 2u);
    if (status == TURBO_OK)
      status = chttp_server_output_append(output, output_capacity, inout_size, header->value,
                                          strlen(header->value));
    if (status == TURBO_OK)
      status = chttp_server_output_append(output, output_capacity, inout_size, "\r\n", 2u);
  }
  if (status == TURBO_OK) {
    if (builder->status_code == 204u || builder->status_code == 304u)
      line_size = snprintf(line, sizeof(line), "Connection: %s\r\n\r\n",
                           request->protocol_keep_alive ? "keep-alive" : "close");
    else
      line_size =
          snprintf(line, sizeof(line), "Content-Length: %zu\r\nConnection: %s\r\n\r\n",
                   builder->body_size, request->protocol_keep_alive ? "keep-alive" : "close");
    if (line_size < 0 || (size_t)line_size >= sizeof(line)) status = TURBO_EMSGSIZE;
  }
  if (status == TURBO_OK)
    status =
        chttp_server_output_append(output, output_capacity, inout_size, line, (size_t)line_size);
  if (status == TURBO_OK)
    status =
        chttp_server_output_append(output, output_capacity, inout_size, builder->body, body_size);
  if (status != TURBO_OK) *inout_size = initial_size;
  return status;
}

int chttp_server_error_serialize(unsigned int status_code, unsigned char *output,
                                 size_t output_capacity, size_t *inout_size) {
  char wire[256];
  const char *reason = chttp_server_reason(status_code);
  int wire_size;
  if (output == NULL || inout_size == NULL || status_code < 400u || status_code > 599u)
    return TURBO_EINVAL;
  wire_size = snprintf(wire, sizeof(wire),
                       "HTTP/1.1 %u %s\r\nContent-Type: text/plain\r\nContent-Length: %zu\r\n"
                       "Connection: close\r\n\r\n%s",
                       status_code, reason, strlen(reason), reason);
  if (wire_size < 0 || (size_t)wire_size >= sizeof(wire)) return TURBO_EMSGSIZE;
  return chttp_server_output_append(output, output_capacity, inout_size, wire, (size_t)wire_size);
}
