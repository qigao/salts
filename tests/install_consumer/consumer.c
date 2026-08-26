#if defined(CONSUME_CAPTURE)
#include <turbo_capture.h>

int main(void) {
  const turbo_video_native_mode_t ntsc = {
      1920, 1080, 30000u, 1001u, TURBO_VIDEO_CAPTURE_FORMAT_NV12, 1u};
  return turbo_video_mode_fps(&ntsc) == 30 ? 0 : 1;
}

#elif defined(CONSUME_PLATFORM)
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

#elif defined(CONSUME_CFLOW)
#include <cflow/clock.h>

int main(void) {
  cflow_clock clock = {0};
  if (!cflow_clock_system_init(&clock)) return 1;
  cflow_clock_destroy(&clock);
  return 0;
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
