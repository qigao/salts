/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 TurboUtils Project
 */

#include "tinytest.h"
#include "tinymock.h"
#include "tlog.h"
#include "turbo_serial_internal.h"
#include "turbo_serial_test.h"
#include "turbo_thread.h"

#include <string.h>

static int fake_handle_marker = 0;
#define FAKE_HANDLE ((turbo_port_handle_t *)&fake_handle_marker)

/* TinyMock's portable value carrier supports int/void pointers, not incomplete
 * backend handle pointers or enum return types in MSVC's _Generic. */
TINYMOCk_MOCK(int, fake_nonblocking_read, void *, void *, size_t)
TINYMOCk_MOCK(int, fake_nonblocking_write, void *, const void *, size_t)

static unsigned char fake_rx_data[16];
static size_t fake_rx_len;
static unsigned char fake_tx_data[16];
static size_t fake_tx_len;
static size_t storage_clone_calls;

static bool fail_fourth_storage_clone(tstr *out, tstr source) {
  storage_clone_calls++;
  if (storage_clone_calls == 4) return false;
  return turbo_serial_port_info_storage_clone_string(out, source);
}

static void fill_port_storage(turbo_serial_port_info_storage_t *storage) {
  memset(storage, 0, sizeof(*storage));
  storage->name = tstr_dup("COM7");
  storage->description = tstr_dup("USB serial port");
  storage->usb_manufacturer = tstr_dup("Turbo");
  storage->usb_product = tstr_dup("Bridge");
  storage->usb_serial = tstr_dup("ABC123");
  storage->bluetooth_address = tstr_dup("00:11:22:33:44:55");
  turbo_serial_port_info_storage_refresh_view(storage);
}

static size_t mock_min_size(size_t a, size_t b) {
  return a < b ? a : b;
}

static turbo_serial_result_t fake_nonblocking_read_impl(turbo_port_handle_t *handle, void *buf,
                                                        size_t count, size_t *bytes_read) {
  turbo_serial_result_t result = fake_nonblocking_read(handle, buf, count);
  if (result == TURBO_SERIAL_OK && buf && bytes_read) {
    size_t copy_len = mock_min_size(count, fake_rx_len);
    if (copy_len > 0) {
      memcpy(buf, fake_rx_data, copy_len);
      if (fake_rx_len > copy_len) {
        memmove(fake_rx_data, fake_rx_data + copy_len, fake_rx_len - copy_len);
      }
      fake_rx_len -= copy_len;
    }
    *bytes_read = copy_len;
  }
  return result;
}

static turbo_serial_result_t fake_nonblocking_write_impl(turbo_port_handle_t *handle,
                                                          const void *buf, size_t count,
                                                          size_t *bytes_written) {
  turbo_serial_result_t result = fake_nonblocking_write(handle, buf, count);
  if (result == TURBO_SERIAL_OK && buf && bytes_written) {
    size_t copy_len = mock_min_size(count, sizeof(fake_tx_data) - fake_tx_len);
    if (copy_len > 0) {
      memcpy(fake_tx_data + fake_tx_len, buf, copy_len);
      fake_tx_len += copy_len;
    }
    *bytes_written = copy_len;
  }
  return result;
}

static void fake_close(turbo_port_handle_t *handle) {
  (void)handle;
}

static const turbo_serial_backend_ops_t fake_backend_ops = {
    .close = fake_close,
    .nonblocking_read = fake_nonblocking_read_impl,
    .nonblocking_write = fake_nonblocking_write_impl,
};

suite("turbo_serial mocked backend") {
  before_each() {
    memset(fake_rx_data, 0, sizeof(fake_rx_data));
    memset(fake_tx_data, 0, sizeof(fake_tx_data));
    fake_rx_len = 0;
    fake_tx_len = 0;

    mock_fake_nonblocking_read_reset();
    mock_fake_nonblocking_read_set_default_return(TINYMOCk_RETURN(TURBO_SERIAL_OK));
    mock_fake_nonblocking_write_reset();
    mock_fake_nonblocking_write_set_default_return(TINYMOCk_RETURN(TURBO_SERIAL_OK));

    turbo_serial_set_backend_ops_for_testing(&fake_backend_ops);
  }

  it("pumps mocked rx bytes into the rx SPSC ring") {
    turbo_serial_config_t config;
    turbo_serial_t *serial = NULL;
    char out[4] = {0};
    size_t bytes_read = 0;

    turbo_serial_config_default(&config);
    config.rx_buffer_size = 8;
    config.tx_buffer_size = 8;
    config.poll_interval_ms = 5;

    fake_rx_data[0] = 'A';
    fake_rx_data[1] = 'B';
    fake_rx_data[2] = 'C';
    fake_rx_len = 3;

    mock_fake_nonblocking_read_expect(tinymock_expected_arg_any(),
                                      tinymock_expected_arg_any(),
                                      tinymock_expected_arg_any(),
                                      TINYMOCk_RETURN(TURBO_SERIAL_OK));

    check_equal(turbo_serial_create(&serial, &config), TURBO_SERIAL_OK);
    check_not_null(serial);
    turbo_serial_test_set_handle(serial, FAKE_HANDLE);

    check_equal(turbo_serial_start_async(serial), TURBO_SERIAL_OK);
    turbo_sleep_ms(30);
    check_equal(turbo_serial_stop_async(serial), TURBO_SERIAL_OK);

    check_equal(turbo_serial_rx_available(serial), 3);
    check_equal(turbo_serial_read_buffered(serial, out, sizeof(out), &bytes_read),
                TURBO_SERIAL_OK);
    check_equal(bytes_read, 3);
    check_equal(out, "ABC", 3);

    mock_fake_nonblocking_read_verify();
    turbo_serial_destroy(serial);

    TLOG_DEBUGF("mocked rx pump delivered {} bytes", bytes_read);
  }

  it("drains tx bytes through the mocked backend") {
    turbo_serial_config_t config;
    turbo_serial_t *serial = NULL;
    size_t bytes_buffered = 0;

    turbo_serial_config_default(&config);
    config.rx_buffer_size = 8;
    config.tx_buffer_size = 8;
    config.poll_interval_ms = 5;

    mock_fake_nonblocking_write_expect(tinymock_expected_arg_any(),
                                       tinymock_expected_arg_any(),
                                       tinymock_expected_arg_any(),
                                       TINYMOCk_RETURN(TURBO_SERIAL_OK));

    check_equal(turbo_serial_create(&serial, &config), TURBO_SERIAL_OK);
    check_not_null(serial);
    turbo_serial_test_set_handle(serial, FAKE_HANDLE);

    check_equal(turbo_serial_start_async(serial), TURBO_SERIAL_OK);
    check_equal(turbo_serial_write_buffered(serial, "XY", 2, &bytes_buffered),
                TURBO_SERIAL_OK);
    check_equal(bytes_buffered, 2);

    turbo_sleep_ms(30);
    check_equal(turbo_serial_stop_async(serial), TURBO_SERIAL_OK);

    check_equal(fake_tx_len, 2);
    check_equal(fake_tx_data, "XY", 2);

    mock_fake_nonblocking_write_verify();
    turbo_serial_destroy(serial);

    TLOG_DEBUGF("mocked tx pump drained {} bytes", fake_tx_len);
  }

  it("owns all port-info strings through copy move and repeated destroy") {
    turbo_serial_port_info_storage_t source;
    turbo_serial_port_info_storage_t moved = {0};
    turbo_serial_port_info_vec_t items = {0};
    turbo_serial_port_info_storage_t *stored;

    fill_port_storage(&source);
    check_equal(turbo_serial_port_info_vec_t_init(&items), STL_OK);
    check_equal(turbo_serial_port_info_vec_t_push(&items, source), STL_OK);
    stored = turbo_serial_port_info_vec_t_at(&items, 0);
    check_not_null(stored);
    check_equal(tstr_cmp(stored->name, source.name), 0);
    check_equal(tstr_cmp(stored->description, source.description), 0);
    check_equal(tstr_cmp(stored->usb_manufacturer, source.usb_manufacturer), 0);
    check_equal(tstr_cmp(stored->usb_product, source.usb_product), 0);
    check_equal(tstr_cmp(stored->usb_serial, source.usb_serial), 0);
    check_equal(tstr_cmp(stored->bluetooth_address, source.bluetooth_address), 0);
    check(stored->name != source.name);

    turbo_serial_port_info_storage_destroy(&source);
    check_equal(stored->view.name, "COM7");
    turbo_serial_port_info_storage_move(&moved, stored);
    check_null(stored->name);
    check_equal(moved.view.usb_serial, "ABC123");
    turbo_serial_port_info_storage_move(&moved, &moved);
    check_equal(moved.view.usb_serial, "ABC123");
    turbo_serial_port_info_storage_destroy(&moved);
    turbo_serial_port_info_storage_destroy(&moved);
    turbo_serial_port_info_vec_t_destroy(&items);
  }

  it("rolls back every temporary string when an entry copy fails") {
    turbo_serial_port_info_storage_t source;
    turbo_serial_port_info_storage_t destination = {0};

    fill_port_storage(&source);
    storage_clone_calls = 0;
    check(!turbo_serial_port_info_storage_copy_with(
        &destination, &source, fail_fourth_storage_clone));
    check_equal(storage_clone_calls, 4);
    check_null(destination.name);
    check_null(destination.description);
    check_null(destination.usb_manufacturer);
    check_null(destination.usb_product);
    check_null(destination.usb_serial);
    check_null(destination.bluetooth_address);
    turbo_serial_port_info_storage_destroy(&source);
    turbo_serial_port_info_storage_destroy(&destination);
  }
}
