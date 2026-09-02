/**
 * @file chttp_h2_proto_test.c
 * @brief In-memory protocol engine round-trip tests (client <-> server).
 */

#include "chttp_h2_proto.h"
#include "tinytest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct e2e_s {
  chttp_h2_proto *client;
  chttp_h2_proto *server;
  /* request captured by the server */
  char method[16];
  char path[64];
  char authority[64];
  int req_headers;
  int req_end;
  int32_t req_stream;
  /* response captured by the client */
  char status[8];
  char body[64];
  size_t body_len;
  int resp_headers;
  int resp_closed;
  int client_recv_error;
  int server_recv_error;
  uint32_t goaway_error;
} e2e_t;

static int srv_begin(void *ud, int32_t stream_id) {
  e2e_t *e = (e2e_t *)ud;
  e->req_headers = 1;
  e->req_stream = stream_id;
  e->method[0] = e->path[0] = e->authority[0] = '\0';
  return 0;
}

static int srv_hdr(void *ud, int32_t stream_id, const char *name, size_t nl, const char *value,
                   size_t vl) {
  e2e_t *e = (e2e_t *)ud;
  (void)stream_id;
  if (nl == 7 && memcmp(name, ":method", 7) == 0 && vl < sizeof(e->method)) {
    memcpy(e->method, value, vl);
    e->method[vl] = '\0';
  } else if (nl == 5 && memcmp(name, ":path", 5) == 0 && vl < sizeof(e->path)) {
    memcpy(e->path, value, vl);
    e->path[vl] = '\0';
  } else if (nl == 10 && memcmp(name, ":authority", 10) == 0 && vl < sizeof(e->authority)) {
    memcpy(e->authority, value, vl);
    e->authority[vl] = '\0';
  }
  return 0;
}

static int srv_end(void *ud, int32_t stream_id) {
  e2e_t *e = (e2e_t *)ud;
  static const char *status = ":status";
  static const char *cl = "content-length";
  static const char *body = "hello from server";
  chttp_h2_hpack_header hdrs[2];
  hdrs[0].name = status;
  hdrs[0].name_size = 7;
  hdrs[0].value = "200";
  hdrs[0].value_size = 3;
  hdrs[1].name = cl;
  hdrs[1].name_size = 14;
  hdrs[1].value = "17";
  hdrs[1].value_size = 2;
  e->req_end = 1;
  (void)chttp_h2_proto_submit_response(e->server, stream_id, hdrs, 2, (const uint8_t *)body,
                                       strlen(body));
  return 0;
}

static int cli_begin(void *ud, int32_t stream_id) {
  e2e_t *e = (e2e_t *)ud;
  (void)stream_id;
  e->resp_headers = 1;
  e->status[0] = '\0';
  e->body_len = 0;
  return 0;
}

static int cli_hdr(void *ud, int32_t stream_id, const char *name, size_t nl, const char *value,
                   size_t vl) {
  e2e_t *e = (e2e_t *)ud;
  (void)stream_id;
  if (nl == 7 && memcmp(name, ":status", 7) == 0 && vl < sizeof(e->status)) {
    memcpy(e->status, value, vl);
    e->status[vl] = '\0';
  }
  return 0;
}

static int cli_data(void *ud, int32_t stream_id, const uint8_t *data, size_t len) {
  e2e_t *e = (e2e_t *)ud;
  (void)stream_id;
  if (e->body_len + len <= sizeof(e->body)) {
    memcpy(e->body + e->body_len, data, len);
    e->body_len += len;
  }
  return 0;
}

static int cli_close(void *ud, int32_t stream_id, uint32_t error_code) {
  e2e_t *e = (e2e_t *)ud;
  (void)stream_id;
  (void)error_code;
  e->resp_closed = 1;
  return 0;
}

static void capture_goaway(void *ud, uint32_t last_stream_id, uint32_t error_code) {
  e2e_t *e = (e2e_t *)ud;
  (void)last_stream_id;
  e->goaway_error = error_code;
}

static void pump(e2e_t *e) {
  int iter;
  for (iter = 0; iter < 200; iter++) {
    int progress = 0;
    while (chttp_h2_proto_want_write(e->client)) {
      const uint8_t *wire = NULL;
      ptrdiff_t n = chttp_h2_proto_send(e->client, &wire);
      if (n <= 0) break;
      if (chttp_h2_proto_recv(e->server, wire, (size_t)n) < 0) {
        e->server_recv_error = 1;
      }
      progress = 1;
    }
    while (chttp_h2_proto_want_write(e->server)) {
      const uint8_t *wire = NULL;
      ptrdiff_t n = chttp_h2_proto_send(e->server, &wire);
      if (n <= 0) break;
      if (chttp_h2_proto_recv(e->client, wire, (size_t)n) < 0) {
        e->client_recv_error = 1;
      }
      progress = 1;
    }
    if (!progress) break;
  }
}

static void e2e_init(e2e_t *e) {
  chttp_h2_proto_callbacks scbs, ccbs;
  memset(e, 0, sizeof(*e));
  memset(&scbs, 0, sizeof(scbs));
  memset(&ccbs, 0, sizeof(ccbs));
  scbs.user_data = e;
  scbs.on_begin_headers = srv_begin;
  scbs.on_header = srv_hdr;
  scbs.on_end_headers = srv_end;
  scbs.on_goaway = capture_goaway;
  ccbs.user_data = e;
  ccbs.on_begin_headers = cli_begin;
  ccbs.on_header = cli_hdr;
  ccbs.on_data = cli_data;
  ccbs.on_stream_close = cli_close;
  ccbs.on_goaway = capture_goaway;
  e->server = chttp_h2_proto_create(CHTTP_H2_PROTO_SERVER, NULL, &scbs);
  e->client = chttp_h2_proto_create(CHTTP_H2_PROTO_CLIENT, NULL, &ccbs);
}

spec("CHTTP HTTP/2 protocol engine") {
  it("round-trips a GET request and 200 response through HPACK and DATA") {
    e2e_t e;
    chttp_h2_hpack_header req[4];
    static const char *m = ":method", *s = ":scheme", *pa = ":path", *a = ":authority";
    int32_t sid = 0;
    e2e_init(&e);
    check_not_null(e.client);
    check_not_null(e.server);

    req[0].name = m;
    req[0].name_size = 7;
    req[0].value = "GET";
    req[0].value_size = 3;
    req[1].name = s;
    req[1].name_size = 7;
    req[1].value = "http";
    req[1].value_size = 4;
    req[2].name = pa;
    req[2].name_size = 5;
    req[2].value = "/index";
    req[2].value_size = 6;
    req[3].name = a;
    req[3].name_size = 10;
    req[3].value = "localhost";
    req[3].value_size = 9;

    check_equal(chttp_h2_proto_submit_request(e.client, req, 4, NULL, 0, &sid), 0);
    check(sid == 1);
    pump(&e);

    check(e.req_headers);
    check(e.req_end);
    check_equal(e.method, "GET");
    check_equal(e.path, "/index");
    check_equal(e.authority, "localhost");
    check_equal(e.goaway_error, 0);
    check_equal(e.client_recv_error, 0);
    check_equal(e.server_recv_error, 0);
    check(e.resp_headers);
    check_equal(e.status, "200");
    check_equal(e.body_len, 17);
    check(memcmp(e.body, "hello from server", 17) == 0);
    check(e.resp_closed);

    chttp_h2_proto_destroy(e.client);
    chttp_h2_proto_destroy(e.server);
  }

  it("streams a request body with flow control") {
    e2e_t e;
    chttp_h2_hpack_header req[4];
    static const char *m = ":method", *s = ":scheme", *pa = ":path", *a = ":authority";
    static const char body[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    int32_t sid = 0;
    e2e_init(&e);
    check_not_null(e.client);
    check_not_null(e.server);

    req[0].name = m;
    req[0].name_size = 7;
    req[0].value = "POST";
    req[0].value_size = 4;
    req[1].name = s;
    req[1].name_size = 7;
    req[1].value = "http";
    req[1].value_size = 4;
    req[2].name = pa;
    req[2].name_size = 5;
    req[2].value = "/upload";
    req[2].value_size = 7;
    req[3].name = a;
    req[3].name_size = 10;
    req[3].value = "localhost";
    req[3].value_size = 9;

    check_equal(chttp_h2_proto_submit_request(e.client, req, 4, (const uint8_t *)body,
                                              sizeof(body) - 1, &sid),
                0);
    pump(&e);
    /* The server saw the request; the response round-trips. */
    check_equal(e.method, "POST");
    check_equal(e.goaway_error, 0);
    check_equal(e.client_recv_error, 0);
    check_equal(e.server_recv_error, 0);
    check(e.req_end);
    check(e.resp_closed);

    chttp_h2_proto_destroy(e.client);
    chttp_h2_proto_destroy(e.server);
  }
}
