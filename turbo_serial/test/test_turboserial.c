/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 TurboUtils Project
 */

#include "tinytest.h"
#include "turbo_serial.h"

#include <string.h>

suite("turbo_serial native ABI") {
  it("keeps turbo serial public configuration independent from OS ABI") {
    turbo_serial_config_t config;

    turbo_serial_config_default(&config);

    check_int_eq(config.parity, TURBO_SERIAL_PARITY_NONE);
    check_int_eq(config.flowcontrol, TURBO_SERIAL_FLOWCONTROL_NONE);
    check_int_eq(TURBO_SERIAL_MODE_READ, 1);
    check_int_eq(TURBO_SERIAL_MODE_WRITE, 2);
    check_int_eq(TURBO_SERIAL_MODE_READ_WRITE, 3);
    check_str_eq(turbo_serial_result_name(TURBO_SERIAL_WOULD_BLOCK), "would block");
  }

  it("creates a per-instance turbo serial buffer") {
    turbo_serial_config_t config;
    turbo_serial_t *serial = NULL;

    turbo_serial_config_default(&config);
    check_equal(config.io_chunk_size, 256);
    check_equal((int)config.poll_interval_ms, 10);
    config.rx_buffer_size = 16;
    config.tx_buffer_size = 16;

    check_int_eq(turbo_serial_create(&serial, &config), TURBO_SERIAL_OK);
    check_not_null(serial);
    check_equal(turbo_serial_rx_capacity(serial), 15);
    check_equal(turbo_serial_tx_capacity(serial), 15);
    check_equal(turbo_serial_rx_available(serial), 0);
    check_equal(turbo_serial_tx_available(serial), 0);

    turbo_serial_destroy(serial);
  }

  it("rejects invalid turbo serial buffer sizes") {
    turbo_serial_config_t config;
    turbo_serial_t *serial = NULL;

    turbo_serial_config_default(&config);
    config.rx_buffer_size = 15;

    check_int_eq(turbo_serial_create(&serial, &config), TURBO_SERIAL_INVALID_VALUE);
    check_null(serial);

    turbo_serial_config_default(&config);
    config.io_chunk_size = 0;
    check_int_eq(turbo_serial_create(&serial, &config), TURBO_SERIAL_INVALID_VALUE);
    check_null(serial);

    turbo_serial_config_default(&config);
    config.poll_interval_ms = 0;
    check_int_eq(turbo_serial_create(&serial, &config), TURBO_SERIAL_INVALID_VALUE);
    check_null(serial);

    turbo_serial_config_default(&config);
    config.bits = 9;
    check_int_eq(turbo_serial_create(&serial, &config), TURBO_SERIAL_INVALID_VALUE);
    check_null(serial);

    turbo_serial_config_default(&config);
    config.stopbits = 3;
    check_int_eq(turbo_serial_create(&serial, &config), TURBO_SERIAL_INVALID_VALUE);
    check_null(serial);

    turbo_serial_config_default(&config);
    config.parity = TURBO_SERIAL_PARITY_INVALID;
    check_int_eq(turbo_serial_create(&serial, &config), TURBO_SERIAL_INVALID_VALUE);
    check_null(serial);
  }

  it("requires an open port before starting async pump") {
    turbo_serial_t *serial = NULL;

    check_int_eq(turbo_serial_create(&serial, NULL), TURBO_SERIAL_OK);
    check_not_null(serial);

    check_int_eq(turbo_serial_start_async(serial), TURBO_SERIAL_INVALID_STATE);
    check_equal(turbo_serial_async_running(serial), 0);
    check_int_eq(turbo_serial_stop_async(serial), TURBO_SERIAL_OK);
    check_int_eq(turbo_serial_last_error(serial), TURBO_SERIAL_OK);

    turbo_serial_destroy(serial);
  }

  it("buffers rx and tx data through per-handle SPSC rings") {
    turbo_serial_config_t config;
    turbo_serial_t *serial = NULL;
    const char rx_data[] = "sensor:1234";
    const char tx_data[] = "command:go";
    char out[32] = {0};
    size_t count = 0;

    turbo_serial_config_default(&config);
    config.rx_buffer_size = 32;
    config.tx_buffer_size = 32;

    check_int_eq(turbo_serial_create(&serial, &config), TURBO_SERIAL_OK);
    check_not_null(serial);

    check_int_eq(turbo_serial_buffer_rx(serial, rx_data, strlen(rx_data), &count),
                 TURBO_SERIAL_OK);
    check_equal(count, strlen(rx_data));
    check_equal(turbo_serial_rx_available(serial), strlen(rx_data));

    check_int_eq(turbo_serial_read_buffered(serial, out, sizeof(out), &count),
                 TURBO_SERIAL_OK);
    check_equal(count, strlen(rx_data));
    check_mem_eq(out, rx_data, strlen(rx_data));
    check_equal(turbo_serial_rx_available(serial), 0);

    memset(out, 0, sizeof(out));
    check_int_eq(turbo_serial_write_buffered(serial, tx_data, strlen(tx_data), &count),
                 TURBO_SERIAL_OK);
    check_equal(count, strlen(tx_data));
    check_equal(turbo_serial_tx_available(serial), strlen(tx_data));

    turbo_serial_destroy(serial);
  }
}
