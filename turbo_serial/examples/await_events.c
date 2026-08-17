#include "turbo_serial.h"

#include <stdio.h>

/* Example of waiting for serial readiness through a TurboSerial event set. */

int main(int argc, char **argv) {
  turbo_serial_t *serial = NULL;
  turbo_serial_event_set_t *events = NULL;
  unsigned int ready_events = 0;
  turbo_serial_result_t result;

  if (argc != 2) {
    printf("Usage: %s <port name>\n", argv[0]);
    return 1;
  }

  result = turbo_serial_create(&serial, NULL);
  if (result != TURBO_SERIAL_OK) {
    printf("turbo_serial_create() failed: %s\n", turbo_serial_result_name(result));
    return 1;
  }

  result = turbo_serial_open(serial, argv[1], TURBO_SERIAL_MODE_READ_WRITE);
  if (result != TURBO_SERIAL_OK) {
    printf("turbo_serial_open() failed: %s\n", turbo_serial_result_name(result));
    turbo_serial_destroy(serial);
    return 1;
  }

  result = turbo_serial_event_set_create(&events);
  if (result != TURBO_SERIAL_OK) {
    printf("turbo_serial_event_set_create() failed: %s\n", turbo_serial_result_name(result));
    turbo_serial_destroy(serial);
    return 1;
  }

  result = turbo_serial_event_set_add(events, serial,
                                      TURBO_SERIAL_EVENT_RX_READY | TURBO_SERIAL_EVENT_ERROR);
  if (result != TURBO_SERIAL_OK) {
    printf("turbo_serial_event_set_add() failed: %s\n", turbo_serial_result_name(result));
    turbo_serial_event_set_destroy(events);
    turbo_serial_destroy(serial);
    return 1;
  }

  result = turbo_serial_event_wait_ex(events, 5000, &ready_events);
  if (result == TURBO_SERIAL_OK) {
    printf("Serial events ready: rx=%u tx=%u error=%u\n",
           (ready_events & TURBO_SERIAL_EVENT_RX_READY) ? 1u : 0u,
           (ready_events & TURBO_SERIAL_EVENT_TX_READY) ? 1u : 0u,
           (ready_events & TURBO_SERIAL_EVENT_ERROR) ? 1u : 0u);
  } else {
    printf("turbo_serial_event_wait_ex() failed: %s\n", turbo_serial_result_name(result));
  }

  turbo_serial_event_set_destroy(events);
  turbo_serial_destroy(serial);
  return result == TURBO_SERIAL_OK ? 0 : 1;
}
