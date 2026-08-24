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

#include <stddef.h>

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
  cbind_consumer_reader_context source = {0};
  cserde_reader_ops ops = {
      offsetof(cserde_reader_ops, next) + sizeof(cserde_reader_next_fn),
      CSERDE_READER_OPS_ABI_VERSION,
      cbind_consumer_next};
  cserde_reader reader = {0};
  cbind_context context = CBIND_CONTEXT_INIT(NULL, 0u, 0u);
  cbind_error error = CBIND_ERROR_INIT;
  int out = 0;

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

#elif defined(CONSUME_CORE)
#include <platform.h>
#include <turbo_thread.h>

int main(void) {
  turbo_threadpool_t *pool = turbo_threadpool_create(1);
  if (!pool) return 1;
  turbo_threadpool_destroy(pool);
  return turbo_hrtime() == 0u;
}

#else
#error "one TurboUtils consumer contract is required"
#endif
