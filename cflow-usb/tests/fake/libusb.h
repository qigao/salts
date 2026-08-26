#ifndef CFLOW_USB_TEST_FAKE_LIBUSB_H
#define CFLOW_USB_TEST_FAKE_LIBUSB_H

#include <stddef.h>
#include <stdint.h>

#if defined(_MSC_VER)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/time.h>
#include <sys/types.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LIBUSB_CALL
#define LIBUSB_SUCCESS 0
#define LIBUSB_ERROR_IO -1
#define LIBUSB_ERROR_INVALID_PARAM -2
#define LIBUSB_ERROR_ACCESS -3
#define LIBUSB_ERROR_NO_DEVICE -4
#define LIBUSB_ERROR_NOT_FOUND -5
#define LIBUSB_ERROR_BUSY -6
#define LIBUSB_ERROR_TIMEOUT -7
#define LIBUSB_ERROR_OVERFLOW -8
#define LIBUSB_ERROR_PIPE -9
#define LIBUSB_ERROR_INTERRUPTED -10
#define LIBUSB_ERROR_NO_MEM -11
#define LIBUSB_ERROR_NOT_SUPPORTED -12

#define LIBUSB_ENDPOINT_IN 0x80u
#define LIBUSB_ENDPOINT_ADDRESS_MASK 0x0fu
#define LIBUSB_CONTROL_SETUP_SIZE 8u
#define LIBUSB_CAP_HAS_HOTPLUG 1u
#define LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED 1
#define LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT 2
#define LIBUSB_HOTPLUG_NO_FLAGS 0
#define LIBUSB_HOTPLUG_MATCH_ANY -1

typedef struct libusb_context libusb_context;
typedef struct libusb_device libusb_device;
typedef struct libusb_device_handle libusb_device_handle;
typedef int libusb_hotplug_event;
typedef int libusb_hotplug_callback_handle;

#if defined(_MSC_VER)
struct timeval {
    long tv_sec;
    long tv_usec;
};
#endif

struct libusb_device_descriptor {
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bNumConfigurations;
};

struct libusb_control_setup {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
};

enum libusb_transfer_status {
    LIBUSB_TRANSFER_COMPLETED = 0,
    LIBUSB_TRANSFER_ERROR,
    LIBUSB_TRANSFER_TIMED_OUT,
    LIBUSB_TRANSFER_CANCELLED,
    LIBUSB_TRANSFER_STALL,
    LIBUSB_TRANSFER_NO_DEVICE,
    LIBUSB_TRANSFER_OVERFLOW
};

struct libusb_transfer;
typedef void (LIBUSB_CALL *libusb_transfer_cb_fn)(
    struct libusb_transfer *transfer);

struct libusb_transfer {
    libusb_device_handle *dev_handle;
    uint8_t endpoint;
    uint8_t type;
    unsigned int timeout;
    enum libusb_transfer_status status;
    int length;
    int actual_length;
    libusb_transfer_cb_fn callback;
    void *user_data;
    unsigned char *buffer;
};

typedef int (LIBUSB_CALL *libusb_hotplug_callback_fn)(
    libusb_context *context, libusb_device *device,
    libusb_hotplug_event event, void *user);

static inline void libusb_fill_control_setup(
    unsigned char *buffer, uint8_t request_type, uint8_t request,
    uint16_t value, uint16_t index, uint16_t length) {
    struct libusb_control_setup *setup =
        (struct libusb_control_setup *)(void *)buffer;
    setup->bmRequestType = request_type;
    setup->bRequest = request;
    setup->wValue = value;
    setup->wIndex = index;
    setup->wLength = length;
}

static inline struct libusb_control_setup *
libusb_control_transfer_get_setup(struct libusb_transfer *transfer) {
    return (struct libusb_control_setup *)(void *)transfer->buffer;
}

static inline unsigned char *
libusb_control_transfer_get_data(struct libusb_transfer *transfer) {
    return transfer->buffer + LIBUSB_CONTROL_SETUP_SIZE;
}

static inline void libusb_fill_control_transfer(
    struct libusb_transfer *transfer, libusb_device_handle *handle,
    unsigned char *buffer, libusb_transfer_cb_fn callback, void *user,
    unsigned int timeout) {
    struct libusb_control_setup *setup =
        (struct libusb_control_setup *)(void *)buffer;
    transfer->dev_handle = handle;
    transfer->buffer = buffer;
    transfer->length = (int)(LIBUSB_CONTROL_SETUP_SIZE + setup->wLength);
    transfer->callback = callback;
    transfer->user_data = user;
    transfer->timeout = timeout;
    transfer->type = 0u;
}

static inline void libusb_fill_bulk_transfer(
    struct libusb_transfer *transfer, libusb_device_handle *handle,
    unsigned char endpoint, unsigned char *buffer, int length,
    libusb_transfer_cb_fn callback, void *user, unsigned int timeout) {
    transfer->dev_handle = handle;
    transfer->endpoint = endpoint;
    transfer->buffer = buffer;
    transfer->length = length;
    transfer->callback = callback;
    transfer->user_data = user;
    transfer->timeout = timeout;
    transfer->type = 2u;
}

static inline void libusb_fill_interrupt_transfer(
    struct libusb_transfer *transfer, libusb_device_handle *handle,
    unsigned char endpoint, unsigned char *buffer, int length,
    libusb_transfer_cb_fn callback, void *user, unsigned int timeout) {
    libusb_fill_bulk_transfer(transfer, handle, endpoint, buffer, length,
                              callback, user, timeout);
    transfer->type = 3u;
}

int libusb_init(libusb_context **context);
void libusb_exit(libusb_context *context);
ssize_t libusb_get_device_list(libusb_context *context,
                               libusb_device ***devices);
void libusb_free_device_list(libusb_device **devices, int unref_devices);
int libusb_get_device_descriptor(libusb_device *device,
                                 struct libusb_device_descriptor *descriptor);
uint8_t libusb_get_bus_number(libusb_device *device);
uint8_t libusb_get_device_address(libusb_device *device);
uint8_t libusb_get_port_number(libusb_device *device);
int libusb_open(libusb_device *device, libusb_device_handle **handle);
void libusb_close(libusb_device_handle *handle);
int libusb_set_configuration(libusb_device_handle *handle, int configuration);
int libusb_claim_interface(libusb_device_handle *handle, int interface_number);
int libusb_release_interface(libusb_device_handle *handle,
                             int interface_number);
struct libusb_transfer *libusb_alloc_transfer(int iso_packets);
void libusb_free_transfer(struct libusb_transfer *transfer);
int libusb_submit_transfer(struct libusb_transfer *transfer);
int libusb_cancel_transfer(struct libusb_transfer *transfer);
int libusb_handle_events_timeout_completed(libusb_context *context,
                                            struct timeval *timeout,
                                            int *completed);
void libusb_interrupt_event_handler(libusb_context *context);
int libusb_has_capability(uint32_t capability);
int libusb_hotplug_register_callback(
    libusb_context *context, int events, int flags, int vendor_id,
    int product_id, int device_class, libusb_hotplug_callback_fn callback,
    void *user, libusb_hotplug_callback_handle *handle);
void libusb_hotplug_deregister_callback(
    libusb_context *context, libusb_hotplug_callback_handle handle);

void fake_libusb_allow_completion(int allow);
void fake_libusb_emit_hotplug(libusb_hotplug_event event);

#ifdef __cplusplus
}
#endif

#endif
