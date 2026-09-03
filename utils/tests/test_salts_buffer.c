#include "salts_buffer.h"
#include "tinytest.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

static atomic_int g_external_free_calls;
static atomic_size_t g_external_stress_free_calls;
static void *g_external_free_data;
static void *g_external_free_user;

#define TEST_SLAB_SIZE_CLASSES 9U
#define TEST_SLAB_LOCAL_MAGAZINE_SLOTS 1U
static const size_t g_test_slab_sizes[TEST_SLAB_SIZE_CLASSES] = {
    16U, 48U, 112U, 240U, 496U, 1008U, 2032U, 4080U, 8176U};

static void external_free_probe(void *data, void *user_data) {
    g_external_free_data = data;
    g_external_free_user = user_data;
    atomic_fetch_add_explicit(&g_external_free_calls, 1, memory_order_relaxed);
}

static void external_stress_free_probe(void *data, void *user_data) {
    (void)data;
    (void)user_data;
    atomic_fetch_add_explicit(&g_external_stress_free_calls, 1U, memory_order_relaxed);
}

/* 
 * Linus's Law: Keep it simple.
 * StressWorker is at file scope to satisfy C standard.
 */
#ifdef _WIN32
static DWORD WINAPI StressWorker(LPVOID lpParam) {
    mem_pool_t* pool = (mem_pool_t*)lpParam;
    for (int i = 0; i < 1000; i++) {
        unsigned char external = (unsigned char)i;
        void* p = mem_alloc(pool, (i % 8) * 64 + 1);
        if (p) mem_free(pool, p);
        
        mem_buffer_t* b = mem_get_buffer(pool, (i % 4) * 128 + 1);
        if (b) mem_release(b);

        b = mem_wrap_external(&external, sizeof(external), external_stress_free_probe, NULL);
        if (b) mem_release(b);
    }
    return 0;
}
#else
static void* StressWorker(void* arg) {
    mem_pool_t* pool = (mem_pool_t*)arg;
    for (int i = 0; i < 1000; i++) {
        unsigned char external = (unsigned char)i;
        void* p = mem_alloc(pool, (i % 8) * 64 + 1);
        if (p) mem_free(pool, p);
        
        mem_buffer_t* b = mem_get_buffer(pool, (i % 4) * 128 + 1);
        if (b) mem_release(b);

        b = mem_wrap_external(&external, sizeof(external), external_stress_free_probe, NULL);
        if (b) mem_release(b);
    }
    return NULL;
}
#endif

spec("Salts Buffer (mem_pool_t) Tests") {
  
  it("should maintain ABI stability (112 bytes)") {
    check_equal(sizeof(mem_pool_t), 112);
  }

  it("should initialize and destroy a pool") {
    mem_pool_t pool;
    int rc = mem_init(&pool, 0);
    check_equal((size_t)rc, 0);
    
    check_equal(mem_pool_total_allocated(&pool), 0);
    check_equal(mem_pool_total_used(&pool), 0);
    
    mem_destroy(&pool);
  }

  it("should perform slab allocations (small)") {
    mem_pool_t pool;
    mem_init(&pool, 0);
    
    void* p1 = mem_alloc(&pool, 32);
    check_not_null(p1);
    
    void* p2 = mem_alloc(&pool, 32);
    check_not_null(p2);
    check_not_equal((const void *)(p1), (const void *)(p2));
    
    check_greater(mem_pool_total_allocated(&pool), 0);
    check_greater(mem_pool_total_used(&pool), 64);
    
    mem_free(&pool, p1);
    mem_free(&pool, p2);
    
    mem_destroy(&pool);
  }

  it("should recycle every slab size class and drain magazines on trim") {
    mem_pool_t pool;
    void* first[TEST_SLAB_SIZE_CLASSES] = {0};
    void* burst[TEST_SLAB_LOCAL_MAGAZINE_SLOTS + 1U] = {0};
    size_t i;

    mem_init(&pool, 0);
    for (i = 0; i < TEST_SLAB_SIZE_CLASSES; ++i) {
      first[i] = mem_alloc(&pool, g_test_slab_sizes[i]);
      check_not_null(first[i]);
    }
    for (i = 0; i < TEST_SLAB_SIZE_CLASSES; ++i) mem_free(&pool, first[i]);
    check_equal(mem_pool_total_used(&pool), 0U);

    for (i = 0; i < TEST_SLAB_SIZE_CLASSES; ++i) {
      void* recycled = mem_alloc(&pool, g_test_slab_sizes[i]);
      check_equal((const void *)(recycled), (const void *)(first[i]));
      mem_free(&pool, recycled);
    }
    check_greater(mem_pool_total_allocated(&pool), 0U);
    mem_trim(&pool);
    check_equal(mem_pool_total_allocated(&pool), 0U);

    for (i = 0; i < TEST_SLAB_LOCAL_MAGAZINE_SLOTS + 1U; ++i) {
      burst[i] = mem_alloc(&pool, 64U);
      check_not_null(burst[i]);
    }
    for (i = 0; i < TEST_SLAB_LOCAL_MAGAZINE_SLOTS + 1U; ++i) mem_free(&pool, burst[i]);
    check_equal(mem_pool_total_used(&pool), 0U);
    mem_trim(&pool);
    check_equal(mem_pool_total_allocated(&pool), 0U);
    mem_destroy(&pool);
  }

  it("should isolate thread-local slab hints between pools") {
    mem_pool_t first_pool;
    mem_pool_t second_pool;
    void* first;
    void* second;
    void* recycled;

    mem_init(&first_pool, 0);
    mem_init(&second_pool, 0);
    first = mem_alloc(&first_pool, 64U);
    second = mem_alloc(&second_pool, 64U);
    check_not_null(first);
    check_not_null(second);
    mem_free(&first_pool, first);
    mem_free(&second_pool, second);

    recycled = mem_alloc(&first_pool, 64U);
    check_equal((const void *)(recycled), (const void *)(first));
    mem_free(&first_pool, recycled);
    mem_trim(&first_pool);
    mem_trim(&second_pool);
    check_equal(mem_pool_total_allocated(&first_pool), 0U);
    check_equal(mem_pool_total_allocated(&second_pool), 0U);
    mem_destroy(&first_pool);
    mem_destroy(&second_pool);
  }

  it("should perform oversize allocations (large)") {
    mem_pool_t pool;
    mem_init(&pool, 0);
    
    size_t large_size = 16 * 1024;
    void* p = mem_alloc(&pool, large_size);
    check_not_null(p);
    
    check_greater_equal(mem_pool_total_used(&pool), large_size);
    
    mem_free(&pool, p);
    mem_destroy(&pool);
  }

  it("should retain slabs across reset until explicitly trimmed") {
    mem_pool_t pool;
    size_t allocated_before_reset;
    void* first;
    void* reused;

    mem_init(&pool, 0);
    first = mem_alloc(&pool, 32);
    check_not_null(first);
    allocated_before_reset = mem_pool_total_allocated(&pool);
    check_greater(allocated_before_reset, 0);

    mem_reset(&pool);
    check_equal(mem_pool_total_used(&pool), 0);
    check_equal(mem_pool_total_allocated(&pool), allocated_before_reset);

    reused = mem_alloc(&pool, 32);
    check_not_null(reused);
    check_equal(mem_pool_total_allocated(&pool), allocated_before_reset);
    mem_free(&pool, reused);
    mem_trim(&pool);
    check_equal(mem_pool_total_allocated(&pool), 0);
    mem_destroy(&pool);
  }

  it("should reject overflowing allocation sizes") {
    mem_pool_t pool;

    mem_init(&pool, 0);
    check_null(mem_alloc(&pool, SIZE_MAX));
    check_null(mem_alloc_array(&pool, sizeof(uint64_t), SIZE_MAX));
    check_null(MEM_ALLOC_ARRAY(&pool, uint64_t, SIZE_MAX));
    check_null(mem_get_buffer(&pool, SIZE_MAX));
    mem_destroy(&pool);
  }

  it("should support buffer refcounting") {
    mem_pool_t pool;
    mem_init(&pool, 0);
    
    mem_buffer_t* buffer = mem_get_buffer(&pool, 100);
    check_not_null(buffer);
    check_equal((size_t)mem_buffer_ref_count(buffer), 1);
    check_equal((const void *)(mem_buffer_pool(buffer)), (const void *)(&pool));
    
    mem_ref(buffer);
    check_equal((size_t)mem_buffer_ref_count(buffer), 2);
    
    mem_unref(buffer);
    check_equal((size_t)mem_buffer_ref_count(buffer), 1);

    check_equal((const void *)(mem_buffer_retain(buffer)), (const void *)(buffer));
    check_equal((size_t)mem_buffer_ref_count(buffer), 2);

    mem_buffer_release(buffer);
    check_equal((size_t)mem_buffer_ref_count(buffer), 1);
    
    mem_release(buffer);
    
    mem_destroy(&pool);
  }

  it("should support buffer recycling") {
    mem_pool_t pool;
    mem_init(&pool, 0);
    
    mem_buffer_t* b1 = mem_get_buffer(&pool, 100);
    void* data1 = mem_buffer_data(b1);
    b1->used = 17;
    mem_release(b1);
    
    mem_buffer_t* b2 = mem_get_buffer(&pool, 100);
    check_equal((const void *)(mem_buffer_data(b2)), (const void *)(data1));
    check_equal(mem_buffer_used(b2), 0);
    
    mem_release(b2);
    mem_destroy(&pool);
  }

  it("should bound and clear the internal buffer cache") {
    mem_pool_t pool;
    mem_buffer_t* buffers[MEM_BUFFER_RECYCLE_LIMIT + 8U] = {0};
    void* cache_metadata;
    size_t i;

    mem_init(&pool, 0);
    for (i = 0; i < sizeof(buffers) / sizeof(buffers[0]); ++i) {
      buffers[i] = mem_get_buffer(&pool, 64);
      check_not_null(buffers[i]);
    }
    for (i = 0; i < sizeof(buffers) / sizeof(buffers[0]); ++i) mem_buffer_release(buffers[i]);

    check_equal(mem_pool_recycle_count(&pool), MEM_BUFFER_RECYCLE_LIMIT);
    cache_metadata = pool.recycle_head;
    check_not_null(cache_metadata);
    mem_reset(&pool);
    check_equal(mem_pool_recycle_count(&pool), 0U);
    check_equal((const void *)(pool.recycle_head), (const void *)(cache_metadata));
    mem_destroy(&pool);
  }

  it("should support slicing") {
    mem_pool_t pool;
    mem_init(&pool, 0);
    
    mem_buffer_t* buffer = mem_get_buffer(&pool, 200);
    if (buffer) {
        strncpy(buffer->data, "Hello World from SaltsBuffer", 200);
        buffer->used = strlen(buffer->data) + 1;
        
        mem_slice_t slice = mem_slice(buffer, 6, 5);
        check_equal((const void *)(slice.data), (const void *)(mem_buffer_data(buffer) + 6));
        check_equal(slice.length, 5);
        check_equal((const void *)(slice.buffer), (const void *)(buffer));
        check_equal((size_t)mem_buffer_ref_count(buffer), 2);
        
        mem_slice_release(&slice);
        check_equal((size_t)mem_buffer_ref_count(buffer), 1);
        
        mem_release(buffer);
    }
    mem_destroy(&pool);
  }

  it("should handle external wrapping") {
    char data[] = "External static data";
    int user_data = 42;
    mem_buffer_t* buffer;

    atomic_store_explicit(&g_external_free_calls, 0, memory_order_relaxed);
    g_external_free_data = NULL;
    g_external_free_user = NULL;
    buffer = mem_wrap_external(data, sizeof(data), external_free_probe, &user_data);
    
    check_not_null(buffer);
    check_equal((size_t)mem_is_external(buffer), 1);
    check_equal((const void *)(mem_buffer_data(buffer)), (const void *)(data));
    
    mem_release(buffer);
    check_equal(atomic_load_explicit(&g_external_free_calls, memory_order_relaxed), 1);
    check_equal((const void *)(g_external_free_data), (const void *)(data));
    check_equal((const void *)(g_external_free_user), (const void *)(&user_data));
  }

  it("should fall back safely when the external wrapper cache is full") {
    mem_buffer_t *buffers[80] = {0};
    unsigned char data = 7;
    size_t i;

    atomic_store_explicit(&g_external_free_calls, 0, memory_order_relaxed);
    for (i = 0; i < sizeof(buffers) / sizeof(buffers[0]); ++i) {
      buffers[i] = mem_wrap_external(&data, sizeof(data), external_free_probe, NULL);
      check_not_null(buffers[i]);
    }
    for (i = 0; i < sizeof(buffers) / sizeof(buffers[0]); ++i) mem_buffer_release(buffers[i]);
    check_equal(atomic_load_explicit(&g_external_free_calls, memory_order_relaxed), 80);
  }

  it("should survive concurrency stress") {
    mem_pool_t pool;
    mem_init(&pool, 0);
    atomic_store_explicit(&g_external_stress_free_calls, 0U, memory_order_relaxed);
    
    const int num_threads = 8;
#ifdef _WIN32
    HANDLE threads[8];
    for (int i = 0; i < num_threads; i++) {
        threads[i] = CreateThread(NULL, 0, StressWorker, &pool, 0, NULL);
    }
    WaitForMultipleObjects(num_threads, threads, TRUE, INFINITE);
    for (int i = 0; i < num_threads; i++) CloseHandle(threads[i]);
#else
    pthread_t threads[8];
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, StressWorker, &pool);
    }
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
#endif
    
    check_greater(mem_pool_recycle_count(&pool), 0U);
    check_less_equal(mem_pool_recycle_count(&pool), MEM_BUFFER_RECYCLE_LIMIT);
    mem_destroy(&pool);
    check_equal(atomic_load_explicit(&g_external_stress_free_calls, memory_order_relaxed),
                  (size_t)num_threads * 1000U);
  }
}
