#ifndef CFLOW_USB_H
#define CFLOW_USB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Threading contract:
 * - one internal thread exclusively drives libusb native events;
 * - one caller at a time may drive user callbacks with run_ready();
 * - public control-plane calls for a device/context are externally serialized;
 * - destroy is quiescent-only and never races an API call or callback.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cflow_usb_context {
    void *impl;
} cflow_usb_context;

typedef struct cflow_usb_device {
    void *impl;
} cflow_usb_device;

typedef uint64_t cflow_usb_transfer_id;

#define CFLOW_USB_INVALID_TRANSFER_ID UINT64_C(0)

typedef enum cflow_usb_transfer_kind {
    CFLOW_USB_TRANSFER_CONTROL = 0,
    CFLOW_USB_TRANSFER_BULK,
    CFLOW_USB_TRANSFER_INTERRUPT
} cflow_usb_transfer_kind;

typedef enum cflow_usb_hotplug_kind {
    CFLOW_USB_HOTPLUG_ARRIVED = 0,
    CFLOW_USB_HOTPLUG_LEFT,
    CFLOW_USB_HOTPLUG_RESCAN_REQUIRED
} cflow_usb_hotplug_kind;

struct cflow_usb_hotplug_event;

typedef struct cflow_usb_context_config {
    /* Hard maximum number of devices accepted in one enumeration snapshot. */
    size_t device_capacity;
    /* Fixed number of asynchronous transfer slots owned by the context. */
    size_t transfer_capacity;
    /* Hard bound for copied control-transfer payloads. */
    size_t control_payload_capacity;
    /* Fixed detailed hotplug queue capacity. */
    size_t hotplug_capacity;
    /* Native event-loop wait bound; must be in [1, 1000]. */
    unsigned int event_poll_timeout_ms;
    void (*hotplug)(void *user, const struct cflow_usb_hotplug_event *event);
    void *hotplug_user;
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

typedef struct cflow_usb_device_identity {
    uint8_t bus_number;
    uint8_t device_address;
    uint16_t vendor_id;
    uint16_t product_id;
} cflow_usb_device_identity;

typedef struct cflow_usb_control_setup {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
} cflow_usb_control_setup;

typedef struct cflow_usb_transfer_result {
    cflow_usb_transfer_id id;
    cflow_usb_transfer_kind kind;
    int status;
    size_t bytes_transferred;
} cflow_usb_transfer_result;

typedef void (*cflow_usb_transfer_fn)(
    void *user, const cflow_usb_transfer_result *result);

typedef struct cflow_usb_transfer_request {
    cflow_usb_transfer_kind kind;
    /* Includes the USB direction bit for bulk and interrupt transfers. */
    uint8_t endpoint;
    cflow_usb_control_setup control;
    void *buffer;
    size_t length;
    unsigned int timeout_ms;
    cflow_usb_transfer_fn complete;
    void *complete_user;
} cflow_usb_transfer_request;

typedef struct cflow_usb_hotplug_event {
    cflow_usb_hotplug_kind kind;
    cflow_usb_device_identity identity;
} cflow_usb_hotplug_event;

typedef struct cflow_usb_stats {
    size_t device_capacity;
    size_t enumerations;
    size_t devices_observed;
    size_t enumeration_overflow;
    size_t transfer_capacity;
    size_t active_devices;
    size_t active_transfers;
    size_t transfers_delivered;
    size_t hotplug_queued;
    size_t hotplug_suppressed;
    size_t hotplug_rescan_required;
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
/**
 * Open the exact enumerated identity; reconnect never rebinds this handle.
 * @param context Live owning context.
 * @param identity Exact bus/address/VID/PID snapshot.
 * @param device Zero-initialized destination.
 * @return TURBO_OK, TURBO_ENODEV, TURBO_ENOBUFS, or a mapped libusb error.
 */
int cflow_usb_device_open(cflow_usb_context *context,
                          const cflow_usb_device_identity *identity,
                          cflow_usb_device *device);
/**
 * Set the active device configuration before claiming interfaces.
 * @return TURBO_EBUSY while interfaces/transfers are active, otherwise the
 * mapped libusb result.
 */
int cflow_usb_device_set_configuration(cflow_usb_device *device,
                                       int configuration);
/** Claim one explicit interface; duplicate claims return TURBO_EALREADY. */
int cflow_usb_device_claim_interface(cflow_usb_device *device,
                                     uint8_t interface_number);
/** Release an idle claimed interface; active transfers return TURBO_EBUSY. */
int cflow_usb_device_release_interface(cflow_usb_device *device,
                                       uint8_t interface_number);
/** Close an idle device; claimed interfaces or transfers return TURBO_EBUSY. */
int cflow_usb_device_close(cflow_usb_device *device);
/**
 * Submit one bounded asynchronous control, bulk, or interrupt transfer.
 * The caller buffer is borrowed until its exactly-once terminal callback
 * returns from cflow_usb_run_ready(). Control payloads are copied into fixed
 * context-owned storage; successful IN payloads are copied back before the
 * callback. Bulk and interrupt payloads are passed directly to libusb.
 * @return TURBO_ENOBUFS when every fixed slot is occupied; no request is
 * accepted on any error and out_id remains CFLOW_USB_INVALID_TRANSFER_ID.
 */
int cflow_usb_submit(cflow_usb_context *context,
                     cflow_usb_device *device,
                     const cflow_usb_transfer_request *request,
                     cflow_usb_transfer_id *out_id);
/**
 * Request cancellation; completion remains asynchronous and exactly once.
 * Unknown IDs return TURBO_ENOENT and already-terminal IDs TURBO_EALREADY.
 */
int cflow_usb_cancel(cflow_usb_context *context,
                     cflow_usb_transfer_id id);
/**
 * Deliver at most max_events terminal transfer/hotplug callbacks on one
 * caller driver and write the delivered count. Concurrent drivers fail busy.
 */
int cflow_usb_run_ready(cflow_usb_context *context, size_t max_events,
                        size_t *delivered);
/**
 * Resume detailed hotplug delivery after the rescan marker was observed.
 * Calling before marker delivery returns TURBO_EALREADY.
 */
int cflow_usb_acknowledge_hotplug_rescan(cflow_usb_context *context);
/** Copy an observational enumeration/capacity snapshot. */
bool cflow_usb_get_stats(const cflow_usb_context *context,
                         cflow_usb_stats *out);
/**
 * Release libusb and restore a live context to the zero state.
 * All calls/callbacks must have stopped, all events must be drained, and all
 * devices must be closed; otherwise this control-plane operation returns busy.
 */
int cflow_usb_context_destroy(cflow_usb_context *context);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_USB_H */
