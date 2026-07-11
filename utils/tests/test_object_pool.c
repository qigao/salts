#include "object_pool.h"
#include "tinytest.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Test structure
typedef struct {
    int id;
    double value;
    char name[32];
} test_object_t;

enum {
    OBJECT_POOL_BENCH_ITERATIONS = 10,
    OBJECT_POOL_BENCH_1K = 1000,
    OBJECT_POOL_BENCH_10K = 10000,
    OBJECT_POOL_BENCH_100K = 100000
};

static object_pool_t *benchmark_pool_create(size_t count) {
    object_pool_config_t config = {
        .object_size = sizeof(test_object_t),
        .initial_capacity = count,
        .max_capacity = count,
        .zero_on_alloc = false
    };
    object_pool_t *pool = object_pool_create(&config);
    if (!pool) abort();
    return pool;
}

static void **benchmark_objects_create(size_t count) {
    void **objects = (void **)malloc(count * sizeof(*objects));
    if (!objects) abort();
    return objects;
}

static void benchmark_interleaved(size_t count) {
    object_pool_t *pool = benchmark_pool_create(count);
    for (size_t i = 0; i < count; ++i) {
        void *obj = object_pool_alloc(pool);
        if (!obj) abort();
        object_pool_free(pool, obj);
    }
    object_pool_destroy(pool);
}

static void benchmark_allocate_then_free(size_t count) {
    object_pool_t *pool = benchmark_pool_create(count);
    void **objects = benchmark_objects_create(count);
    for (size_t i = 0; i < count; ++i) {
        objects[i] = object_pool_alloc(pool);
        if (!objects[i]) abort();
    }
    for (size_t i = 0; i < count; ++i) {
        object_pool_free(pool, objects[i]);
    }
    free(objects);
    object_pool_destroy(pool);
}

static uint32_t benchmark_random_next(uint32_t *state) {
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static void benchmark_randomized(size_t count) {
    object_pool_t *pool = benchmark_pool_create(count);
    void **objects = benchmark_objects_create(count);
    size_t active = 0;
    size_t allocated = 0;
    size_t freed = 0;
    uint32_t random_state = 0x9e3779b9u;

    while (allocated < count || freed < count) {
        bool should_allocate = active == 0 ||
                               (allocated < count && (benchmark_random_next(&random_state) & 1u));
        if (should_allocate) {
            objects[active] = object_pool_alloc(pool);
            if (!objects[active]) abort();
            active++;
            allocated++;
        } else {
            size_t index = benchmark_random_next(&random_state) % active;
            object_pool_free(pool, objects[index]);
            objects[index] = objects[active - 1];
            active--;
            freed++;
        }
    }

    free(objects);
    object_pool_destroy(pool);
}

suite("ObjectPool") {
    group("Basic Operations") {
        it("creates pool with initial capacity") {
            object_pool_config_t config = {
                .object_size = sizeof(test_object_t),
                .initial_capacity = 10,
                .max_capacity = 0,
                .zero_on_alloc = false
            };

            object_pool_t *pool = object_pool_create(&config);
            check(pool != NULL);
            check(object_pool_capacity(pool) >= 10);
            check(object_pool_allocated_count(pool) == 0);
            check(object_pool_free_count(pool) >= 10);

            object_pool_destroy(pool);
        }

        it("allocates and frees single object") {
            object_pool_config_t config = {
                .object_size = sizeof(test_object_t),
                .initial_capacity = 10,
                .max_capacity = 0,
                .zero_on_alloc = false
            };

            object_pool_t *pool = object_pool_create(&config);

            test_object_t *obj = (test_object_t *)object_pool_alloc(pool);
            check(obj != NULL);
            check(object_pool_allocated_count(pool) == 1);
            check(object_pool_free_count(pool) >= 9);

            obj->id = 42;
            obj->value = 3.14;
            strcpy(obj->name, "test");

            object_pool_free(pool, obj);
            check(object_pool_allocated_count(pool) == 0);
            check(object_pool_free_count(pool) >= 10);

            object_pool_destroy(pool);
        }

        it("reuses freed objects") {
            object_pool_config_t config = {
                .object_size = sizeof(test_object_t),
                .initial_capacity = 1,
                .max_capacity = 0,
                .zero_on_alloc = false
            };

            object_pool_t *pool = object_pool_create(&config);

            void *obj1 = object_pool_alloc(pool);
            void *addr1 = obj1;
            object_pool_free(pool, obj1);

            void *obj2 = object_pool_alloc(pool);
            void *addr2 = obj2;

            // Should reuse the same memory
            check(addr1 == addr2);

            object_pool_free(pool, obj2);
            object_pool_destroy(pool);
        }

        it("zeroes memory when requested") {
            object_pool_config_t config = {
                .object_size = sizeof(test_object_t),
                .initial_capacity = 1,
                .max_capacity = 0,
                .zero_on_alloc = true
            };

            object_pool_t *pool = object_pool_create(&config);

            test_object_t *obj = (test_object_t *)object_pool_alloc(pool);
            check(obj != NULL);

            // Check all bytes are zero
            unsigned char *bytes = (unsigned char *)obj;
            bool all_zero = true;
            for (size_t i = 0; i < sizeof(test_object_t); ++i) {
                if (bytes[i] != 0) {
                    all_zero = false;
                    break;
                }
            }
            check(all_zero);

            object_pool_free(pool, obj);
            object_pool_destroy(pool);
        }
    }

    group("Auto-Growing") {
        it("grows when free list is empty") {
            object_pool_config_t config = {
                .object_size = sizeof(test_object_t),
                .initial_capacity = 2,
                .max_capacity = 0,
                .zero_on_alloc = false
            };

            object_pool_t *pool = object_pool_create(&config);
            size_t initial_capacity = object_pool_capacity(pool);

            // Allocate all initial objects
            void *obj1 = object_pool_alloc(pool);
            void *obj2 = object_pool_alloc(pool);
            check(object_pool_allocated_count(pool) == 2);

            // This should trigger growth
            void *obj3 = object_pool_alloc(pool);
            check(obj3 != NULL);
            check(object_pool_capacity(pool) > initial_capacity);
            check(object_pool_allocated_count(pool) == 3);

            object_pool_free(pool, obj1);
            object_pool_free(pool, obj2);
            object_pool_free(pool, obj3);
            object_pool_destroy(pool);
        }

        it("respects max capacity") {
            object_pool_config_t config = {
                .object_size = sizeof(test_object_t),
                .initial_capacity = 2,
                .max_capacity = 2,
                .zero_on_alloc = false
            };

            object_pool_t *pool = object_pool_create(&config);

            void *obj1 = object_pool_alloc(pool);
            void *obj2 = object_pool_alloc(pool);
            check(obj1 != NULL);
            check(obj2 != NULL);

            // Should fail - at max capacity
            void *obj3 = object_pool_alloc(pool);
            check(obj3 == NULL);

            // Free one and try again
            object_pool_free(pool, obj1);
            void *obj4 = object_pool_alloc(pool);
            check(obj4 != NULL);

            object_pool_free(pool, obj2);
            object_pool_free(pool, obj4);
            object_pool_destroy(pool);
        }
    }

    group("Statistics") {
        it("tracks allocated count") {
            object_pool_config_t config = {
                .object_size = sizeof(test_object_t),
                .initial_capacity = 10,
                .max_capacity = 0,
                .zero_on_alloc = false
            };

            object_pool_t *pool = object_pool_create(&config);

            check(object_pool_allocated_count(pool) == 0);

            void *obj1 = object_pool_alloc(pool);
            check(object_pool_allocated_count(pool) == 1);

            void *obj2 = object_pool_alloc(pool);
            check(object_pool_allocated_count(pool) == 2);

            object_pool_free(pool, obj1);
            check(object_pool_allocated_count(pool) == 1);

            object_pool_free(pool, obj2);
            check(object_pool_allocated_count(pool) == 0);

            object_pool_destroy(pool);
        }

        it("tracks peak usage") {
            object_pool_config_t config = {
                .object_size = sizeof(test_object_t),
                .initial_capacity = 10,
                .max_capacity = 0,
                .zero_on_alloc = false
            };

            object_pool_t *pool = object_pool_create(&config);

            void *objs[5];
            for (int i = 0; i < 5; ++i) {
                objs[i] = object_pool_alloc(pool);
            }
            check(object_pool_peak_usage(pool) == 5);

            // Free some
            object_pool_free(pool, objs[0]);
            object_pool_free(pool, objs[1]);
            check(object_pool_peak_usage(pool) == 5);  // Peak unchanged

            // Allocate more
            void *obj6 = object_pool_alloc(pool);
            void *obj7 = object_pool_alloc(pool);
            void *obj8 = object_pool_alloc(pool);
            check(object_pool_peak_usage(pool) == 6);

            for (int i = 2; i < 5; ++i) {
                object_pool_free(pool, objs[i]);
            }
            object_pool_free(pool, obj6);
            object_pool_free(pool, obj7);
            object_pool_free(pool, obj8);
            object_pool_destroy(pool);
        }

        it("resets statistics") {
            object_pool_config_t config = {
                .object_size = sizeof(test_object_t),
                .initial_capacity = 10,
                .max_capacity = 0,
                .zero_on_alloc = false
            };

            object_pool_t *pool = object_pool_create(&config);

            void *obj1 = object_pool_alloc(pool);
            void *obj2 = object_pool_alloc(pool);
            check(object_pool_peak_usage(pool) == 2);

            object_pool_free(pool, obj1);
            object_pool_free(pool, obj2);

            object_pool_reset_stats(pool);
            check(object_pool_peak_usage(pool) == 0);

            object_pool_destroy(pool);
        }
    }

    group("Edge Cases") {
        it("handles NULL pool gracefully") {
            check(object_pool_alloc(NULL) == NULL);
            object_pool_free(NULL, (void *)0x1234);  // Should not crash
            check(object_pool_allocated_count(NULL) == 0);
            check(object_pool_free_count(NULL) == 0);
            check(object_pool_capacity(NULL) == 0);
            check(object_pool_peak_usage(NULL) == 0);
            object_pool_reset_stats(NULL);  // Should not crash
            object_pool_destroy(NULL);  // Should not crash
        }

        it("rejects invalid config") {
            object_pool_config_t config = {
                .object_size = sizeof(void *) - 1,  // Too small
                .initial_capacity = 10,
                .max_capacity = 0,
                .zero_on_alloc = false
            };

            object_pool_t *pool = object_pool_create(&config);
            check(pool == NULL);
        }

        it("handles zero initial capacity") {
            object_pool_config_t config = {
                .object_size = sizeof(test_object_t),
                .initial_capacity = 0,
                .max_capacity = 0,
                .zero_on_alloc = false
            };

            object_pool_t *pool = object_pool_create(&config);
            check(pool != NULL);
            check(object_pool_capacity(pool) == 0);

            // Should auto-grow on first allocation
            void *obj = object_pool_alloc(pool);
            check(obj != NULL);
            check(object_pool_capacity(pool) > 0);

            object_pool_free(pool, obj);
            object_pool_destroy(pool);
        }

        it("rejects object size alignment overflow") {
            object_pool_config_t config = {
                .object_size = SIZE_MAX - 3,
                .initial_capacity = 1,
                .max_capacity = 0,
                .zero_on_alloc = false
            };

            object_pool_t *pool = object_pool_create(&config);
            check(pool == NULL);
        }

        it("rejects chunk byte size overflow") {
            object_pool_config_t config = {
                .object_size = SIZE_MAX / 2 + 1,
                .initial_capacity = 2,
                .max_capacity = 0,
                .zero_on_alloc = false
            };

            object_pool_t *pool = object_pool_create(&config);
            check(pool == NULL);
        }

    }

    group("Stress Tests") {
        it("handles many allocations") {
            object_pool_config_t config = {
                .object_size = sizeof(test_object_t),
                .initial_capacity = 100,
                .max_capacity = 0,
                .zero_on_alloc = false
            };

            object_pool_t *pool = object_pool_create(&config);

            void *objs[1000];
            for (int i = 0; i < 1000; ++i) {
                objs[i] = object_pool_alloc(pool);
                check(objs[i] != NULL);
            }

            check(object_pool_allocated_count(pool) == 1000);
            check(object_pool_peak_usage(pool) == 1000);

            for (int i = 0; i < 1000; ++i) {
                object_pool_free(pool, objs[i]);
            }

            check(object_pool_allocated_count(pool) == 0);

            object_pool_destroy(pool);
        }

        it("handles random alloc/free pattern") {
            object_pool_config_t config = {
                .object_size = sizeof(test_object_t),
                .initial_capacity = 10,
                .max_capacity = 0,
                .zero_on_alloc = false
            };

            object_pool_t *pool = object_pool_create(&config);

            void *objs[100] = {0};
            size_t allocated = 0;

            // Random pattern: allocate/free
            for (int i = 0; i < 1000; ++i) {
                if (i % 3 == 0 && allocated < 100) {
                    // Allocate
                    objs[allocated] = object_pool_alloc(pool);
                    check(objs[allocated] != NULL);
                    allocated++;
                } else if (allocated > 0) {
                    // Free
                    allocated--;
                    object_pool_free(pool, objs[allocated]);
                    objs[allocated] = NULL;
                }

                check(object_pool_allocated_count(pool) == allocated);
            }

            // Clean up remaining
            for (size_t i = 0; i < allocated; ++i) {
                object_pool_free(pool, objs[i]);
            }

            object_pool_destroy(pool);
        }
    }

    bench("Performance") {
        benchmark("interleaved alloc/free 1K", OBJECT_POOL_BENCH_ITERATIONS,
                  2.0 * OBJECT_POOL_BENCH_1K) { benchmark_interleaved(OBJECT_POOL_BENCH_1K); }
        benchmark("interleaved alloc/free 10K", OBJECT_POOL_BENCH_ITERATIONS,
                  2.0 * OBJECT_POOL_BENCH_10K) { benchmark_interleaved(OBJECT_POOL_BENCH_10K); }
        benchmark("interleaved alloc/free 100K", OBJECT_POOL_BENCH_ITERATIONS,
                  2.0 * OBJECT_POOL_BENCH_100K) { benchmark_interleaved(OBJECT_POOL_BENCH_100K); }

        benchmark("allocate N then free all 1K", OBJECT_POOL_BENCH_ITERATIONS,
                  2.0 * OBJECT_POOL_BENCH_1K) { benchmark_allocate_then_free(OBJECT_POOL_BENCH_1K); }
        benchmark("allocate N then free all 10K", OBJECT_POOL_BENCH_ITERATIONS,
                  2.0 * OBJECT_POOL_BENCH_10K) { benchmark_allocate_then_free(OBJECT_POOL_BENCH_10K); }
        benchmark("allocate N then free all 100K", OBJECT_POOL_BENCH_ITERATIONS,
                  2.0 * OBJECT_POOL_BENCH_100K) { benchmark_allocate_then_free(OBJECT_POOL_BENCH_100K); }

        benchmark("randomized allocate/free 1K", OBJECT_POOL_BENCH_ITERATIONS,
                  2.0 * OBJECT_POOL_BENCH_1K) { benchmark_randomized(OBJECT_POOL_BENCH_1K); }
        benchmark("randomized allocate/free 10K", OBJECT_POOL_BENCH_ITERATIONS,
                  2.0 * OBJECT_POOL_BENCH_10K) { benchmark_randomized(OBJECT_POOL_BENCH_10K); }
        benchmark("randomized allocate/free 100K", OBJECT_POOL_BENCH_ITERATIONS,
                  2.0 * OBJECT_POOL_BENCH_100K) { benchmark_randomized(OBJECT_POOL_BENCH_100K); }
    }
}
