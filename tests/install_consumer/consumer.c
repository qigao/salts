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

#elif defined(CONSUME_CFLOW_SCXML)
#include <cflow/executor.h>
#include <cflow/scxml.h>
#include <cflow/statechart_runtime.h>

static bool installed_legacy_action(void *user, cflow_statechart_action_phase phase,
                                    cflow_machine_state_id owner, const void *state,
                                    const cflow_event_view *event, void *out_state,
                                    cflow_statechart_raise_fn raise_internal, void *raise_user,
                                    const char **out_error) {
  (void)user;
  (void)phase;
  (void)owner;
  (void)event;
  (void)raise_internal;
  (void)raise_user;
  if (state == NULL || out_state == NULL || out_error == NULL) return false;
  *(bool *)out_state = *(const bool *)state;
  *out_error = NULL;
  return true;
}

static bool installed_legacy_guard(void *user, const void *state,
                                   const cflow_event_view *event,
                                   bool *out_enabled,
                                   const char **out_error) {
  (void)user;
  (void)event;
  if (state == NULL || out_enabled == NULL || out_error == NULL) return false;
  *out_enabled = true;
  *out_error = NULL;
  return true;
}

int main(void) {
  static const char source[] =
      "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
      "<state id='active'><onentry><log label='installed'/>"
      "<if cond='In(active)'/>"
      "</onentry><transition cond='In(active)' target='done'/></state>"
      "<final id='done'/></scxml>";
  const cflow_statechart_executable_binding legacy = {
      1u, installed_legacy_action, NULL};
  const cflow_statechart_guard_binding legacy_guard = {
      1u, installed_legacy_guard, NULL};
  const cflow_statechart_runtime_hooks runtime_hooks = {
      CFLOW_STATECHART_RUNTIME_HOOKS_ABI_V1,
      sizeof(cflow_statechart_runtime_hooks), NULL, NULL};
  cflow_mailbox_status (*tagged_send)(
      cflow_statechart_instance *, const cflow_event_view *, uint64_t) =
      cflow_statechart_instance_try_send_tagged;
  cflow_mailbox_status (*report_invoke_event)(
      cflow_scxml_session *, uint64_t, const cflow_event_view *) =
      cflow_scxml_session_report_invoke_event;
  const cflow_scxml_invoke_adapter_v1 invoke_adapter_shape = {
      .abi_version = CFLOW_SCXML_INVOKE_ADAPTER_ABI_V1,
      .struct_size = sizeof(cflow_scxml_invoke_adapter_v1)};
  const cflow_scxml_cmeta_compile_options_v1 cmeta_compile_shape =
      cflow_scxml_cmeta_default_compile_options(NULL);
  const cflow_scxml_cmeta_session_options_v1 cmeta_session_shape = {
      .abi_version = CFLOW_SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
      .struct_size = sizeof(cflow_scxml_cmeta_session_options_v1),
      .initial_state = NULL};
  cflow_scxml_status (*compile_cmeta)(
      cflow_scxml_program *, const char *, size_t,
      const cflow_scxml_limits *,
      const cflow_scxml_cmeta_compile_options_v1 *,
      cflow_scxml_diagnostic *) = cflow_scxml_compile_cmeta;
  cflow_statechart_runtime_status (*init_cmeta)(
      cflow_scxml_session *, const cflow_scxml_session_config *,
      const cflow_scxml_cmeta_session_options_v1 *) =
      cflow_scxml_session_init_cmeta;
  cflow_scxml_program program = {0};
  cflow_executor executor = {0};
  cflow_statechart_instance instance = {0};
  cflow_scxml_session session = {0};
  cflow_statechart_instance_stats stats = {0};
  cflow_scxml_invoke_stats invoke_stats = {0};
  cflow_statechart_instance_config config;
  cflow_scxml_session_config session_config;
  const cflow_statechart_executable_binding *bindings = NULL;
  const cflow_statechart_guard_binding *guard_bindings = NULL;
  size_t binding_count = 0u;
  size_t guard_count = 0u;
  bool executor_initialized = false;
  bool instance_initialized = false;
  bool session_initialized = false;
  uint32_t requirements = UINT32_MAX;
  int result = 1;
  if (cflow_scxml_session_report_adapter_error(
          NULL, CFLOW_SCXML_ADAPTER_ERROR_KIND_EXECUTION) !=
          CFLOW_MAILBOX_INVALID_ARGUMENT ||
      cflow_scxml_session_report_send_done(NULL, "none", 4u) ||
      cflow_scxml_session_report_invoke_event(NULL, UINT64_C(1), NULL) !=
          CFLOW_MAILBOX_INVALID_ARGUMENT ||
      cflow_scxml_session_get_invoke_stats(NULL, &invoke_stats)) {
    goto cleanup;
  }
  cflow_scxml_status status = cflow_scxml_compile(
      &program, source, sizeof(source) - 1u, NULL, NULL);
  if (status == CFLOW_SCXML_OK &&
      !cflow_scxml_program_runtime_bindings(
          &program, &bindings, &binding_count)) {
    goto cleanup;
  }
  if (status == CFLOW_SCXML_OK &&
      !cflow_scxml_program_guard_bindings(
          &program, &guard_bindings, &guard_count)) {
    goto cleanup;
  }
  if (status != CFLOW_SCXML_OK || bindings == NULL || binding_count != 1u ||
      bindings[0].fn != NULL || bindings[0].contextual_fn == NULL ||
      guard_bindings == NULL || guard_count != 1u ||
      guard_bindings[0].fn != NULL ||
      guard_bindings[0].contextual_fn == NULL ||
      legacy.fn == NULL || legacy.contextual_fn != NULL ||
      legacy_guard.fn == NULL || legacy_guard.contextual_fn != NULL ||
      tagged_send == NULL || report_invoke_event == NULL ||
      invoke_adapter_shape.abi_version !=
          CFLOW_SCXML_INVOKE_ADAPTER_ABI_V1 ||
      invoke_adapter_shape.struct_size !=
          sizeof(cflow_scxml_invoke_adapter_v1) ||
      cmeta_compile_shape.abi_version !=
          CFLOW_SCXML_CMETA_COMPILE_OPTIONS_ABI_V1 ||
      cmeta_compile_shape.struct_size !=
          sizeof(cflow_scxml_cmeta_compile_options_v1) ||
      cmeta_compile_shape.root != NULL ||
      cmeta_session_shape.abi_version !=
          CFLOW_SCXML_CMETA_SESSION_OPTIONS_ABI_V1 ||
      cmeta_session_shape.struct_size !=
          sizeof(cflow_scxml_cmeta_session_options_v1) ||
      compile_cmeta == NULL || init_cmeta == NULL ||
      !cflow_scxml_program_requirements(&program, &requirements) ||
      requirements != CFLOW_SCXML_REQUIREMENT_NONE) {
    goto cleanup;
  }
  if (!cflow_executor_serial_init(&executor)) goto cleanup;
  executor_initialized = true;
  config = (cflow_statechart_instance_config){
      .statechart = cflow_scxml_program_statechart(&program),
      .initial_state = cflow_scxml_program_initial_state(&program),
      .guards = guard_bindings,
      .guard_count = guard_count,
      .executables = bindings,
      .executable_count = binding_count,
      .external_event_capacity = 2u,
      .internal_event_capacity = 2u,
      .completion_capacity = 2u,
      .microstep_limit = 16u,
      .executor = &executor,
      .runtime_hooks = &runtime_hooks};
  if (cflow_statechart_instance_init(&instance, &config) !=
      CFLOW_STATECHART_RUNTIME_OK) {
    goto cleanup;
  }
  instance_initialized = true;
  if (!cflow_executor_wait_idle(&executor) ||
      !cflow_statechart_instance_get_stats(&instance, &stats) ||
      !stats.done || stats.errored) {
    goto cleanup;
  }
  session_config = (cflow_scxml_session_config){
      .program = &program,
      .executor = &executor,
      .external_event_capacity = 2u,
      .internal_event_capacity = 2u,
      .completion_capacity = 2u,
      .microstep_limit = 16u,
      .effect_capacity = 2u};
  if (cflow_scxml_session_init(&session, &session_config) !=
      CFLOW_STATECHART_RUNTIME_OK) {
    goto cleanup;
  }
  session_initialized = true;
  if (!cflow_executor_wait_idle(&executor) ||
      !cflow_scxml_session_get_stats(&session, &stats) ||
      !stats.done || stats.errored) {
    goto cleanup;
  }
  result = 0;

cleanup:
  if (session_initialized &&
      cflow_scxml_session_destroy(&session) !=
          CFLOW_STATECHART_RUNTIME_OK) {
    result = 1;
  }
  if (instance_initialized &&
      cflow_statechart_instance_destroy(&instance) !=
          CFLOW_STATECHART_RUNTIME_OK) {
    result = 1;
  }
  if (executor_initialized) cflow_executor_destroy(&executor);
  cflow_scxml_program_destroy(&program);
  return result;
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
#include <cflow/fs_watch_source.h>

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
  cflow_fs_watch_source_owner source_owner = {0};
  cflow_fs_config config = {1u, 1u, 64u, fs_complete, NULL};
  if (watch.impl != NULL) return 1;
  if (cflow_fs_service_init(&service, &config) != 0) return 2;
  if (cflow_fs_close(&service) != 0) return 3;
  while (!cflow_fs_is_quiescent(&service)) {
    size_t completed = 0u;
    if (cflow_fs_run_ready(&service, 1u, &completed) != 0) return 4;
  }
  if (cflow_fs_destroy(&service) != 0) return 5;
  return cflow_fs_watch_source_owner_close(&source_owner) == 0 ? 0 : 6;
}

#elif defined(CONSUME_CFLOW_MINICORO)
#include <cflow/minicoro.h>

static void complete(cflow_minicoro *coroutine, void *user) {
  (void)coroutine;
  (void)user;
}

int main(void) {
  cflow_minicoro_config config = {
      "installed-minicoro", &cmeta_type_int, complete, NULL,
      0u, NULL, NULL, NULL};
  cflow_resumable resumable = {0};
  cflow_resume_ctx context = {0};
  int output = 0;
  cflow_step step;

  if (!cflow_resumable_from_minicoro(&resumable, &config)) return 1;
  step = resumable.ops->resume(resumable.state, &context, &output);
  if (step.kind != CFLOW_STEP_DONE) return 2;
  resumable.ops->destroy(resumable.state);
  return 0;
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
