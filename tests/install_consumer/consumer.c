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

int main(void) {
  Vec(int, values);
  if (vec_init(&values, 1u) != STL_OK) return 1;
  vec_destroy(&values);
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
