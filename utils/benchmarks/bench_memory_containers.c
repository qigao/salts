#include "tinytest.h"
#include "turbo_buffer.h"
#include "turbo_containers.h"
#include "turbo_thread.h"

#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define MEMORY_BENCH_ITERS 5000U
#define MEMORY_BENCH_BATCH 256U
#define MEMORY_BENCH_BLOCK_SIZE 64U
#define CONTAINER_BENCH_ITERS 2000U
#define CONTAINER_BENCH_ITEMS 1024U
#define EXTERNAL_WRAPPER_BENCH_ITERS 100000U
#define EXTERNAL_WRAPPER_BENCH_REPEATS 5U
#define EXTERNAL_WRAPPER_BENCH_MAX_THREADS 8U
#define BUFFER_RECYCLE_BENCH_ITERS 100000U
#define BUFFER_RECYCLE_BENCH_REPEATS 5U
#define BUFFER_RECYCLE_BENCH_SIZE 512U
#define BUFFER_RECYCLE_BENCH_LOCK_SPACER 128U
#define SLAB_ALLOC_BENCH_ITERS 100000U
#define SLAB_ALLOC_BENCH_REPEATS 5U
#define SLAB_ALLOC_BENCH_FIXED_SIZE 64U
#define SLAB_ALLOC_BENCH_SIZE_CLASSES 9U

typedef struct memory_bench_heap_item {
  uint32_t priority;
  unsigned char payload[508];
} memory_bench_heap_item_t;

static volatile uintptr_t g_memory_bench_sink;
static mem_pool_t g_memory_bench_pool;
static unsigned char g_external_wrapper_data;
static atomic_uintptr_t g_parallel_bench_sink;
static atomic_size_t g_memory_bench_failures;

typedef struct external_wrapper_bench_worker {
  atomic_size_t *ready_count;
  atomic_int *start;
  size_t iterations;
  int locked_baseline;
} external_wrapper_bench_worker_t;

typedef struct buffer_recycle_bench_worker {
  atomic_size_t *ready_count;
  atomic_int *start;
  mem_pool_t *shared_pool;
  mem_pool_t local_pool;
  size_t iterations;
  int owner_local;
  int serialized;
  unsigned char lock_spacer[BUFFER_RECYCLE_BENCH_LOCK_SPACER];
} buffer_recycle_bench_worker_t;

typedef struct slab_alloc_bench_worker {
  atomic_size_t *ready_count;
  atomic_int *start;
  mem_pool_t *shared_pool;
  mem_pool_t local_pool;
  size_t iterations;
  int owner_local;
  int mixed_sizes;
  unsigned char lock_spacer[BUFFER_RECYCLE_BENCH_LOCK_SPACER];
} slab_alloc_bench_worker_t;

static const size_t g_slab_alloc_bench_sizes[SLAB_ALLOC_BENCH_SIZE_CLASSES] = {
    16U, 48U, 112U, 240U, 496U, 1008U, 2032U, 4080U, 8176U};

static turbo_mutex_t g_memory_bench_baseline_mutex;

static void external_wrapper_bench_worker_run(void *arg) {
  external_wrapper_bench_worker_t *worker = (external_wrapper_bench_worker_t *)arg;
  uintptr_t local_sink = 0;
  size_t i;
  atomic_fetch_add_explicit(worker->ready_count, 1U, memory_order_release);
  while (!atomic_load_explicit(worker->start, memory_order_acquire)) turbo_thread_yield();
  for (i = 0; i < worker->iterations; ++i) {
    mem_buffer_t *buffer;
    if (worker->locked_baseline) {
      buffer = (mem_buffer_t *)malloc(sizeof(*buffer));
    } else {
      buffer =
          mem_wrap_external(&g_external_wrapper_data, sizeof(g_external_wrapper_data), NULL, NULL);
    }
    if (buffer == NULL) {
      atomic_fetch_add_explicit(&g_memory_bench_failures, 1U, memory_order_relaxed);
      continue;
    }
    local_sink ^= (uintptr_t)buffer;
    if (worker->locked_baseline) {
      turbo_mutex_lock(&g_memory_bench_baseline_mutex);
      free(buffer);
      turbo_mutex_unlock(&g_memory_bench_baseline_mutex);
    } else {
      mem_buffer_release(buffer);
    }
  }
  atomic_fetch_xor_explicit(&g_parallel_bench_sink, local_sink, memory_order_relaxed);
}

static void external_wrapper_bench_parallel(size_t thread_count, int locked_baseline) {
  turbo_thread_t threads[EXTERNAL_WRAPPER_BENCH_MAX_THREADS] = {0};
  external_wrapper_bench_worker_t workers[EXTERNAL_WRAPPER_BENCH_MAX_THREADS];
  atomic_size_t ready_count = 0;
  atomic_int start = 0;
  size_t created = 0;
  size_t i;

  for (i = 0; i < thread_count; ++i) {
    workers[i].ready_count = &ready_count;
    workers[i].start = &start;
    workers[i].iterations = EXTERNAL_WRAPPER_BENCH_ITERS;
    workers[i].locked_baseline = locked_baseline;
    if (turbo_thread_create(&threads[i], external_wrapper_bench_worker_run, &workers[i]) != 0) {
      atomic_fetch_add_explicit(&g_memory_bench_failures, 1U, memory_order_relaxed);
      break;
    }
    created++;
  }
  while (atomic_load_explicit(&ready_count, memory_order_acquire) < created) turbo_thread_yield();
  atomic_store_explicit(&start, 1, memory_order_release);
  for (i = 0; i < created; ++i) {
    if (turbo_thread_join(&threads[i]) != 0)
      atomic_fetch_add_explicit(&g_memory_bench_failures, 1U, memory_order_relaxed);
  }
}

static void buffer_recycle_bench_worker_run(void *arg) {
  buffer_recycle_bench_worker_t *worker = (buffer_recycle_bench_worker_t *)arg;
  mem_pool_t *pool = worker->shared_pool;
  uintptr_t local_sink = 0;
  size_t i;

  if (worker->owner_local) {
    if (mem_init(&worker->local_pool, 0) != 0) {
      atomic_fetch_add_explicit(&g_memory_bench_failures, 1U, memory_order_relaxed);
      atomic_fetch_add_explicit(worker->ready_count, 1U, memory_order_release);
      return;
    }
    pool = &worker->local_pool;
  }

  atomic_fetch_add_explicit(worker->ready_count, 1U, memory_order_release);
  while (!atomic_load_explicit(worker->start, memory_order_acquire)) turbo_thread_yield();
  for (i = 0; i < worker->iterations; ++i) {
    mem_buffer_t *buffer;
    if (worker->serialized) turbo_mutex_lock(&g_memory_bench_baseline_mutex);
    buffer = mem_get_buffer(pool, BUFFER_RECYCLE_BENCH_SIZE);
    if (worker->serialized) turbo_mutex_unlock(&g_memory_bench_baseline_mutex);
    if (buffer == NULL) {
      atomic_fetch_add_explicit(&g_memory_bench_failures, 1U, memory_order_relaxed);
      continue;
    }
    local_sink ^= (uintptr_t)buffer;
    if (worker->serialized) turbo_mutex_lock(&g_memory_bench_baseline_mutex);
    mem_buffer_release(buffer);
    if (worker->serialized) turbo_mutex_unlock(&g_memory_bench_baseline_mutex);
  }
  atomic_fetch_xor_explicit(&g_parallel_bench_sink, local_sink, memory_order_relaxed);
  if (worker->owner_local) mem_destroy(&worker->local_pool);
}

static void buffer_recycle_bench_parallel(size_t thread_count, int owner_local, int serialized) {
  turbo_thread_t threads[EXTERNAL_WRAPPER_BENCH_MAX_THREADS] = {0};
  buffer_recycle_bench_worker_t workers[EXTERNAL_WRAPPER_BENCH_MAX_THREADS] = {0};
  atomic_size_t ready_count = 0;
  atomic_int start = 0;
  size_t created = 0;
  size_t i;

  for (i = 0; i < thread_count; ++i) {
    workers[i].ready_count = &ready_count;
    workers[i].start = &start;
    workers[i].shared_pool = &g_memory_bench_pool;
    workers[i].iterations = BUFFER_RECYCLE_BENCH_ITERS;
    workers[i].owner_local = owner_local;
    workers[i].serialized = serialized;
    if (turbo_thread_create(&threads[i], buffer_recycle_bench_worker_run, &workers[i]) != 0) {
      atomic_fetch_add_explicit(&g_memory_bench_failures, 1U, memory_order_relaxed);
      break;
    }
    created++;
  }
  while (atomic_load_explicit(&ready_count, memory_order_acquire) < created) turbo_thread_yield();
  atomic_store_explicit(&start, 1, memory_order_release);
  for (i = 0; i < created; ++i) {
    if (turbo_thread_join(&threads[i]) != 0)
      atomic_fetch_add_explicit(&g_memory_bench_failures, 1U, memory_order_relaxed);
  }
}

static void slab_alloc_bench_worker_run(void *arg) {
  slab_alloc_bench_worker_t *worker = (slab_alloc_bench_worker_t *)arg;
  mem_pool_t *pool = worker->shared_pool;
  uintptr_t local_sink = 0;
  size_t i;

  if (worker->owner_local) {
    if (mem_init(&worker->local_pool, 0) != 0) {
      atomic_fetch_add_explicit(&g_memory_bench_failures, 1U, memory_order_relaxed);
      atomic_fetch_add_explicit(worker->ready_count, 1U, memory_order_release);
      return;
    }
    pool = &worker->local_pool;
  }

  atomic_fetch_add_explicit(worker->ready_count, 1U, memory_order_release);
  while (!atomic_load_explicit(worker->start, memory_order_acquire)) turbo_thread_yield();
  for (i = 0; i < worker->iterations; ++i) {
    size_t size = worker->mixed_sizes
                      ? g_slab_alloc_bench_sizes[i % SLAB_ALLOC_BENCH_SIZE_CLASSES]
                      : SLAB_ALLOC_BENCH_FIXED_SIZE;
    void *ptr = mem_alloc(pool, size);
    if (ptr == NULL) {
      atomic_fetch_add_explicit(&g_memory_bench_failures, 1U, memory_order_relaxed);
      continue;
    }
    local_sink ^= (uintptr_t)ptr;
    mem_free(pool, ptr);
  }
  atomic_fetch_xor_explicit(&g_parallel_bench_sink, local_sink, memory_order_relaxed);
  if (worker->owner_local) mem_destroy(&worker->local_pool);
}

static void slab_alloc_bench_parallel(size_t thread_count, int owner_local, int mixed_sizes) {
  turbo_thread_t threads[EXTERNAL_WRAPPER_BENCH_MAX_THREADS] = {0};
  slab_alloc_bench_worker_t workers[EXTERNAL_WRAPPER_BENCH_MAX_THREADS] = {0};
  atomic_size_t ready_count = 0;
  atomic_int start = 0;
  size_t created = 0;
  size_t i;

  for (i = 0; i < thread_count; ++i) {
    workers[i].ready_count = &ready_count;
    workers[i].start = &start;
    workers[i].shared_pool = &g_memory_bench_pool;
    workers[i].iterations = SLAB_ALLOC_BENCH_ITERS;
    workers[i].owner_local = owner_local;
    workers[i].mixed_sizes = mixed_sizes;
    if (turbo_thread_create(&threads[i], slab_alloc_bench_worker_run, &workers[i]) != 0) {
      atomic_fetch_add_explicit(&g_memory_bench_failures, 1U, memory_order_relaxed);
      break;
    }
    created++;
  }
  while (atomic_load_explicit(&ready_count, memory_order_acquire) < created) turbo_thread_yield();
  atomic_store_explicit(&start, 1, memory_order_release);
  for (i = 0; i < created; ++i) {
    if (turbo_thread_join(&threads[i]) != 0)
      atomic_fetch_add_explicit(&g_memory_bench_failures, 1U, memory_order_relaxed);
  }
}

static int memory_bench_heap_compare(const void *left, const void *right, void *ctx) {
  const memory_bench_heap_item_t *a = (const memory_bench_heap_item_t *)left;
  const memory_bench_heap_item_t *b = (const memory_bench_heap_item_t *)right;
  (void)ctx;
  return (a->priority > b->priority) - (a->priority < b->priority);
}

spec("Memory and container allocation benchmarks") {
  before_all() { turbo_mutex_init(&g_memory_bench_baseline_mutex); }

  after_all() { turbo_mutex_destroy(&g_memory_bench_baseline_mutex); }

  before_each() { check_int_eq(mem_init(&g_memory_bench_pool, 0), 0); }

  after_each() { mem_destroy(&g_memory_bench_pool); }

  bench("Small allocation batches") {
    void *pointers[MEMORY_BENCH_BATCH];
    size_t i;

    for (i = 0; i < MEMORY_BENCH_BATCH; ++i) {
      pointers[i] = mem_alloc(&g_memory_bench_pool, MEMORY_BENCH_BLOCK_SIZE);
      check_not_null(pointers[i]);
    }
    mem_reset(&g_memory_bench_pool);

    benchmark("libc malloc/free: 256 x 64B", MEMORY_BENCH_ITERS, MEMORY_BENCH_BATCH) {
      for (i = 0; i < MEMORY_BENCH_BATCH; ++i) {
        pointers[i] = malloc(MEMORY_BENCH_BLOCK_SIZE);
      }
      g_memory_bench_sink ^= (uintptr_t)pointers[MEMORY_BENCH_BATCH - 1U];
      for (i = 0; i < MEMORY_BENCH_BATCH; ++i) free(pointers[i]);
    }

    benchmark("mem_pool alloc/free: 256 x 64B", MEMORY_BENCH_ITERS, MEMORY_BENCH_BATCH) {
      for (i = 0; i < MEMORY_BENCH_BATCH; ++i) {
        pointers[i] = mem_alloc(&g_memory_bench_pool, MEMORY_BENCH_BLOCK_SIZE);
      }
      g_memory_bench_sink ^= (uintptr_t)pointers[MEMORY_BENCH_BATCH - 1U];
      for (i = 0; i < MEMORY_BENCH_BATCH; ++i) mem_free(&g_memory_bench_pool, pointers[i]);
    }

    benchmark("mem_pool alloc/reset: 256 x 64B", MEMORY_BENCH_ITERS, MEMORY_BENCH_BATCH) {
      for (i = 0; i < MEMORY_BENCH_BATCH; ++i) {
        pointers[i] = mem_alloc(&g_memory_bench_pool, MEMORY_BENCH_BLOCK_SIZE);
      }
      g_memory_bench_sink ^= (uintptr_t)pointers[MEMORY_BENCH_BATCH - 1U];
      mem_reset(&g_memory_bench_pool);
    }
  }

  bench("External buffer wrapper allocation") {
    benchmark("locked malloc shell: 1 thread", EXTERNAL_WRAPPER_BENCH_REPEATS,
              EXTERNAL_WRAPPER_BENCH_ITERS) {
      external_wrapper_bench_parallel(1U, 1);
    }
    benchmark("locked malloc shell: 2 threads", EXTERNAL_WRAPPER_BENCH_REPEATS,
              2U * EXTERNAL_WRAPPER_BENCH_ITERS) {
      external_wrapper_bench_parallel(2U, 1);
    }
    benchmark("locked malloc shell: 4 threads", EXTERNAL_WRAPPER_BENCH_REPEATS,
              4U * EXTERNAL_WRAPPER_BENCH_ITERS) {
      external_wrapper_bench_parallel(4U, 1);
    }
    benchmark("locked malloc shell: 8 threads", EXTERNAL_WRAPPER_BENCH_REPEATS,
              8U * EXTERNAL_WRAPPER_BENCH_ITERS) {
      external_wrapper_bench_parallel(8U, 1);
    }
    benchmark("atomic wrapper cache: 1 thread", EXTERNAL_WRAPPER_BENCH_REPEATS,
              EXTERNAL_WRAPPER_BENCH_ITERS) {
      external_wrapper_bench_parallel(1U, 0);
    }
    benchmark("atomic wrapper cache: 2 threads", EXTERNAL_WRAPPER_BENCH_REPEATS,
              2U * EXTERNAL_WRAPPER_BENCH_ITERS) {
      external_wrapper_bench_parallel(2U, 0);
    }
    benchmark("atomic wrapper cache: 4 threads", EXTERNAL_WRAPPER_BENCH_REPEATS,
              4U * EXTERNAL_WRAPPER_BENCH_ITERS) {
      external_wrapper_bench_parallel(4U, 0);
    }
    benchmark("atomic wrapper cache: 8 threads", EXTERNAL_WRAPPER_BENCH_REPEATS,
              8U * EXTERNAL_WRAPPER_BENCH_ITERS) {
      external_wrapper_bench_parallel(8U, 0);
    }
    check_size_eq(atomic_load_explicit(&g_memory_bench_failures, memory_order_relaxed), 0U);
  }

  bench("Internal buffer recycling") {
    benchmark("serialized recycle baseline: 1 thread", BUFFER_RECYCLE_BENCH_REPEATS,
              BUFFER_RECYCLE_BENCH_ITERS) {
      buffer_recycle_bench_parallel(1U, 0, 1);
    }
    benchmark("serialized recycle baseline: 2 threads", BUFFER_RECYCLE_BENCH_REPEATS,
              2U * BUFFER_RECYCLE_BENCH_ITERS) {
      buffer_recycle_bench_parallel(2U, 0, 1);
    }
    benchmark("serialized recycle baseline: 4 threads", BUFFER_RECYCLE_BENCH_REPEATS,
              4U * BUFFER_RECYCLE_BENCH_ITERS) {
      buffer_recycle_bench_parallel(4U, 0, 1);
    }
    benchmark("serialized recycle baseline: 8 threads", BUFFER_RECYCLE_BENCH_REPEATS,
              8U * BUFFER_RECYCLE_BENCH_ITERS) {
      buffer_recycle_bench_parallel(8U, 0, 1);
    }
    benchmark("shared pool recycle: 1 thread", BUFFER_RECYCLE_BENCH_REPEATS,
              BUFFER_RECYCLE_BENCH_ITERS) {
      buffer_recycle_bench_parallel(1U, 0, 0);
    }
    benchmark("shared pool recycle: 2 threads", BUFFER_RECYCLE_BENCH_REPEATS,
              2U * BUFFER_RECYCLE_BENCH_ITERS) {
      buffer_recycle_bench_parallel(2U, 0, 0);
    }
    benchmark("shared pool recycle: 4 threads", BUFFER_RECYCLE_BENCH_REPEATS,
              4U * BUFFER_RECYCLE_BENCH_ITERS) {
      buffer_recycle_bench_parallel(4U, 0, 0);
    }
    benchmark("shared pool recycle: 8 threads", BUFFER_RECYCLE_BENCH_REPEATS,
              8U * BUFFER_RECYCLE_BENCH_ITERS) {
      buffer_recycle_bench_parallel(8U, 0, 0);
    }
    benchmark("owner-local pool recycle: 1 thread", BUFFER_RECYCLE_BENCH_REPEATS,
              BUFFER_RECYCLE_BENCH_ITERS) {
      buffer_recycle_bench_parallel(1U, 1, 0);
    }
    benchmark("owner-local pool recycle: 2 threads", BUFFER_RECYCLE_BENCH_REPEATS,
              2U * BUFFER_RECYCLE_BENCH_ITERS) {
      buffer_recycle_bench_parallel(2U, 1, 0);
    }
    benchmark("owner-local pool recycle: 4 threads", BUFFER_RECYCLE_BENCH_REPEATS,
              4U * BUFFER_RECYCLE_BENCH_ITERS) {
      buffer_recycle_bench_parallel(4U, 1, 0);
    }
    benchmark("owner-local pool recycle: 8 threads", BUFFER_RECYCLE_BENCH_REPEATS,
              8U * BUFFER_RECYCLE_BENCH_ITERS) {
      buffer_recycle_bench_parallel(8U, 1, 0);
    }
    check_size_eq(atomic_load_explicit(&g_memory_bench_failures, memory_order_relaxed), 0U);
  }

  bench("Concurrent slab allocation") {
    benchmark("shared slab fixed: 1 thread", SLAB_ALLOC_BENCH_REPEATS,
              SLAB_ALLOC_BENCH_ITERS) {
      slab_alloc_bench_parallel(1U, 0, 0);
    }
    benchmark("shared slab fixed: 2 threads", SLAB_ALLOC_BENCH_REPEATS,
              2U * SLAB_ALLOC_BENCH_ITERS) {
      slab_alloc_bench_parallel(2U, 0, 0);
    }
    benchmark("shared slab fixed: 4 threads", SLAB_ALLOC_BENCH_REPEATS,
              4U * SLAB_ALLOC_BENCH_ITERS) {
      slab_alloc_bench_parallel(4U, 0, 0);
    }
    benchmark("shared slab fixed: 8 threads", SLAB_ALLOC_BENCH_REPEATS,
              8U * SLAB_ALLOC_BENCH_ITERS) {
      slab_alloc_bench_parallel(8U, 0, 0);
    }
    benchmark("owner-local slab fixed: 1 thread", SLAB_ALLOC_BENCH_REPEATS,
              SLAB_ALLOC_BENCH_ITERS) {
      slab_alloc_bench_parallel(1U, 1, 0);
    }
    benchmark("owner-local slab fixed: 2 threads", SLAB_ALLOC_BENCH_REPEATS,
              2U * SLAB_ALLOC_BENCH_ITERS) {
      slab_alloc_bench_parallel(2U, 1, 0);
    }
    benchmark("owner-local slab fixed: 4 threads", SLAB_ALLOC_BENCH_REPEATS,
              4U * SLAB_ALLOC_BENCH_ITERS) {
      slab_alloc_bench_parallel(4U, 1, 0);
    }
    benchmark("owner-local slab fixed: 8 threads", SLAB_ALLOC_BENCH_REPEATS,
              8U * SLAB_ALLOC_BENCH_ITERS) {
      slab_alloc_bench_parallel(8U, 1, 0);
    }
    benchmark("shared slab mixed: 1 thread", SLAB_ALLOC_BENCH_REPEATS,
              SLAB_ALLOC_BENCH_ITERS) {
      slab_alloc_bench_parallel(1U, 0, 1);
    }
    benchmark("shared slab mixed: 2 threads", SLAB_ALLOC_BENCH_REPEATS,
              2U * SLAB_ALLOC_BENCH_ITERS) {
      slab_alloc_bench_parallel(2U, 0, 1);
    }
    benchmark("shared slab mixed: 4 threads", SLAB_ALLOC_BENCH_REPEATS,
              4U * SLAB_ALLOC_BENCH_ITERS) {
      slab_alloc_bench_parallel(4U, 0, 1);
    }
    benchmark("shared slab mixed: 8 threads", SLAB_ALLOC_BENCH_REPEATS,
              8U * SLAB_ALLOC_BENCH_ITERS) {
      slab_alloc_bench_parallel(8U, 0, 1);
    }
    benchmark("owner-local slab mixed: 1 thread", SLAB_ALLOC_BENCH_REPEATS,
              SLAB_ALLOC_BENCH_ITERS) {
      slab_alloc_bench_parallel(1U, 1, 1);
    }
    benchmark("owner-local slab mixed: 2 threads", SLAB_ALLOC_BENCH_REPEATS,
              2U * SLAB_ALLOC_BENCH_ITERS) {
      slab_alloc_bench_parallel(2U, 1, 1);
    }
    benchmark("owner-local slab mixed: 4 threads", SLAB_ALLOC_BENCH_REPEATS,
              4U * SLAB_ALLOC_BENCH_ITERS) {
      slab_alloc_bench_parallel(4U, 1, 1);
    }
    benchmark("owner-local slab mixed: 8 threads", SLAB_ALLOC_BENCH_REPEATS,
              8U * SLAB_ALLOC_BENCH_ITERS) {
      slab_alloc_bench_parallel(8U, 1, 1);
    }
    check_size_eq(mem_pool_total_used(&g_memory_bench_pool), 0U);
    check_size_eq(atomic_load_explicit(&g_memory_bench_failures, memory_order_relaxed), 0U);
  }

  bench("Container allocation paths") {
    benchmark("heap push/pop: 1024 x 512B", CONTAINER_BENCH_ITERS,
              CONTAINER_BENCH_ITEMS * 2U) {
      turbo_heap_t heap;
      memory_bench_heap_item_t item;
      memory_bench_heap_item_t out;
      size_t i;
      (void)turbo_heap_init(&heap, sizeof(item), memory_bench_heap_compare, NULL);
      memset(&item, 0x5a, sizeof(item));
      for (i = 0; i < CONTAINER_BENCH_ITEMS; ++i) {
        item.priority = (uint32_t)(CONTAINER_BENCH_ITEMS - i);
        (void)turbo_heap_push(&heap, &item);
      }
      while (turbo_heap_pop(&heap, &out) == TURBO_OK) {
        g_memory_bench_sink ^= out.priority;
      }
      turbo_heap_destroy(&heap);
    }

    benchmark("deque wrapped growth: 1024 ints", CONTAINER_BENCH_ITERS,
              CONTAINER_BENCH_ITEMS) {
      turbo_deque_t deque;
      int value;
      size_t i;
      (void)turbo_deque_init(&deque, sizeof(value));
      (void)turbo_deque_reserve(&deque, CONTAINER_BENCH_ITEMS);
      for (i = 0; i < CONTAINER_BENCH_ITEMS; ++i) {
        value = (int)i;
        (void)turbo_deque_push_back(&deque, &value);
      }
      for (i = 0; i < CONTAINER_BENCH_ITEMS / 2U; ++i) {
        (void)turbo_deque_pop_front(&deque, &value);
      }
      for (i = 0; i < CONTAINER_BENCH_ITEMS / 2U; ++i) {
        value = (int)(CONTAINER_BENCH_ITEMS + i);
        (void)turbo_deque_push_back(&deque, &value);
      }
      (void)turbo_deque_reserve(&deque, CONTAINER_BENCH_ITEMS * 2U);
      g_memory_bench_sink ^= (uintptr_t)turbo_deque_back(&deque);
      turbo_deque_destroy(&deque);
    }

    benchmark("hash insert with growth: 1024 pairs", CONTAINER_BENCH_ITERS,
              CONTAINER_BENCH_ITEMS) {
      turbo_hash_map_t map;
      uint64_t key;
      uint64_t value;
      size_t i;
      (void)turbo_hash_map_init(&map, sizeof(key), sizeof(value), NULL, NULL, NULL);
      for (i = 0; i < CONTAINER_BENCH_ITEMS; ++i) {
        key = (uint64_t)i;
        value = key * 2U;
        (void)turbo_hash_map_put(&map, &key, &value);
      }
      key = CONTAINER_BENCH_ITEMS - 1U;
      g_memory_bench_sink ^= (uintptr_t)turbo_hash_map_get(&map, &key);
      turbo_hash_map_destroy(&map);
    }
  }
}
