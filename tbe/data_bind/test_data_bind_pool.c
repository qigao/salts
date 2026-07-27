#include "data_bind.h"
#include "tinytest.h"
#include "turbo_thread.h"

#include <stdio.h>
#include <stdatomic.h>
#include <string.h>

#define DATA_BIND_POOL_TEST_ITEMS 80U
#define DATA_BIND_POOL_TEST_THREADS 4U
#define DATA_BIND_POOL_TEST_THREAD_ITERS 200U
#define DATA_BIND_POOL_TEST_TOGGLE_ITERS 200U

typedef struct data_bind_pool_test_worker {
  DataBind *codec;
  const char *json;
  atomic_size_t *ready_count;
  atomic_int *start;
  atomic_size_t *failures;
} data_bind_pool_test_worker_t;

typedef struct data_bind_pool_test_toggler {
  atomic_size_t *ready_count;
  atomic_int *start;
} data_bind_pool_test_toggler_t;

static void data_bind_pool_test_worker(void *arg) {
  data_bind_pool_test_worker_t *worker = (data_bind_pool_test_worker_t *)arg;
  size_t i;
  atomic_fetch_add_explicit(worker->ready_count, 1U, memory_order_release);
  while (!atomic_load_explicit(worker->start, memory_order_acquire)) turbo_thread_yield();
  for (i = 0; i < DATA_BIND_POOL_TEST_THREAD_ITERS; ++i) {
    DataBindValue *value = NULL;
    if (data_bind_parse_json(worker->codec, "Batch", worker->json, strlen(worker->json), &value,
                             NULL) != DATA_BIND_OK ||
        value == NULL) {
      atomic_fetch_add_explicit(worker->failures, 1U, memory_order_relaxed);
    }
    data_bind_value_free(value);
  }
}

static void data_bind_pool_test_toggler(void *arg) {
  data_bind_pool_test_toggler_t *toggler = (data_bind_pool_test_toggler_t *)arg;
  size_t i;
  atomic_fetch_add_explicit(toggler->ready_count, 1U, memory_order_release);
  while (!atomic_load_explicit(toggler->start, memory_order_acquire)) turbo_thread_yield();
  for (i = 0; i < DATA_BIND_POOL_TEST_TOGGLE_ITERS; ++i) {
    data_bind_set_value_pool_enabled(0);
    data_bind_set_value_pool_enabled(1);
  }
}

static void data_bind_pool_write_schema(void) {
  FILE *file = fopen("test_data_bind_pool.tbe", "w");
  check_not_null(file);
  if (file == NULL) return;
  fputs("message Batch { uint32[80] values; }\n", file);
  fclose(file);
}

static void data_bind_pool_build_json(char *json, size_t capacity) {
  size_t offset = 0;
  size_t i;
  int written = snprintf(json, capacity, "{\"values\":[");
  check_true(written > 0);
  if (written <= 0) return;
  offset = (size_t)written;
  for (i = 0; i < DATA_BIND_POOL_TEST_ITEMS; ++i) {
    written = snprintf(json + offset, capacity - offset, "%s%u", i == 0 ? "" : ",",
                       (unsigned)i);
    check_true(written > 0 && (size_t)written < capacity - offset);
    if (written <= 0 || (size_t)written >= capacity - offset) return;
    offset += (size_t)written;
  }
  written = snprintf(json + offset, capacity - offset, "]}");
  check_true(written == 2);
}

spec("DataBind value pool") {
  it("should retain at most 64 freed nodes and reuse them") {
    char json[512];
    DataBind *codec = NULL;
    DataBindValue *value = NULL;
    DataBindError error = DATA_BIND_ERROR_INIT;
    DataBindStatus status;
    size_t allocated_before = 0;
    size_t reused_before = 0;
    size_t allocated_after_first = 0;
    size_t reused_after_first = 0;
    size_t allocated_after_second = 0;
    size_t reused_after_second = 0;

    data_bind_pool_write_schema();
    data_bind_pool_build_json(json, sizeof(json));
    data_bind_set_value_pool_enabled(1);
    data_bind_get_value_pool_stats(&allocated_before, &reused_before);

    status = data_bind_create("test_data_bind_pool.tbe", &codec, &error);
    check_int_eq(status, DATA_BIND_OK);
    check_not_null(codec);
    if (codec != NULL) {
      status = data_bind_parse_json(codec, "Batch", json, strlen(json), &value, &error);
      check_int_eq(status, DATA_BIND_OK);
      check_not_null(value);
      data_bind_value_free(value);
      value = NULL;
      data_bind_get_value_pool_stats(&allocated_after_first, &reused_after_first);

      status = data_bind_parse_json(codec, "Batch", json, strlen(json), &value, &error);
      check_int_eq(status, DATA_BIND_OK);
      check_not_null(value);
      data_bind_value_free(value);
      value = NULL;
      data_bind_get_value_pool_stats(&allocated_after_second, &reused_after_second);

      check_size_ge(allocated_after_first - allocated_before, DATA_BIND_POOL_TEST_ITEMS + 2U);
      check_size_eq(reused_after_first, reused_before);
      check_size_eq(reused_after_second - reused_after_first, 64U);
      check_size_eq(allocated_after_second - allocated_after_first,
                    DATA_BIND_POOL_TEST_ITEMS + 2U - 64U);
    }

    data_bind_value_free(value);
    data_bind_free(codec);
    data_bind_set_value_pool_enabled(0);
    remove("test_data_bind_pool.tbe");
  }

  it("should parse and release values safely from concurrent codecs") {
    char json[512];
    DataBind *codecs[DATA_BIND_POOL_TEST_THREADS] = {0};
    data_bind_pool_test_worker_t workers[DATA_BIND_POOL_TEST_THREADS];
    turbo_thread_t threads[DATA_BIND_POOL_TEST_THREADS] = {0};
    turbo_thread_t toggle_thread = {0};
    data_bind_pool_test_toggler_t toggler;
    atomic_size_t ready_count = 0;
    atomic_size_t failures = 0;
    atomic_int start = 0;
    size_t created = 0;
    size_t toggle_created = 0;
    size_t i;

    data_bind_pool_write_schema();
    data_bind_pool_build_json(json, sizeof(json));
    data_bind_set_value_pool_enabled(1);

    for (i = 0; i < DATA_BIND_POOL_TEST_THREADS; ++i) {
      check_int_eq(data_bind_create("test_data_bind_pool.tbe", &codecs[i], NULL), DATA_BIND_OK);
      check_not_null(codecs[i]);
      workers[i].codec = codecs[i];
      workers[i].json = json;
      workers[i].ready_count = &ready_count;
      workers[i].start = &start;
      workers[i].failures = &failures;
      if (codecs[i] != NULL &&
          turbo_thread_create(&threads[i], data_bind_pool_test_worker, &workers[i]) == 0) {
        created++;
      } else {
        atomic_fetch_add_explicit(&failures, 1U, memory_order_relaxed);
        break;
      }
    }

    toggler.ready_count = &ready_count;
    toggler.start = &start;
    if (turbo_thread_create(&toggle_thread, data_bind_pool_test_toggler, &toggler) == 0) {
      toggle_created = 1;
    } else {
      atomic_fetch_add_explicit(&failures, 1U, memory_order_relaxed);
    }

    while (atomic_load_explicit(&ready_count, memory_order_acquire) < created + toggle_created)
      turbo_thread_yield();
    atomic_store_explicit(&start, 1, memory_order_release);
    for (i = 0; i < created; ++i) check_int_eq(turbo_thread_join(&threads[i]), 0);
    if (toggle_created != 0) check_int_eq(turbo_thread_join(&toggle_thread), 0);

    check_size_eq(created, DATA_BIND_POOL_TEST_THREADS);
    check_size_eq(toggle_created, 1U);
    check_size_eq(atomic_load_explicit(&failures, memory_order_relaxed), 0U);

    for (i = 0; i < DATA_BIND_POOL_TEST_THREADS; ++i) data_bind_free(codecs[i]);
    data_bind_set_value_pool_enabled(0);
    remove("test_data_bind_pool.tbe");
  }
}
