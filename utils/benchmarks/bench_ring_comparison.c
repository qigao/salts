#include "ring_buffer.h"
#include "ring_buffer_spsc.h"
#include "tinytest.h"
#include "platform.h"
#include "salts_thread.h"
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define BUFFER_SIZE (1024 * 1024)  // 1MB
#define BENCH_ITERS (1024 * 1024)  // 1M operations
#define BENCH_ITERS_LARGE (10 * 1024 * 1024) // 10M operations

/************************** SINGLE-THREADED BENCHMARKS **************************/

static uint8_t *st_data;
static ring_data_type st_ring;

spec("Ring Buffer - Single Threaded") {
    before_each() {
        st_data = (uint8_t *)malloc(BUFFER_SIZE);
        ring_init(&st_ring, st_data, BUFFER_SIZE);
    }

    after_each() {
        free(st_data);
    }

    bench("Single Threaded Operations") {

        printf("\n=== Single-Threaded (ring_buffer.c) ===\n");

        benchmark_batch("Write+Read (64 bytes)", BENCH_ITERS) {
            uint8_t *p = ring_write_acquire(&st_ring, 64);
            if (p) {
                ring_write_release(&st_ring, 64);
                size_t avail = 0;
                uint8_t *rp = ring_read_acquire(&st_ring, &avail);
                if (rp) {
                    ring_read_release(&st_ring, (avail < 64) ? avail : 64);
                }
            }
        }

        benchmark_batch("Write+Read (256 bytes)", BENCH_ITERS) {
            uint8_t *p = ring_write_acquire(&st_ring, 256);
            if (p) {
                ring_write_release(&st_ring, 256);
                size_t avail = 0;
                uint8_t *rp = ring_read_acquire(&st_ring, &avail);
                if (rp) {
                    ring_read_release(&st_ring, (avail < 256) ? avail : 256);
                }
            }
        }

        benchmark_batch("Write+Read (1KB)", BENCH_ITERS) {
            uint8_t *p = ring_write_acquire(&st_ring, 1024);
            if (p) {
                ring_write_release(&st_ring, 1024);
                size_t avail = 0;
                uint8_t *rp = ring_read_acquire(&st_ring, &avail);
                if (rp) {
                    ring_read_release(&st_ring, (avail < 1024) ? avail : 1024);
                }
            }
        }
    }
}

/************************** SPSC BENCHMARKS **************************/

typedef struct {
    ring_spsc_t *ring;
    size_t count;
    size_t batch_size;
    bool use_memcpy;
    atomic_int start;
} spsc_ctx_t;

static void spsc_producer_thread(void *arg) {
    spsc_ctx_t *ctx = (spsc_ctx_t *)arg;
    uint8_t local_buf[4096];
    memset(local_buf, 0xAA, sizeof(local_buf));

    while (atomic_load(&ctx->start) == 0) {
        salts_sleep_ms(1);
    }

    size_t produced = 0;
    while (produced < ctx->count) {
        size_t to_write = (ctx->count - produced < ctx->batch_size)
                          ? (ctx->count - produced) : ctx->batch_size;

        uint8_t *p = ring_spsc_write_acquire(ctx->ring, to_write);
        if (p) {
            if (ctx->use_memcpy) {
                memcpy(p, local_buf, to_write);
            }
            ring_spsc_write_release(ctx->ring, to_write);
            produced += to_write;
        } else {
            salts_sleep_ms(0);
        }
    }
}

static void spsc_consumer_thread(void *arg) {
    spsc_ctx_t *ctx = (spsc_ctx_t *)arg;
    uint8_t local_buf[4096];

    while (atomic_load(&ctx->start) == 0) {
        salts_sleep_ms(1);
    }

    size_t consumed = 0;
    while (consumed < ctx->count) {
        size_t available = 0;
        uint8_t *p = ring_spsc_read_acquire(ctx->ring, &available);
        if (p) {
            size_t to_read = (available < ctx->batch_size) ? available : ctx->batch_size;
            if (ctx->use_memcpy) {
                memcpy(local_buf, p, to_read);
            } else {
                volatile uint8_t sink = p[0];
                (void)sink;
            }
            ring_spsc_read_release(ctx->ring, to_read);
            consumed += to_read;
        } else {
            salts_sleep_ms(0);
        }
    }
}

static void run_spsc_bench(size_t count, size_t batch, bool use_memcpy) {
    uint8_t *data = (uint8_t *)malloc(BUFFER_SIZE);
    ring_spsc_t ring;

    if (!ring_spsc_init(&ring, data, BUFFER_SIZE)) {
        printf("ERROR: Failed to init SPSC ring (size must be power of 2)\n");
        free(data);
        return;
    }

    spsc_ctx_t ctx;
    ctx.ring = &ring;
    ctx.count = count;
    ctx.batch_size = batch;
    ctx.use_memcpy = use_memcpy;
    atomic_store(&ctx.start, 0);

    salts_thread_t prod, cons;
    salts_thread_create(&prod, spsc_producer_thread, &ctx);
    salts_thread_create(&cons, spsc_consumer_thread, &ctx);

    salts_sleep_ms(50);
    atomic_store(&ctx.start, 1);

    salts_thread_join(&prod);
    salts_thread_join(&cons);
    free(data);
}

spec("Ring Buffer SPSC - Multithreaded") {
    bench("SPSC Thread-Safe Benchmarks") {
        printf("\n=== SPSC (Single-Producer Single-Consumer) ===\n");
        benchmark_ops("SPSC Batch 64B", 1, BENCH_ITERS) {
            run_spsc_bench(BENCH_ITERS, 64, false);
        }
        benchmark_ops("SPSC Batch 256B", 1, BENCH_ITERS) {
            run_spsc_bench(BENCH_ITERS, 256, false);
        }
        benchmark_ops("SPSC Batch 1KB", 1, BENCH_ITERS) {
            run_spsc_bench(BENCH_ITERS, 1024, false);
        }
        benchmark_ops("SPSC No Batch (1B)", 1, 100000) {
            run_spsc_bench(100000, 1, false);
        }
        benchmark_ops("SPSC High Throughput (128B)", 1, BENCH_ITERS_LARGE) {
            run_spsc_bench(BENCH_ITERS_LARGE, 128, false);
        }
        benchmark_ops("SPSC with Memcpy (512B)", 1, BENCH_ITERS) {
            run_spsc_bench(BENCH_ITERS, 512, true);
        }
        benchmark_ops("SPSC with Memcpy (4KB)", 1, BENCH_ITERS) {
            run_spsc_bench(BENCH_ITERS, 4096, true);
        }
    }
}

/************************** COMPARISON SUMMARY **************************/

spec("Performance Comparison Summary") {
    it("should print comparison guide") {
        printf("\n");
        printf("╔════════════════════════════════════════════════════════════════╗\n");
        printf("║          Ring Buffer Performance Comparison                    ║\n");
        printf("╠════════════════════════════════════════════════════════════════╣\n");
        printf("║ Implementation      │ Use Case              │ Relative Speed  ║\n");
        printf("╠════════════════════════════════════════════════════════════════╣\n");
        printf("║ ring_buffer.c       │ Single-threaded       │ 1.0x (fastest)  ║\n");
        printf("║ ring_buffer_spsc.c  │ 1 producer + 1 cons   │ ~1.3x           ║\n");
        printf("╠════════════════════════════════════════════════════════════════╣\n");
        printf("║ For multiple producers/consumers, use disruptor.c             ║\n");
        printf("║ (See bench_disruptor.c for MPMC benchmarks)                   ║\n");
        printf("╠════════════════════════════════════════════════════════════════╣\n");
        printf("║ Recommendation: Use the simplest that meets your needs        ║\n");
        printf("╚════════════════════════════════════════════════════════════════╝\n");
        printf("\n");
    }
}
