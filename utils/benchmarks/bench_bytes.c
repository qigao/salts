#include "salts_bytes.h"

#include "tinytest.h"
#include "salts_error.h"

#include <stdint.h>
#include <string.h>

enum {
  BYTE_BUFFER_BENCH_SAMPLES = 20000,
  BYTE_BUFFER_FRAGMENT_SAMPLES = 4000,
  BYTE_BUFFER_CHUNK_SIZE = 1024,
  BYTE_BUFFER_PACKET_SIZE = 64 * 1024,
  BYTE_BUFFER_FRAGMENT_COUNT = BYTE_BUFFER_PACKET_SIZE / BYTE_BUFFER_CHUNK_SIZE
};

static uint8_t byte_buffer_chunk[BYTE_BUFFER_CHUNK_SIZE];
static uint8_t byte_buffer_packet[BYTE_BUFFER_PACKET_SIZE];
static volatile size_t byte_buffer_sink;

spec("bounded byte buffer benchmarks") {
  before_all() {
    memset(byte_buffer_chunk, 0x5a, sizeof(byte_buffer_chunk));
    memset(byte_buffer_packet, 0xa5, sizeof(byte_buffer_packet));
  }

  bench("steady-state framing") {
    salts_bytes_t buffer = salts_bytes_INIT;
    salts_bytes_view_t view;
    int failures = 0;

    check_equal(salts_bytes_init(&buffer, BYTE_BUFFER_PACKET_SIZE), SALTS_OK);
    check_equal(salts_bytes_append(&buffer, byte_buffer_chunk, sizeof(byte_buffer_chunk)),
                 SALTS_OK);
    check_equal(salts_bytes_consume(&buffer, sizeof(byte_buffer_chunk)), SALTS_OK);

    benchmark_io("append+view+consume 1 KiB", BYTE_BUFFER_BENCH_SAMPLES, 3u,
                 BYTE_BUFFER_CHUNK_SIZE) {
      if (salts_bytes_append(&buffer, byte_buffer_chunk, sizeof(byte_buffer_chunk)) !=
              SALTS_OK ||
          salts_bytes_view(&buffer, &view) != SALTS_OK ||
          salts_bytes_consume(&buffer, sizeof(byte_buffer_chunk)) != SALTS_OK) {
        ++failures;
      } else {
        byte_buffer_sink += view.size;
      }
    }

    check_equal(failures, 0);
    check_equal(salts_bytes_size(&buffer), 0u);
    salts_bytes_destroy(&buffer);
  }

  bench("fragmented packet reassembly") {
    salts_bytes_t buffer = salts_bytes_INIT;
    salts_bytes_view_t view;
    int failures = 0;
    size_t i;

    check_equal(salts_bytes_init(&buffer, BYTE_BUFFER_PACKET_SIZE), SALTS_OK);
    check_equal(salts_bytes_append(&buffer, byte_buffer_packet, sizeof(byte_buffer_packet)),
                 SALTS_OK);
    check_equal(salts_bytes_consume(&buffer, sizeof(byte_buffer_packet)), SALTS_OK);

    benchmark_io("64 x 1 KiB append + view + consume", BYTE_BUFFER_FRAGMENT_SAMPLES,
                 BYTE_BUFFER_FRAGMENT_COUNT + 2u, BYTE_BUFFER_PACKET_SIZE) {
      for (i = 0u; i < BYTE_BUFFER_FRAGMENT_COUNT; ++i) {
        if (salts_bytes_append(&buffer, byte_buffer_chunk, sizeof(byte_buffer_chunk)) !=
            SALTS_OK)
          ++failures;
      }
      if (salts_bytes_view(&buffer, &view) != SALTS_OK ||
          salts_bytes_consume(&buffer, BYTE_BUFFER_PACKET_SIZE) != SALTS_OK) {
        ++failures;
      } else {
        byte_buffer_sink += view.size;
      }
    }

    check_equal(failures, 0);
    check_equal(salts_bytes_size(&buffer), 0u);
    salts_bytes_destroy(&buffer);
  }

  bench("deferred compaction") {
    salts_bytes_t buffer = salts_bytes_INIT;
    int failures = 0;

    check_equal(salts_bytes_init(&buffer, BYTE_BUFFER_PACKET_SIZE), SALTS_OK);
    check_equal(salts_bytes_append(&buffer, byte_buffer_packet, sizeof(byte_buffer_packet)),
                 SALTS_OK);
    check_equal(salts_bytes_consume(&buffer, sizeof(byte_buffer_packet)), SALTS_OK);

    benchmark_io("48 KiB append, 32 KiB consume, compact+append", BYTE_BUFFER_FRAGMENT_SAMPLES, 4u,
                 80u * 1024u) {
      if (salts_bytes_append(&buffer, byte_buffer_packet, 48u * 1024u) != SALTS_OK ||
          salts_bytes_consume(&buffer, 32u * 1024u) != SALTS_OK ||
          salts_bytes_append(&buffer, byte_buffer_packet, 32u * 1024u) != SALTS_OK ||
          salts_bytes_consume(&buffer, 48u * 1024u) != SALTS_OK) {
        ++failures;
      }
    }

    check_equal(failures, 0);
    check_equal(salts_bytes_size(&buffer), 0u);
    salts_bytes_destroy(&buffer);
  }
}
