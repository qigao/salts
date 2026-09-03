#if defined(CONSUME_PLATFORM)
  #include <salts/clock.h>
  #include <salts/error_codes.h>
  #include <salts/random.h>

int main(void) {
  unsigned char random_byte = 0u;
  return salts_platform_secure_random(&random_byte, sizeof(random_byte)) == SALTS_OK &&
                 salts_hrtime() != 0u
             ? 0
             : 1;
}

#elif defined(CONSUME_CONCURRENCY)
  #include <salts/thread_pool.h>

int main(void) {
  salts_threadpool_t *pool = salts_threadpool_create(1);
  if (!pool) return 1;
  salts_threadpool_destroy(pool);
  return 0;
}

#elif defined(CONSUME_COROUTINE)
  #include <salts_coro.h>
  #include <salts_coro_executor.h>

static void complete(coro_t *coroutine, void *user_data) {
  int *completed = (int *)user_data;
  (void)coroutine;
  *completed = 1;
}

typedef struct installed_executor_state {
  salts_coro_executor_t *executor;
  int completed;
  int status;
} installed_executor_state;

static void complete_with_await(coro_t *coroutine, void *user_data) {
  installed_executor_state *state = (installed_executor_state *)user_data;
  salts_coro_executor_await_t await_handle = {0};
  int completion_status = SALTS_EIO;
  (void)coroutine;

  state->status = salts_coro_executor_yield();
  if (state->status != SALTS_OK) return;
  state->status = salts_coro_executor_await_begin(&await_handle);
  if (state->status != SALTS_OK) return;
  state->status = salts_coro_executor_await_complete(state->executor, await_handle, SALTS_OK);
  if (state->status != SALTS_OK) return;
  state->status = salts_coro_executor_await(await_handle, &completion_status);
  if (state->status != SALTS_OK || completion_status != SALTS_OK) return;
  state->status = salts_coro_executor_await_begin(&await_handle);
  if (state->status != SALTS_OK) return;
  state->status = salts_coro_executor_await_abort(await_handle);
  if (state->status == SALTS_OK) state->completed = 1;
}

int main(void) {
  int completed = 0;
  installed_executor_state executor_state = {0};
  coro_t *coroutine = coro_create(complete, &completed, NULL);
  salts_coro_executor_config_t config = SALTS_CORO_EXECUTOR_CONFIG_DEFAULT;
  salts_coro_executor_task_t task = {complete_with_await, NULL, NULL, &executor_state};
  salts_coro_executor_t *executor;
  if (coroutine == NULL) return 1;
  if (coro_resume(coroutine) != 0 || completed != 1 || coro_alive(coroutine)) {
    coro_destroy(coroutine);
    return 2;
  }
  coro_destroy(coroutine);

  completed = 0;
  config.worker_count = 1u;
  config.queue_capacity_per_worker = 1u;
  config.coroutine_pool.initial_capacity = 0u;
  config.coroutine_pool.max_capacity = 1u;
  executor = salts_coro_executor_create(&config);
  if (executor == NULL) return 3;
  executor_state.executor = executor;
  if (salts_coro_executor_submit(executor, &task) != 0) {
    salts_coro_executor_destroy(executor);
    return 4;
  }
  if (salts_coro_executor_wait(executor) != 0 || executor_state.completed != 1 ||
      executor_state.status != SALTS_OK) {
    salts_coro_executor_destroy(executor);
    return 5;
  }
  if (salts_coro_executor_destroy(executor) != 0) return 6;
  return 0;
}

#elif defined(CONSUME_NATIVE_IO)
  #include <salts/native_io.h>

int main(void) {
  return native_io_backend_kind_model(NATIVE_IO_BACKEND_IOCP) == NATIVE_IO_MODEL_COMPLETION ? 0 : 1;
}

#elif defined(CONSUME_CNET)
  #include <cnet/cnet.h>
  #include <cnet/websocket.h>
  #include <string.h>

typedef struct installed_websocket_probe {
  uint8_t frame[CNET_WEBSOCKET_MAX_HEADER_BYTES + CNET_WEBSOCKET_MIN_FRAME_BYTES];
  size_t frame_size;
  size_t event_count;
} installed_websocket_probe;

static int installed_websocket_write(void *user, const uint8_t *data, size_t size) {
  installed_websocket_probe *probe = (installed_websocket_probe *)user;
  if (probe == NULL || data == NULL || size > sizeof(probe->frame)) return SALTS_ENOSPC;
  memcpy(probe->frame, data, size);
  probe->frame_size = size;
  return SALTS_OK;
}

static void installed_websocket_event(void *user, cnet_websocket *websocket,
                                      const cnet_websocket_event *event) {
  installed_websocket_probe *probe = (installed_websocket_probe *)user;
  (void)websocket;
  if (probe != NULL && event != NULL) ++probe->event_count;
}

int main(void) {
  static const uint8_t inbound_text[] = {0x81u, 0x01u, 'x'};
  cnet_client client = {0};
  cnet_listener listener = {0};
  cnet_tls_client tls_client = {0};
  cnet_tls_server tls_server = {0};
  cnet_tls_client_config tls_client_config = {.size = sizeof(tls_client_config)};
  cnet_tls_server_config tls_server_config = {.size = sizeof(tls_server_config)};
  cnet_connection connection = {0};
  cnet_websocket websocket = {0};
  installed_websocket_probe probe = {0};
  cnet_websocket_config websocket_config = {
      .size = sizeof(websocket_config),
      .role = CNET_WEBSOCKET_CLIENT,
      .max_frame_bytes = CNET_WEBSOCKET_MIN_FRAME_BYTES,
      .max_message_bytes = CNET_WEBSOCKET_MIN_FRAME_BYTES,
      .max_buffered_input_bytes = CNET_WEBSOCKET_MAX_HEADER_BYTES + CNET_WEBSOCKET_MIN_FRAME_BYTES,
      .write = installed_websocket_write,
      .on_event = installed_websocket_event,
      .user = &probe,
  };
  int status;

  if (client.impl != NULL || listener.impl != NULL || tls_client.impl != NULL ||
      tls_server.impl != NULL || tls_client_config.size != sizeof(tls_client_config) ||
      tls_server_config.size != sizeof(tls_server_config) || connection.slot != 0u ||
      connection.generation != 0u || websocket.impl != NULL ||
      CNET_WEBSOCKET_MAX_CONTROL_BYTES != 125 || CNET_TLS_MIN_IO_BUFFER_BYTES < 16384)
    return 1;
  status = cnet_websocket_init(&websocket, &websocket_config);
  if (status != SALTS_OK) return 2;
  status = cnet_websocket_send_text(&websocket, "x", 1u);
  if (status != SALTS_OK || probe.frame_size == 0u) {
    (void)cnet_websocket_destroy(&websocket);
    return 3;
  }
  status = cnet_websocket_feed(&websocket, inbound_text, sizeof(inbound_text));
  if (status != SALTS_OK || probe.event_count != 1u) {
    (void)cnet_websocket_destroy(&websocket);
    return 4;
  }
  if (cnet_websocket_destroy(&websocket) != SALTS_OK) return 5;
  return cnet_tls_client_destroy(&tls_client) == SALTS_OK ? 0 : 6;
}

#elif defined(CONSUME_CHTTP)
  #include <chttp/chttp.h>

int main(void) {
  chttp_client client = {0};
  chttp_tls_profile tls_profile = {0};
  chttp_server server = {0};
  chttp_session session = {0};
  chttp_websocket websocket = {0};
  chttp_websocket_client websocket_client = {0};
  chttp_websocket_pool websocket_pool = {0};
  chttp_websocket_session websocket_session = {0};
  chttp_response response = {0};
  chttp_options options = {0};
  chttp_client_config config = {0};
  chttp_server_config server_config = {0};
  chttp_body_source body_source = {0};
  chttp_body_sink body_sink = {0};
  chttp_websocket_client_config websocket_config = {0};
  chttp_websocket_connect_options websocket_options = {0};
  chttp_websocket_pool_config websocket_pool_config = {0};
  int (*response_source_fn)(chttp_server_response *, unsigned int, const char *,
                            const chttp_body_source *) = chttp_server_response_source;
  int (*response_file_fn)(chttp_server_response *, unsigned int, const char *, const char *) =
      chttp_server_response_file;
  config.h2_input_buffer_bytes = 64u * 1024u;
  server_config.enable_http2 = 1;
  server_config.h2_stream_capacity = 32u;
  websocket_config.h2_input_buffer_bytes = 128u * 1024u;
  websocket_config.h2_hpack_dynamic_table_bytes = 4096u;
  websocket_config.h2_max_settings_count = 16u;
  (void)response_source_fn;
  (void)response_file_fn;
  chttp_response_destroy(&response);
  return chttp_server_response_source(NULL, 0u, NULL, NULL) == SALTS_EINVAL &&
                 chttp_server_response_file(NULL, 0u, NULL, NULL) == SALTS_EINVAL &&
                 chttp_post_file(NULL, NULL, NULL, NULL, NULL, NULL, NULL) == SALTS_EINVAL &&
                 chttp_put_file(NULL, NULL, NULL, NULL, NULL, NULL, NULL) == SALTS_EINVAL &&
                 chttp_download_file(NULL, NULL, NULL, NULL, NULL, NULL, NULL) == SALTS_EINVAL &&
                 chttp_server_websocket_with(&server, NULL) == SALTS_EINVAL &&
                 chttp_websocket_state_get(&websocket, NULL) == SALTS_EINVAL &&
                 chttp_websocket_client_destroy(&websocket_client, 0u) == SALTS_OK &&
                 chttp_websocket_pool_destroy(&websocket_pool, 0u) == SALTS_OK &&
                 chttp_client_destroy(&client, 0u) == SALTS_OK &&
                 chttp_tls_profile_destroy(&tls_profile) == SALTS_OK &&
                 chttp_server_destroy(&server) == SALTS_OK && session.impl == NULL &&
                 options.protocol == CHTTP_HTTP_1_1 && CHTTP_HTTP_2 != CHTTP_HTTP_1_1 &&
                 config.h2_input_buffer_bytes == 64u * 1024u && server_config.enable_http2 == 1 &&
                 server_config.h2_stream_capacity == 32u && body_source.read == NULL &&
                 body_sink.write == NULL && websocket_config.size == 0u &&
                 websocket_config.h2_input_buffer_bytes == 128u * 1024u &&
                 websocket_config.h2_hpack_dynamic_table_bytes == 4096u &&
                 websocket_config.h2_max_settings_count == 16u && websocket_options.size == 0u &&
                 websocket_options.protocol == CHTTP_HTTP_1_1 && websocket_pool.impl == NULL &&
                 websocket_pool_config.session_capacity == 0u && websocket_session.slot == 0u &&
                 websocket_session.generation == 0u && CHTTP_METHOD_CONNECT != CHTTP_METHOD_OPTIONS
             ? 0
             : 1;
}

#elif defined(CONSUME_CRPC)
  #include <crpc/crpc.h>

int main(void) {
  crpc_client client = {0};
  crpc_async_client async_client = {0};
  crpc_request request = {0};
  crpc_response response = {0};
  crpc_options options = {0};
  crpc_server server = {0};
  crpc_response_destroy(&response);
  return client.impl == NULL && async_client.impl == NULL && request.slot == 0u &&
                 request.generation == 0u && options.tls == NULL &&
                 options.protocol == CHTTP_HTTP_1_1 && server.impl == NULL &&
                 crpc_server_destroy(&server) == SALTS_OK
             ? 0
             : 1;
}

#elif defined(CONSUME_S3)
  #include <s3/s3.h>
  #include <s3/s3_bucket.h>
  #include <s3/s3_bucket_config.h>
  #include <s3/s3_credentials.h>
  #include <s3/s3_multipart.h>
  #include <s3/s3_object.h>
  #include <s3/s3_signer.h>

int main(void) {
  s3_client client = {0};
  s3_async_client async_client = {0};
  s3_response response = {0};
  s3_bucket_list buckets = {0};
  s3_object_list objects = {0};
  s3_multipart multipart = {0};
  s3_signer_result signature = {0};
  s3_response_destroy(&response);
  s3_bucket_list_destroy(&buckets);
  s3_object_list_destroy(&objects);
  s3_signer_result_destroy(&signature);
  return client.impl == NULL && async_client.impl == NULL &&
                 s3_multipart_destroy(&multipart) == SALTS_OK && S3_MULTIPART_MAX_PARTS == 10000
             ? 0
             : 1;
}

#elif defined(CONSUME_CMETA)
  #include <cmeta/cmeta.h>

int main(void) { return cmeta_type_equal(&cmeta_type_int, &cmeta_type_int) ? 0 : 1; }

#elif defined(CONSUME_CBIND)
  #include <cbind/cbind.h>
  #include <salts_cmeta_fixed_width.h>

  #include <stddef.h>
  #include <stdint.h>

typedef struct cbind_consumer_reader_context {
  int emitted;
} cbind_consumer_reader_context;

static cserde_status cbind_consumer_next(void *context, cserde_token *out) {
  cbind_consumer_reader_context *state = (cbind_consumer_reader_context *)context;

  if (state->emitted) return CSERDE_DONE;
  out->kind = CSERDE_SINT;
  out->value.sint = 7;
  state->emitted = 1;
  return CSERDE_OK;
}

int main(void) {
  const cmeta_data_desc *fixed_width = &salts_int32_cmeta_data;
  cbind_consumer_reader_context source = {0};
  cserde_reader_ops ops = {offsetof(cserde_reader_ops, next) + sizeof(cserde_reader_next_fn),
                           CSERDE_READER_OPS_ABI_VERSION, cbind_consumer_next};
  cserde_reader reader = {0};
  cbind_context context = CBIND_CONTEXT_INIT(NULL, 0u, 0u);
  cbind_error error = CBIND_ERROR_INIT;
  int out = 0;

  if (fixed_width->storage_type->size != sizeof(int32_t)) return 1;
  if (cserde_reader_init(&reader, &ops, &source) != CSERDE_OK) return 1;
  if (cbind_decode(&context, &cmeta_data_int, &reader, &out, &error) != CBIND_OK) return 1;
  return out == 7 ? 0 : 1;
}

#elif defined(CONSUME_JSON_CBIND)
  #include <cbind/cbind.h>
  #include <json_cserde_reader.h>
  #include <json_parser.h>

  #include <stddef.h>
  #include <stdint.h>

  #define JSON_CBIND_DATA_PREFIX_SIZE                                                              \
    (offsetof(cmeta_data_desc, shape) + sizeof(((cmeta_data_desc *)0)->shape))

Struct(installed_json_record, (int, value));

static const cmeta_type_identity installed_json_record_identity =
    CMETA_TYPE_ID_ATOM_INIT("install.consumer.json.record");
static const cmeta_type_desc installed_json_record_type = {.name = "installed_json_record",
                                                           .size = sizeof(installed_json_record),
                                                           .align = _Alignof(installed_json_record),
                                                           .kind = CMETA_T_OBJECT,
                                                           .identity =
                                                               &installed_json_record_identity};
static const cmeta_data_field_desc installed_json_record_fields[] = {
    {"install.consumer.json.record.value", "value", offsetof(installed_json_record, value),
     &cmeta_data_int}};
static const cmeta_data_struct_shape installed_json_record_shape = {
    .layout = StructMeta(installed_json_record),
    .fields = installed_json_record_fields,
    .field_count = 1u};
static const cmeta_data_desc installed_json_record_data = {
    .struct_size = JSON_CBIND_DATA_PREFIX_SIZE,
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "install.consumer.json.record.data",
    .display_name = "Installed JSON record",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &installed_json_record_type,
    .shape = &installed_json_record_shape};

int main(void) {
  static const char json[] = "{\"value\":7}";
  json_value_t *root = json_parse(json, sizeof(json) - 1u);
  cserde_reader *reader;
  unsigned char scratch[1] = {0};
  cbind_context context = CBIND_CONTEXT_INIT(scratch, sizeof(scratch), 1u);
  cbind_error error = CBIND_ERROR_INIT;
  installed_json_record value = {0};
  cbind_status status;

  if (root == NULL) return 1;
  reader = json_cserde_reader_create(root, 1u);
  if (reader == NULL) {
    json_free(root);
    return 2;
  }
  status = cbind_decode(&context, &installed_json_record_data, reader, &value, &error);
  json_cserde_reader_destroy(reader);
  json_free(root);
  return status == CBIND_OK && value.value == 7 ? 0 : 3;
}

#elif defined(CONSUME_XML_PARSER)
  #include <xml_parser/xml_parser.h>

int main(void) {
  static const char xml[] = "<installed/>";
  salts_xml_document document = {0};
  salts_xml_status status = salts_xml_parse(&document, xml, sizeof(xml) - 1u, NULL, NULL);
  salts_xml_document_destroy(&document);
  return status == SALTS_XML_OK ? 0 : 1;
}

#elif defined(CONSUME_CFLOW_PROCESS)
  #include <cflow/process.h>

int main(void) {
  cflow_process process = {0};
  cflow_process_stats stats = {0};
  return process.impl == NULL && !stats.stdin_open && CFLOW_PROCESS_SUBMIT_ACCEPTED == 0 ? 0 : 1;
}

#elif defined(CONSUME_CFLOW)
  #include <cflow/actor.h>
  #include <cflow/adapters.h>
  #include <cflow/clock.h>
  #include <cflow/stream.h>

int main(void) {
  cflow_clock clock = {0};
  cflow_statechart_actor_config actor_config = {0};
  cflow_statechart_actor_stats actor_stats = {0};
  cflow_statechart_actor_init_result (*actor_init)(
      cflow_actor *, const cflow_statechart_actor_config *) = cflow_statechart_actor_init;
  bool (*actor_get_stats)(const cflow_actor *, cflow_statechart_actor_stats *) =
      cflow_statechart_actor_get_stats;
  cflow_stream stream = {0};
  cflow_result byte_result = {0};
  cflow_find_result found = {0};
  cflow_status_result terminal_result = {CFLOW_STATUS_OK};
  const char *terminal_error = NULL;
  size_t terminal_count = 1u;
  if (actor_config.statechart.statechart != NULL || actor_stats.state != CFLOW_ACTOR_STATE_START ||
      actor_init == NULL || actor_get_stats == NULL)
    return 1;
  if (!cflow_clock_system_init(&clock)) return 1;
  cflow_clock_destroy(&clock);
  if (!cflow_stream_init(&stream, &cmeta_type_int)) return 2;
  if (stream.skip(&stream, 1u)->take(&stream, 2u) != &stream) return 3;
  if (cflow_stream_count(&stream, &terminal_count, &terminal_error)) return 4;
  if (terminal_count != 0u || terminal_error == NULL) return 5;
  terminal_count = 1u;
  terminal_result = cflow_stream_count_result(&stream, &terminal_count);
  if (terminal_result.status != CFLOW_STATUS_INVALID_ARGUMENT || terminal_count != 0u ||
      cflow_status_result_message(terminal_result) == NULL)
    return 6;
  terminal_result = cflow_eval_stream_result(&stream, &byte_result);
  if (terminal_result.status != CFLOW_STATUS_INVALID_ARGUMENT || byte_result.data != NULL ||
      byte_result.count != 0u || byte_result.type != NULL)
    return 7;
  cflow_result_destroy(&byte_result);
  cflow_find_result_destroy(&found);
  cflow_stream_destroy(&stream);
  return 0;
}

#elif defined(CONSUME_CSTL_STREAM)
  #include <cstl/stream.h>

typed(List, InstalledStreamInts, int);

int main(void) {
  const int input[] = {1, 2, 1};
  InstalledStreamInts values = {0};
  InstalledStreamInts output = {0};
  cstl_stream_t pipeline = {0};
  cstl_collect_result result;
  cstl_status_result byte_status;
  cflow_result bytes = {0};
  size_t index;

  if (InstalledStreamInts_init(&values, 3u) != STL_OK) return 1;
  for (index = 0u; index < 3u; ++index) {
    if (InstalledStreamInts_push_back(&values, input[index]) != STL_OK) return 2;
  }
  if (!stream(&values, &pipeline)) return 3;
  if (!pipeline.distinct(&pipeline, 2u)) return 4;
  if (!pipeline.sorted(&pipeline, 2u)) return 5;
  byte_status = to_array_result(&pipeline, 2u, &bytes);
  if (!cstl_status_result_is_ok(byte_status) || bytes.count != 2u) {
    cflow_result_destroy(&bytes);
    cstl_stream_destroy(&pipeline);
    InstalledStreamInts_destroy(&values);
    return 6;
  }
  cflow_result_destroy(&bytes);
  result = collect_typed(&pipeline, InstalledStreamInts, &output, 2u);
  cstl_stream_destroy(&pipeline);
  InstalledStreamInts_destroy(&output);
  InstalledStreamInts_destroy(&values);
  return result.ok && result.flow_status == CFLOW_STATUS_OK && result.status == CMETA_OK &&
                 result.count == 2u
             ? 0
             : 7;
}

#elif defined(CONSUME_CSTL)
  #include <cstl/typed.h>

typed(Vec, InstalledInts, int);

int main(void) {
  InstalledInts values = {0};
  vec_t raw_values = VecOf(int);
  if (InstalledInts_init(&values, 1u) != STL_OK) return 1;
  if (vec_init(&raw_values, 1u) != STL_OK) {
    InstalledInts_destroy(&values);
    return 2;
  }
  vec_destroy(&raw_values);
  InstalledInts_destroy(&values);
  return 0;
}

#elif defined(CONSUME_CFLOW_FS)
  #include <cflow/fs.h>
  #include <cflow/fs_watch.h>
  #include <cflow/fs_watch_publisher.h>

static void fs_complete(void *user, uint64_t request_id, cflow_fs_operation_kind operation,
                        int result) {
  (void)user;
  (void)request_id;
  (void)operation;
  (void)result;
}

int main(void) {
  cflow_fs_service service = {0};
  cflow_fs_watch watch = {0};
  cflow_fs_watch_publisher_owner source_owner = {0};
  cflow_fs_config config = {1u, 1u, 64u, fs_complete, NULL};
  if (watch.impl != NULL) return 1;
  if (cflow_fs_service_init(&service, &config) != 0) return 2;
  if (cflow_fs_close(&service) != 0) return 3;
  while (!cflow_fs_is_quiescent(&service)) {
    size_t completed = 0u;
    if (cflow_fs_run_ready(&service, 1u, &completed) != 0) return 4;
  }
  if (cflow_fs_destroy(&service) != 0) return 5;
  return cflow_fs_watch_publisher_owner_close(&source_owner) == 0 ? 0 : 6;
}

#elif defined(CONSUME_CORE)
  #include <platform.h>
  #include <salts_cmeta_data.h>
  #include <salts_thread.h>

int main(void) {
  salts_threadpool_t *pool = salts_threadpool_create(1);
  if (!pool) return 1;
  salts_threadpool_destroy(pool);
  if (!salts_uuid_cmeta_data_valid(&salts_uuid_cmeta_data)) return 1;
  return salts_hrtime() == 0u;
}

#else
  #error "one Salts consumer contract is required"
#endif
