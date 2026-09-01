#if defined(CONSUME_PLATFORM)
#include <turbo/clock.h>

int main(void) { return turbo_hrtime() == 0u; }

#elif defined(CONSUME_CONCURRENCY)
#include <turbo/thread_pool.h>

int main(void) {
  turbo_threadpool_t *pool = turbo_threadpool_create(1);
  if (!pool) return 1;
  turbo_threadpool_destroy(pool);
  return 0;
}

#elif defined(CONSUME_COROUTINE)
#include <turbo_coro.h>

static void complete(coro_t *coroutine, void *user_data) {
  int *completed = (int *)user_data;
  (void)coroutine;
  *completed = 1;
}

int main(void) {
  int completed = 0;
  coro_t *coroutine = coro_create(complete, &completed, NULL);
  if (coroutine == NULL) return 1;
  if (coro_resume(coroutine) != 0 || completed != 1 || coro_alive(coroutine)) {
    coro_destroy(coroutine);
    return 2;
  }
  coro_destroy(coroutine);
  return 0;
}

#elif defined(CONSUME_NATIVE_IO)
#include <turbo/native_io.h>

int main(void) {
  return native_io_backend_kind_model(NATIVE_IO_BACKEND_IOCP) ==
                 NATIVE_IO_MODEL_COMPLETION
             ? 0
             : 1;
}

#elif defined(CONSUME_CMETA)
#include <cmeta/cmeta.h>

int main(void) {
  return cmeta_type_equal(&cmeta_type_int, &cmeta_type_int) ? 0 : 1;
}

#elif defined(CONSUME_CBIND)
#include <cbind/cbind.h>
#include <turbo_cmeta_fixed_width.h>

#include <stddef.h>
#include <stdint.h>

typedef struct cbind_consumer_reader_context {
  int emitted;
} cbind_consumer_reader_context;

static cserde_status cbind_consumer_next(void *context, cserde_token *out) {
  cbind_consumer_reader_context *state =
      (cbind_consumer_reader_context *)context;

  if (state->emitted) return CSERDE_DONE;
  out->kind = CSERDE_SINT;
  out->value.sint = 7;
  state->emitted = 1;
  return CSERDE_OK;
}

int main(void) {
  const cmeta_data_desc *fixed_width = &turbo_int32_cmeta_data;
  cbind_consumer_reader_context source = {0};
  cserde_reader_ops ops = {
      offsetof(cserde_reader_ops, next) + sizeof(cserde_reader_next_fn),
      CSERDE_READER_OPS_ABI_VERSION,
      cbind_consumer_next};
  cserde_reader reader = {0};
  cbind_context context = CBIND_CONTEXT_INIT(NULL, 0u, 0u);
  cbind_error error = CBIND_ERROR_INIT;
  int out = 0;

  if (fixed_width->storage_type->size != sizeof(int32_t)) return 1;
  if (cserde_reader_init(&reader, &ops, &source) != CSERDE_OK) return 1;
  if (cbind_decode(&context, &cmeta_data_int, &reader, &out, &error) !=
      CBIND_OK)
    return 1;
  return out == 7 ? 0 : 1;
}

#elif defined(CONSUME_JSON_CBIND)
#include <cbind/cbind.h>
#include <json_cserde_reader.h>
#include <json_parser.h>

#include <stddef.h>
#include <stdint.h>

#define JSON_CBIND_DATA_PREFIX_SIZE                                         \
  (offsetof(cmeta_data_desc, shape) + sizeof(((cmeta_data_desc *)0)->shape))

Struct(installed_json_record, (int, value));

static const cmeta_type_identity installed_json_record_identity =
    CMETA_TYPE_ID_ATOM_INIT("install.consumer.json.record");
static const cmeta_type_desc installed_json_record_type = {
    .name = "installed_json_record",
    .size = sizeof(installed_json_record),
    .align = _Alignof(installed_json_record),
    .kind = CMETA_T_OBJECT,
    .identity = &installed_json_record_identity};
static const cmeta_data_field_desc installed_json_record_fields[] = {
    {"install.consumer.json.record.value", "value",
     offsetof(installed_json_record, value), &cmeta_data_int}};
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
  status = cbind_decode(&context, &installed_json_record_data, reader, &value,
                        &error);
  json_cserde_reader_destroy(reader);
  json_free(root);
  return status == CBIND_OK && value.value == 7 ? 0 : 3;
}

#elif defined(CONSUME_XML_PARSER)
#include <xml_parser/xml_parser.h>

int main(void) {
  static const char xml[] = "<installed/>";
  turbo_xml_document document = {0};
  turbo_xml_status status = turbo_xml_parse(
      &document, xml, sizeof(xml) - 1u, NULL, NULL);
  turbo_xml_document_destroy(&document);
  return status == TURBO_XML_OK ? 0 : 1;
}

#elif defined(CONSUME_CFLOW_PROCESS)
#include <cflow/process.h>

int main(void) {
  cflow_process process = {0};
  cflow_process_stats stats = {0};
  return process.impl == NULL && !stats.stdin_open &&
                 CFLOW_PROCESS_SUBMIT_ACCEPTED == 0
             ? 0
             : 1;
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
      cflow_actor *, const cflow_statechart_actor_config *) =
      cflow_statechart_actor_init;
  bool (*actor_get_stats)(const cflow_actor *,
                          cflow_statechart_actor_stats *) =
      cflow_statechart_actor_get_stats;
  cflow_stream stream = {0};
  cflow_result byte_result = {0};
  cflow_find_result found = {0};
  cflow_status_result terminal_result = {CFLOW_STATUS_OK};
  const char *terminal_error = NULL;
  size_t terminal_count = 1u;
  if (actor_config.statechart.statechart != NULL ||
      actor_stats.state != CFLOW_ACTOR_STATE_START ||
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
  if (terminal_result.status != CFLOW_STATUS_INVALID_ARGUMENT ||
      terminal_count != 0u ||
      cflow_status_result_message(terminal_result) == NULL)
    return 6;
  terminal_result = cflow_eval_stream_result(&stream, &byte_result);
  if (terminal_result.status != CFLOW_STATUS_INVALID_ARGUMENT ||
      byte_result.data != NULL || byte_result.count != 0u ||
      byte_result.type != NULL)
    return 7;
  cflow_result_destroy(&byte_result);
  cflow_find_result_destroy(&found);
  cflow_stream_destroy(&stream);
  return 0;
}

#elif defined(CONSUME_STL_STREAM)
#include <turbostl/stream.h>

typed(List, InstalledStreamInts, int);

int main(void) {
  const int input[] = {1, 2, 1};
  InstalledStreamInts values = {0};
  InstalledStreamInts output = {0};
  turbostl_stream_t pipeline = {0};
  turbostl_collect_result result;
  turbostl_status_result byte_status;
  cflow_result bytes = {0};
  size_t index;

  if (InstalledStreamInts_init(&values, 3u) != STL_OK) return 1;
  for (index = 0u; index < 3u; ++index) {
    if (InstalledStreamInts_push_back(&values, input[index]) != STL_OK)
      return 2;
  }
  if (!stream(&values, &pipeline)) return 3;
  if (!pipeline.distinct(&pipeline, 2u)) return 4;
  if (!pipeline.sorted(&pipeline, 2u)) return 5;
  byte_status = to_array_result(&pipeline, 2u, &bytes);
  if (!turbostl_status_result_is_ok(byte_status) || bytes.count != 2u) {
    cflow_result_destroy(&bytes);
    turbostl_stream_destroy(&pipeline);
    InstalledStreamInts_destroy(&values);
    return 6;
  }
  cflow_result_destroy(&bytes);
  result = collect_typed(
      &pipeline, InstalledStreamInts, &output, 2u);
  turbostl_stream_destroy(&pipeline);
  InstalledStreamInts_destroy(&output);
  InstalledStreamInts_destroy(&values);
  return result.ok && result.flow_status == CFLOW_STATUS_OK &&
         result.status == CMETA_OK && result.count == 2u ? 0 : 7;
}

#elif defined(CONSUME_STL)
#include <turbostl/typed.h>

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

static void fs_complete(void *user, uint64_t request_id,
                        cflow_fs_operation_kind operation, int result) {
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

#elif defined(CONSUME_CFLOW_USB)
#include <cflow/usb.h>

int main(void) {
  cflow_usb_context context = {0};
  return context.impl == NULL ? 0 : 1;
}

#elif defined(CONSUME_CORE)
#include <platform.h>
#include <turbo_cmeta_data.h>
#include <turbo_thread.h>

int main(void) {
  turbo_threadpool_t *pool = turbo_threadpool_create(1);
  if (!pool) return 1;
  turbo_threadpool_destroy(pool);
  if (!turbo_uuid_cmeta_data_valid(&turbo_uuid_cmeta_data)) return 1;
  return turbo_hrtime() == 0u;
}

#else
#error "one TurboUtils consumer contract is required"
#endif
