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
#include <turbostl/vec.h>

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct turbo_port_handle turbo_port_handle_t;
typedef struct turbo_serial_event_set_impl turbo_serial_event_set_impl_t;

typedef struct turbo_serial_port_info_storage {
  turbo_serial_port_info_t view;
  tstr name;
  tstr description;
  tstr usb_manufacturer;
  tstr usb_product;
  tstr usb_serial;
  tstr bluetooth_address;
} turbo_serial_port_info_storage_t;

/* Native enumeration has no configured port-count bound. Preserve that legacy
 * surface with the largest count whose aligned Vec stride fits in size_t. The
 * element descriptor remains the source of size/alignment truth. */
static inline size_t turbo_serial_port_list_entry_limit(
    const cmeta_type_desc *type) {
  size_t rounded;
  size_t stride;
  if (!type || type->size == 0u || type->align == 0u ||
      type->size > SIZE_MAX - (type->align - 1u))
    return 0u;
  rounded = type->size + type->align - 1u;
  stride = (rounded / type->align) * type->align;
  return SIZE_MAX / stride;
}

static inline void turbo_serial_port_info_storage_refresh_view(
    turbo_serial_port_info_storage_t *storage) {
  storage->view.name = storage->name;
  storage->view.description = storage->description;
  storage->view.usb_manufacturer = storage->usb_manufacturer;
  storage->view.usb_product = storage->usb_product;
  storage->view.usb_serial = storage->usb_serial;
  storage->view.bluetooth_address = storage->bluetooth_address;
}

static inline void turbo_serial_port_info_storage_destroy(void *value) {
  turbo_serial_port_info_storage_t *storage =
      (turbo_serial_port_info_storage_t *)value;
  if (!storage) return;
  tstr_freep(&storage->name);
  tstr_freep(&storage->description);
  tstr_freep(&storage->usb_manufacturer);
  tstr_freep(&storage->usb_product);
  tstr_freep(&storage->usb_serial);
  tstr_freep(&storage->bluetooth_address);
  memset(storage, 0, sizeof(*storage));
}

typedef bool (*turbo_serial_clone_string_fn)(tstr *out, tstr source);

static inline bool turbo_serial_port_info_storage_clone_string(tstr *out,
                                                                tstr source) {
  if (!source) {
    *out = NULL;
    return true;
  }
  *out = tstr_clone(source);
  return *out != NULL;
}

static inline bool turbo_serial_port_info_storage_copy_with(
    void *destination_, const void *source_, turbo_serial_clone_string_fn clone) {
  turbo_serial_port_info_storage_t temporary = {0};
  turbo_serial_port_info_storage_t *destination =
      (turbo_serial_port_info_storage_t *)destination_;
  const turbo_serial_port_info_storage_t *source =
      (const turbo_serial_port_info_storage_t *)source_;
  if (!destination || !source || !clone) return false;
  temporary.view = source->view;
  if (!clone(&temporary.name, source->name) ||
      !clone(&temporary.description, source->description) ||
      !clone(&temporary.usb_manufacturer, source->usb_manufacturer) ||
      !clone(&temporary.usb_product, source->usb_product) ||
      !clone(&temporary.usb_serial, source->usb_serial) ||
      !clone(&temporary.bluetooth_address, source->bluetooth_address)) {
    turbo_serial_port_info_storage_destroy(&temporary);
    return false;
  }
  turbo_serial_port_info_storage_refresh_view(&temporary);
  *destination = temporary;
  return true;
}

static inline bool turbo_serial_port_info_storage_copy(void *destination,
                                                        const void *source) {
  return turbo_serial_port_info_storage_copy_with(
      destination, source, turbo_serial_port_info_storage_clone_string);
}

static inline void turbo_serial_port_info_storage_move(void *destination_,
                                                        void *source_) {
  turbo_serial_port_info_storage_t *destination =
      (turbo_serial_port_info_storage_t *)destination_;
  turbo_serial_port_info_storage_t *source =
      (turbo_serial_port_info_storage_t *)source_;
  if (!destination || !source || destination == source) return;
  *destination = *source;
  memset(source, 0, sizeof(*source));
  turbo_serial_port_info_storage_refresh_view(destination);
}

static const cmeta_type_traits turbo_serial_port_info_storage_traits = {
    CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    NULL, NULL, NULL, turbo_serial_port_info_storage_copy,
    turbo_serial_port_info_storage_move,
    turbo_serial_port_info_storage_destroy};

static const cmeta_type_desc turbo_serial_port_info_storage_type = {
    "turbo_serial_port_info_storage", sizeof(turbo_serial_port_info_storage_t),
    _Alignof(turbo_serial_port_info_storage_t), CMETA_T_OBJECT, NULL,
    &turbo_serial_port_info_storage_traits};

typedef turbo_vec_t turbo_serial_port_info_vec_t;

static inline turbo_serial_result_t turbo_serial_result_from_stl(
    turbo_stl_status status) {
  switch (status) {
    case TURBO_STL_OK: return TURBO_SERIAL_OK;
    case TURBO_STL_INVALID_ARGUMENT: return TURBO_SERIAL_INVALID_VALUE;
    case TURBO_STL_OUT_OF_MEMORY:
    case TURBO_STL_CAPACITY_EXCEEDED: return TURBO_SERIAL_NO_MEMORY;
    case TURBO_STL_EMPTY:
    case TURBO_STL_NOT_FOUND:
    case TURBO_STL_TYPE_MISMATCH:
    case TURBO_STL_TRAIT_MISSING: return TURBO_SERIAL_INVALID_STATE;
  }
  return TURBO_SERIAL_INVALID_STATE;
}

static inline turbo_stl_status turbo_serial_port_info_vec_t_init(
    turbo_serial_port_info_vec_t *vec) {
  return turbo_vec_init(vec, &turbo_serial_port_info_storage_type,
                        turbo_serial_port_list_entry_limit(
                            &turbo_serial_port_info_storage_type));
}

static inline turbo_stl_status turbo_serial_port_info_vec_t_push(
    turbo_serial_port_info_vec_t *vec,
    turbo_serial_port_info_storage_t value) {
  return turbo_vec_push(vec, &value);
}

static inline turbo_serial_port_info_storage_t *turbo_serial_port_info_vec_t_at(
    turbo_serial_port_info_vec_t *vec, size_t index) {
  return (turbo_serial_port_info_storage_t *)turbo_vec_at(vec, index);
}

static inline const turbo_serial_port_info_storage_t *
turbo_serial_port_info_vec_t_at_const(const turbo_serial_port_info_vec_t *vec,
                                      size_t index) {
  return (const turbo_serial_port_info_storage_t *)turbo_vec_at_const(vec,
                                                                      index);
}

static inline size_t turbo_serial_port_info_vec_t_size(
    const turbo_serial_port_info_vec_t *vec) {
  return turbo_vec_size(vec);
}

static inline void turbo_serial_port_info_vec_t_destroy(
    turbo_serial_port_info_vec_t *vec) {
  turbo_vec_destroy(vec);
}

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
