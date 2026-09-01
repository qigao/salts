#include "crpc_internal.h"
#include "tinytest.h"

#include <string.h>

static cserde_status crpc_test_write(cserde_writer *writer, cserde_token token) {
  return cserde_writer_write(writer, &token);
}

static cserde_status crpc_test_encode_array(void *user, cserde_writer *writer) {
  static const unsigned char name[] = "Ada";
  cserde_status status;
  (void)user;
  status = crpc_test_write(writer, (cserde_token){.kind = CSERDE_ARRAY_BEGIN});
  if (status == CSERDE_OK)
    status =
        crpc_test_write(writer, (cserde_token){.kind = CSERDE_UINT, .value.uint = UINT64_C(7)});
  if (status == CSERDE_OK)
    status = crpc_test_write(
        writer, (cserde_token){.kind = CSERDE_STRING,
                               .value.slice = {name, sizeof(name) - 1u, CSERDE_VIEW_STABLE}});
  if (status == CSERDE_OK)
    status = crpc_test_write(writer, (cserde_token){.kind = CSERDE_ARRAY_END});
  return status;
}

static cserde_status crpc_test_encode_scalar(void *user, cserde_writer *writer) {
  (void)user;
  return crpc_test_write(writer, (cserde_token){.kind = CSERDE_UINT, .value.uint = UINT64_C(1)});
}

static cserde_status crpc_test_encode_nested(void *user, cserde_writer *writer) {
  cserde_status status;
  (void)user;
  status = crpc_test_write(writer, (cserde_token){.kind = CSERDE_ARRAY_BEGIN});
  if (status == CSERDE_OK)
    status = crpc_test_write(writer, (cserde_token){.kind = CSERDE_ARRAY_BEGIN});
  return status;
}

spec("CRPC JSON-RPC codec") {
  it("encodes a bounded CSerde params array and explicit id") {
    const crpc_method method = {.service = "users", .name = "find\"one"};
    static const char expected[] = "{\"jsonrpc\":\"2.0\",\"method\":\"users.find\\\"one\","
                                   "\"params\":[7,\"Ada\"],\"id\":42}";
    crpc_encoded_request encoded = {0};

    check_equal(crpc_json_encode_request(&method, UINT64_C(42), crpc_test_encode_array, NULL, 64u,
                                         8u, 256u, &encoded),
                TURBO_OK);
    check_equal(encoded.size, sizeof(expected) - 1u);
    check_equal(encoded.data, expected, sizeof(expected) - 1u);
    crpc_encoded_request_destroy(&encoded);
    check_null(encoded.data);
    check_equal(encoded.size, (size_t)0u);
  }

  it("omits params when no encoder is supplied") {
    const crpc_method method = {.name = "health"};
    static const char expected[] = "{\"jsonrpc\":\"2.0\",\"method\":\"health\",\"id\":0}";
    crpc_encoded_request encoded = {0};

    check_equal(crpc_json_encode_request(&method, UINT64_C(0), NULL, NULL, 32u, 4u, 128u, &encoded),
                TURBO_OK);
    check_equal(encoded.data, expected, sizeof(expected) - 1u);
    crpc_encoded_request_destroy(&encoded);
  }

  it("rejects scalar params, excessive depth, reserved methods, and invalid UTF-8") {
    const unsigned char invalid_utf8[] = {0xc0u, 0xafu, 0u};
    crpc_method method = {.name = "call"};
    crpc_encoded_request encoded = {0};

    check_equal(crpc_json_encode_request(&method, UINT64_C(1), crpc_test_encode_scalar, NULL, 32u,
                                         4u, 128u, &encoded),
                TURBO_EINVAL);
    check_null(encoded.data);
    check_equal(crpc_json_encode_request(&method, UINT64_C(1), crpc_test_encode_nested, NULL, 32u,
                                         2u, 128u, &encoded),
                TURBO_EMSGSIZE);
    method.name = "rpc.cancel";
    check_equal(crpc_json_encode_request(&method, UINT64_C(1), NULL, NULL, 32u, 4u, 128u, &encoded),
                TURBO_EPERM);
    method.name = (const char *)invalid_utf8;
    check_equal(crpc_json_encode_request(&method, UINT64_C(1), NULL, NULL, 32u, 4u, 128u, &encoded),
                TURBO_EINVAL);
  }

  it("enforces the complete request body bound") {
    const crpc_method method = {.service = "users", .name = "find"};
    crpc_encoded_request encoded = {0};

    check_equal(crpc_json_encode_request(&method, UINT64_C(1), crpc_test_encode_array, NULL, 64u,
                                         8u, 24u, &encoded),
                TURBO_EMSGSIZE);
    check_null(encoded.data);
  }

  it("decodes a result as a callback-scoped CSerde reader") {
    static const char json[] = "{\"jsonrpc\":\"2.0\",\"result\":{\"name\":\"Ada\"},"
                               "\"id\":18446744073709551615}";
    crpc_decoded_response decoded = {0};
    cserde_token token = {0};
    const char *stage = NULL;

    check_equal(crpc_json_decode_response(json, sizeof(json) - 1u, UINT64_MAX, 200u, 8u, NULL,
                                          &decoded, &stage),
                TURBO_OK);
    check_equal(decoded.response.request_id, UINT64_MAX);
    check_equal(decoded.response.http_status, 200u);
    check_equal(decoded.response.kind, CRPC_RESPONSE_RESULT);
    check_null(decoded.response.callable);
    check_equal(cserde_reader_next(decoded.response.value.result, &token), CSERDE_OK);
    check_equal(token.kind, CSERDE_MAP_BEGIN);
    check_equal(cserde_reader_next(decoded.response.value.result, &token), CSERDE_OK);
    check_equal(token.kind, CSERDE_STRING);
    check_equal(token.value.slice.data, (const unsigned char *)"name", 4u);
    check_equal(cserde_reader_next(decoded.response.value.result, &token), CSERDE_OK);
    check_equal(token.kind, CSERDE_STRING);
    check_equal(token.value.slice.data, (const unsigned char *)"Ada", 3u);
    check_equal(cserde_reader_next(decoded.response.value.result, &token), CSERDE_OK);
    check_equal(token.kind, CSERDE_MAP_END);
    check_equal(cserde_reader_next(decoded.response.value.result, &token), CSERDE_DONE);
    crpc_decoded_response_destroy(&decoded);
  }

  it("accepts an equivalent integral exponent id") {
    static const char json[] = "{\"jsonrpc\":\"2.0\",\"result\":true,\"id\":4.2e1}";
    crpc_decoded_response decoded = {0};
    const char *stage = NULL;

    check_equal(crpc_json_decode_response(json, sizeof(json) - 1u, UINT64_C(42), 200u, 4u, NULL,
                                          &decoded, &stage),
                TURBO_OK);
    check_equal(decoded.response.request_id, UINT64_C(42));
    crpc_decoded_response_destroy(&decoded);
  }

  it("preserves exact integers at the uint64 and int64 boundaries") {
    static const char result_json[] = "{\"jsonrpc\":\"2.0\",\"result\":true,"
                                      "\"id\":1.8446744073709551615e19}";
    static const char error_json[] = "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":"
                                     "-9.223372036854775808e18,\"message\":\"minimum\"},\"id\":1}";
    crpc_decoded_response decoded = {0};
    const char *stage = NULL;

    check_equal(crpc_json_decode_response(result_json, sizeof(result_json) - 1u, UINT64_MAX, 200u,
                                          4u, NULL, &decoded, &stage),
                TURBO_OK);
    check_equal(decoded.response.request_id, UINT64_MAX);
    crpc_decoded_response_destroy(&decoded);

    check_equal(crpc_json_decode_response(error_json, sizeof(error_json) - 1u, UINT64_C(1), 200u,
                                          4u, NULL, &decoded, &stage),
                TURBO_OK);
    check_equal(decoded.response.value.remote_error.code, INT64_MIN);
    crpc_decoded_response_destroy(&decoded);
  }

  it("rejects fractional and overflowing numeric ids and error codes") {
    static const char fractional_id[] = "{\"jsonrpc\":\"2.0\",\"result\":true,\"id\":42e-1}";
    static const char overflow_id[] = "{\"jsonrpc\":\"2.0\",\"result\":true,"
                                      "\"id\":1.8446744073709551616e19}";
    static const char overflow_code[] =
        "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":"
        "9.223372036854775808e18,\"message\":\"overflow\"},\"id\":1}";
    crpc_decoded_response decoded = {0};
    const char *stage = NULL;

    check_equal(crpc_json_decode_response(fractional_id, sizeof(fractional_id) - 1u, UINT64_C(4),
                                          200u, 4u, NULL, &decoded, &stage),
                TURBO_EPROTO);
    check_equal(crpc_json_decode_response(overflow_id, sizeof(overflow_id) - 1u, UINT64_MAX, 200u,
                                          4u, NULL, &decoded, &stage),
                TURBO_EPROTO);
    check_equal(crpc_json_decode_response(overflow_code, sizeof(overflow_code) - 1u, UINT64_C(1),
                                          200u, 4u, NULL, &decoded, &stage),
                TURBO_EPROTO);
  }

  it("decodes a remote application error without converting it to transport failure") {
    static const char json[] = "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32602,"
                               "\"message\":\"bad\\u0000input\",\"data\":[1]},\"id\":7}";
    crpc_decoded_response decoded = {0};
    cserde_token token = {0};
    const char *stage = NULL;

    check_equal(crpc_json_decode_response(json, sizeof(json) - 1u, UINT64_C(7), 200u, 8u, NULL,
                                          &decoded, &stage),
                TURBO_OK);
    check_equal(decoded.response.kind, CRPC_RESPONSE_REMOTE_ERROR);
    check_equal(decoded.response.value.remote_error.code, (int64_t)-32602);
    check_equal(decoded.response.value.remote_error.message.size, (size_t)9u);
    check_equal(decoded.response.value.remote_error.message.data,
                (const unsigned char *)"bad\0input", 9u);
    check_equal(cserde_reader_next(decoded.response.value.remote_error.data, &token), CSERDE_OK);
    check_equal(token.kind, CSERDE_ARRAY_BEGIN);
    crpc_decoded_response_destroy(&decoded);
  }

  it("rejects malformed envelopes, duplicate ids, and non-integer error codes") {
    static const char both[] = "{\"jsonrpc\":\"2.0\",\"result\":1,\"error\":{},\"id\":1}";
    static const char duplicate[] = "{\"jsonrpc\":\"2.0\",\"result\":1,\"id\":1,\"id\":1}";
    static const char fractional_code[] =
        "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":1.5,\"message\":\"bad\"},"
        "\"id\":1}";
    static const char wrong_version[] = "{\"jsonrpc\":\"1.0\",\"result\":1,\"id\":1}";
    crpc_decoded_response decoded = {0};
    const char *stage = NULL;

    check_equal(crpc_json_decode_response(both, sizeof(both) - 1u, UINT64_C(1), 200u, 8u, NULL,
                                          &decoded, &stage),
                TURBO_EPROTO);
    check_equal(stage, "rpc-envelope");
    check_equal(crpc_json_decode_response(duplicate, sizeof(duplicate) - 1u, UINT64_C(1), 200u, 8u,
                                          NULL, &decoded, &stage),
                TURBO_EPROTO);
    check_equal(crpc_json_decode_response(fractional_code, sizeof(fractional_code) - 1u,
                                          UINT64_C(1), 200u, 8u, NULL, &decoded, &stage),
                TURBO_EPROTO);
    check_equal(crpc_json_decode_response(wrong_version, sizeof(wrong_version) - 1u, UINT64_C(1),
                                          200u, 8u, NULL, &decoded, &stage),
                TURBO_EPROTO);
  }

  it("distinguishes HTTP status and JSON depth failures") {
    static const char valid[] = "{\"jsonrpc\":\"2.0\",\"result\":[[1]],\"id\":1}";
    crpc_decoded_response decoded = {0};
    const char *stage = NULL;

    check_equal(crpc_json_decode_response(valid, sizeof(valid) - 1u, UINT64_C(1), 503u, 8u, NULL,
                                          &decoded, &stage),
                TURBO_EPROTO);
    check_equal(stage, "http-status");
    check_equal(crpc_json_decode_response(valid, sizeof(valid) - 1u, UINT64_C(1), 200u, 2u, NULL,
                                          &decoded, &stage),
                TURBO_EMSGSIZE);
    check_equal(stage, "json-depth");
  }
}
