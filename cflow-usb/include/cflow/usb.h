#ifndef CFLOW_USB_H
#define CFLOW_USB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cflow_usb_context {
    void *impl;
} cflow_usb_context;

typedef struct cflow_usb_context_config {
    /* Hard maximum number of devices accepted in one enumeration snapshot. */
    size_t device_capacity;
} cflow_usb_context_config;

typedef struct cflow_usb_device_info {
    uint8_t bus_number;
    uint8_t device_address;
    uint8_t port_number;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_version;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
    uint8_t configuration_count;
} cflow_usb_device_info;

typedef struct cflow_usb_stats {
    size_t device_capacity;
    size_t enumerations;
    size_t devices_observed;
    size_t enumeration_overflow;
} cflow_usb_stats;

/**
 * Initialize an owning libusb context.
 * @param context Zero-initialized destination.
 * @param config Positive hard device snapshot capacity.
 * @return TURBO_OK, TURBO_EINVAL, TURBO_ENOMEM, or mapped libusb error.
 */
int cflow_usb_context_init(cflow_usb_context *context,
                           const cflow_usb_context_config *config);
/**
 * Produce one caller-owned enumeration snapshot.
 *
 * A NULL/zero output is a size query. If the discovered count exceeds either
 * the context bound or output capacity, returns TURBO_ENOBUFS, writes the
 * required count, and commits no partial entries. Device identity is the
 * observed bus/address/VID/PID tuple; reconnect always requires re-enumeration.
 */
int cflow_usb_enumerate(cflow_usb_context *context,
                        cflow_usb_device_info *out,
                        size_t out_capacity,
                        size_t *out_count);
/** Copy an observational enumeration/capacity snapshot. */
bool cflow_usb_get_stats(const cflow_usb_context *context,
                         cflow_usb_stats *out);
/**
 * Release libusb and restore a live context to the zero state.
 * All enumeration calls must have stopped before this control-plane operation.
 */
int cflow_usb_context_destroy(cflow_usb_context *context);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_USB_H */
