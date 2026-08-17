/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 TurboUtils Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR A PARTICULAR PURPOSE AND CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef TURBO_SERIAL_INTERNAL_H
#define TURBO_SERIAL_INTERNAL_H

#include "turbo_serial.h"
#include "turbo_str.h"
#include "turbo_vec.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct turbo_port_handle turbo_port_handle_t;
typedef struct turbo_serial_event_set_impl turbo_serial_event_set_impl_t;

typedef struct turbo_serial_port_info_storage {
  turbo_serial_port_info_t view;
  tstr_t name;
  tstr_t description;
  tstr_t usb_manufacturer;
  tstr_t usb_product;
  tstr_t usb_serial;
  tstr_t bluetooth_address;
} turbo_serial_port_info_storage_t;

TURBO_VEC_DEFINE(turbo_serial_port_info_vec_t, turbo_serial_port_info_storage_t)

typedef struct turbo_serial_backend_ops turbo_serial_backend_ops_t;

struct turbo_serial_backend_ops {
  turbo_serial_result_t (*list_ports)(turbo_serial_port_info_vec_t *vec);
  turbo_serial_result_t (*get_port_info)(const char *port_name,
                                         turbo_serial_port_info_storage_t *storage);

  turbo_serial_result_t (*open)(turbo_port_handle_t **handle_out, const char *port_name,
                               turbo_serial_mode_t mode);
  void (*close)(turbo_port_handle_t *handle);
  turbo_serial_result_t (*configure)(turbo_port_handle_t *handle,
                                     const turbo_serial_config_t *config);

  turbo_serial_result_t (*blocking_read)(turbo_port_handle_t *handle, void *buf, size_t count,
                                         unsigned int timeout_ms, size_t *bytes_read);
  turbo_serial_result_t (*blocking_write)(turbo_port_handle_t *handle, const void *buf,
                                          size_t count, unsigned int timeout_ms,
                                          size_t *bytes_written);

  turbo_serial_result_t (*nonblocking_read)(turbo_port_handle_t *handle, void *buf, size_t count,
                                            size_t *bytes_read);
  turbo_serial_result_t (*nonblocking_write)(turbo_port_handle_t *handle, const void *buf,
                                             size_t count, size_t *bytes_written);

  turbo_serial_result_t (*new_event_set)(turbo_serial_event_set_impl_t **set_out);
  void (*free_event_set)(turbo_serial_event_set_impl_t *set);
  turbo_serial_result_t (*add_port_events)(turbo_serial_event_set_impl_t *set,
                                           turbo_port_handle_t *handle, unsigned int events);
  turbo_serial_result_t (*wait)(turbo_serial_event_set_impl_t *set, unsigned int timeout_ms,
                                unsigned int *events_out);
};

const turbo_serial_backend_ops_t *turbo_serial_get_native_backend(void);

#ifdef __cplusplus
}
#endif

#endif /* TURBO_SERIAL_INTERNAL_H */
