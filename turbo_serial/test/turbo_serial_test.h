/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2026 TurboUtils Project
 */

#ifndef TURBO_SERIAL_TEST_H
#define TURBO_SERIAL_TEST_H

#include "turbo_serial_internal.h"

void turbo_serial_set_backend_ops_for_testing(const turbo_serial_backend_ops_t *ops);
void turbo_serial_test_set_handle(turbo_serial_t *serial, turbo_port_handle_t *handle);

#endif
