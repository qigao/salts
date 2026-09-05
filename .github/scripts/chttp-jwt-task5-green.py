from pathlib import Path


def patch(path, old, new):
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, found {count}: {old[:140]!r}")
    p.write_text(text.replace(old, new, 1))


path = "chttp/src/chttp_h2_server.c"

patch(
    path,
    '#include "chttp_h2_server.h"\n\n#include "chttp_server_runtime.h"\n',
    '#include "chttp_h2_server.h"\n\n#include "chttp_jwt_internal.h"\n#include "chttp_server_runtime.h"\n',
)

patch(
    path,
    """  if (!stream->trailers) {
    request = chttp_h2_server_request_view(stream);
    status = chttp_server_request_body_open(&stream->request_state, &request, &sink);
    if (status != SALTS_OK) {
      chttp_server_request_body_close(&stream->request_state, status);
      return chttp_h2_proto_submit_rst_stream(h2->protocol, stream_id,
                                              CHTTP_H2_ERR_INTERNAL_ERROR) == 0
                 ? 0
                 : -1;
    }
  }
""",
    """  if (!stream->trailers) {
    request = chttp_h2_server_request_view(stream);
    status = chttp_server_request_admit(&stream->request_state, &request, request.method);
    if (status == SALTS_EPERM) {
      chttp_server_response_builder_reset(&stream->request_state.response_builder);
      status = chttp_jwt_bearer_unauthorized_response(&stream->request_state.response);
      if (status == SALTS_OK) status = chttp_h2_server_submit_response(stream);
      return status == SALTS_OK ? 0 : -1;
    }
    if (status != SALTS_OK) {
      chttp_server_request_body_close(&stream->request_state, status);
      return chttp_h2_proto_submit_rst_stream(h2->protocol, stream_id,
                                              CHTTP_H2_ERR_INTERNAL_ERROR) == 0
                 ? 0
                 : -1;
    }
    status = chttp_server_request_body_open(&stream->request_state, &request, &sink);
    if (status != SALTS_OK) {
      chttp_server_request_body_close(&stream->request_state, status);
      return chttp_h2_proto_submit_rst_stream(h2->protocol, stream_id,
                                              CHTTP_H2_ERR_INTERNAL_ERROR) == 0
                 ? 0
                 : -1;
    }
  }
""",
)

patch(
    path,
    """  if (stream->response_submitted) return -1;
  if (stream->body_size > capacity || size > capacity - stream->body_size) {
""",
    """  if (stream->response_submitted && stream->request_state.admission_rejected) {
    if (size != 0u &&
        (chttp_h2_proto_consume_stream(h2->protocol, stream_id, size) != 0 ||
         chttp_h2_proto_consume_connection(h2->protocol, size) != 0))
      return -1;
    return 0;
  }
  if (stream->response_submitted) return -1;
  if (stream->body_size > capacity || size > capacity - stream->body_size) {
""",
)
