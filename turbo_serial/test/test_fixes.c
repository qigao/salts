/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 TurboUtils Project
 *
 * Test suite validating fixes from turbo_serial review.
 */

#include "tinytest.h"
#include "turbo_serial.h"

#include <string.h>

suite("turbo_serial review fixes") {
  it("clamps io_chunk_size to buffer capacity") {
    turbo_serial_config_t config;
    turbo_serial_t *serial = NULL;

    turbo_serial_config_default(&config);
    config.rx_buffer_size = 16;
    config.tx_buffer_size = 16;
    /* io_chunk_size defaults to 256, which exceeds buffer size */
    check_equal(config.io_chunk_size, 256);

    /* Should succeed by clamping io_chunk_size internally */
    check_int_eq(turbo_serial_create(&serial, &config), TURBO_SERIAL_OK);
    check_not_null(serial);

    turbo_serial_destroy(serial);
  }

  it("rejects zero io_chunk_size") {
    turbo_serial_config_t config;
    turbo_serial_t *serial = NULL;

    turbo_serial_config_default(&config);
    config.io_chunk_size = 0;

    check_int_eq(turbo_serial_create(&serial, &config), TURBO_SERIAL_INVALID_VALUE);
    check_null(serial);
  }

  it("documents actual SPSC capacity correctly") {
    turbo_serial_config_t config;
    turbo_serial_t *serial = NULL;

    turbo_serial_config_default(&config);
    config.rx_buffer_size = 64;
    config.tx_buffer_size = 128;

    check_int_eq(turbo_serial_create(&serial, &config), TURBO_SERIAL_OK);
    check_not_null(serial);

    /* Usable capacity = buffer_size - 1 (sentinel slot) */
    check_equal(turbo_serial_rx_capacity(serial), 63);
    check_equal(turbo_serial_tx_capacity(serial), 127);

    turbo_serial_destroy(serial);
  }

  it("sets async_running flag only after thread creation succeeds") {
    turbo_serial_t *serial = NULL;

    check_int_eq(turbo_serial_create(&serial, NULL), TURBO_SERIAL_OK);
    check_not_null(serial);

    /* Without an open port, start_async should fail */
    check_int_eq(turbo_serial_start_async(serial), TURBO_SERIAL_INVALID_STATE);
    
    /* Flag should NOT be set after failed start */
    check_equal(turbo_serial_async_running(serial), 0);

    turbo_serial_destroy(serial);
  }
}
