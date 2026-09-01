#ifndef CHTTP_SERVER_INTERNAL_H
#define CHTTP_SERVER_INTERNAL_H

#include <chttp/chttp.h>

#include <stddef.h>

typedef struct chttp_server_parser {
  void *impl;
} chttp_server_parser;

typedef int (*chttp_server_parser_request_fn)(void *user, const chttp_server_request_view *request);
typedef int (*chttp_server_parser_continue_fn)(void *user);

typedef struct chttp_server_parser_config {
  size_t max_target_bytes;
  size_t max_header_count;
  size_t max_header_bytes;
  size_t max_body_bytes;
  chttp_server_parser_request_fn on_request;
  chttp_server_parser_continue_fn on_continue;
  void *user;
} chttp_server_parser_config;

int chttp_server_parser_init(chttp_server_parser *parser, const chttp_server_parser_config *config);
int chttp_server_parser_execute(chttp_server_parser *parser, const void *data, size_t size,
                                unsigned int *out_http_status);
int chttp_server_parser_reset(chttp_server_parser *parser);
void chttp_server_parser_destroy(chttp_server_parser *parser);

#endif /* CHTTP_SERVER_INTERNAL_H */
