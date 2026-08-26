#include <cflow/usb.h>

#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <libusb.h>

#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct cflow_usb_impl cflow_usb_impl;
typedef struct cflow_usb_device_impl cflow_usb_device_impl;

typedef enum cflow_usb_slot_state {
    CFLOW_USB_SLOT_FREE = 0,
    CFLOW_USB_SLOT_SUBMITTED,
    CFLOW_USB_SLOT_TERMINAL
} cflow_usb_slot_state;

typedef struct cflow_usb_transfer_slot {
    cflow_usb_impl *owner;
    cflow_usb_device_impl *device;
    struct libusb_transfer *native;
    unsigned char *control_storage;
    void *borrowed_buffer;
    size_t requested_length;
    cflow_usb_transfer_id id;
    cflow_usb_transfer_kind kind;
    cflow_usb_transfer_fn complete;
    void *complete_user;
    int terminal_status;
    size_t bytes_transferred;
    cflow_usb_slot_state state;
    bool accepted;
} cflow_usb_transfer_slot;

struct cflow_usb_device_impl {
    cflow_usb_impl *owner;
    cflow_usb_device_impl *next;
    libusb_device_handle *native;
    cflow_usb_device_identity identity;
    size_t active_transfers;
    size_t claimed_count;
    bool claimed[UINT8_MAX + 1u];
    bool lost;
};

struct cflow_usb_impl {
    libusb_context *native;
    cflow_usb_transfer_slot *transfers;
    unsigned char *control_storage;
    cflow_usb_hotplug_event *hotplug_events;
    cflow_usb_device_impl *devices;
    size_t device_capacity;
    size_t transfer_capacity;
    size_t control_payload_capacity;
    size_t hotplug_capacity;
    size_t hotplug_head;
    size_t hotplug_tail;
    size_t hotplug_count;
    size_t delivery_cursor;
    size_t active_devices;
    size_t active_transfers;
    size_t enumerations;
    size_t devices_observed;
    size_t enumeration_overflow;
    size_t transfers_delivered;
    size_t hotplug_suppressed;
    size_t hotplug_rescan_required;
    size_t hotplug_rescan_delivery_suppressed;
    cflow_usb_transfer_id next_id;
    unsigned int event_poll_timeout_ms;
    cflow_usb_context_config config;
    turbo_mutex_t gate;
    turbo_thread_t event_thread;
    libusb_hotplug_callback_handle hotplug_handle;
    atomic_bool stop_requested;
    atomic_bool driver_active;
    bool event_thread_started;
    bool hotplug_registered;
    bool hotplug_awaiting_rescan;
    bool hotplug_rescan_pending;
    bool hotplug_rescan_delivered;
    int event_thread_status;
};

static size_t usb_saturating_add(size_t left, size_t right) {
    return right > SIZE_MAX - left ? SIZE_MAX : left + right;
}

static bool usb_multiply(size_t left, size_t right, size_t *out) {
    if (out == NULL || (right != 0u && left > SIZE_MAX / right))
        return false;
    *out = left * right;
    return true;
}

static int usb_error(int status) {
    switch (status) {
        case LIBUSB_SUCCESS: return TURBO_OK;
        case LIBUSB_ERROR_IO: return TURBO_EIO;
        case LIBUSB_ERROR_INVALID_PARAM: return TURBO_EINVAL;
        case LIBUSB_ERROR_ACCESS: return TURBO_EPERM;
        case LIBUSB_ERROR_NO_DEVICE: return TURBO_ENODEV;
        case LIBUSB_ERROR_NOT_FOUND: return TURBO_ENOENT;
        case LIBUSB_ERROR_BUSY: return TURBO_EBUSY;
        case LIBUSB_ERROR_TIMEOUT: return TURBO_ETIMEDOUT;
        case LIBUSB_ERROR_OVERFLOW: return TURBO_ERANGE;
        case LIBUSB_ERROR_PIPE: return TURBO_EPIPE;
        case LIBUSB_ERROR_INTERRUPTED: return TURBO_EINTR;
        case LIBUSB_ERROR_NO_MEM: return TURBO_ENOMEM;
        case LIBUSB_ERROR_NOT_SUPPORTED: return TURBO_ENOTSUP;
        default: return TURBO_UNKNOWN;
    }
}

static int usb_transfer_status(enum libusb_transfer_status status) {
    switch (status) {
        case LIBUSB_TRANSFER_COMPLETED: return TURBO_OK;
        case LIBUSB_TRANSFER_TIMED_OUT: return TURBO_ETIMEDOUT;
        case LIBUSB_TRANSFER_CANCELLED: return TURBO_ECANCELED;
        case LIBUSB_TRANSFER_STALL: return TURBO_EPIPE;
        case LIBUSB_TRANSFER_NO_DEVICE: return TURBO_ENODEV;
        case LIBUSB_TRANSFER_OVERFLOW: return TURBO_ERANGE;
        default: return TURBO_EIO;
    }
}

static bool usb_identity_equal(const cflow_usb_device_identity *left,
                               const cflow_usb_device_identity *right) {
    return left->bus_number == right->bus_number &&
           left->device_address == right->device_address &&
           left->vendor_id == right->vendor_id &&
           left->product_id == right->product_id;
}

static bool usb_device_identity(libusb_device *device,
                                cflow_usb_device_identity *identity) {
    struct libusb_device_descriptor descriptor;
    if (libusb_get_device_descriptor(device, &descriptor) != LIBUSB_SUCCESS)
        return false;
    *identity = (cflow_usb_device_identity){
        .bus_number = libusb_get_bus_number(device),
        .device_address = libusb_get_device_address(device),
        .vendor_id = descriptor.idVendor,
        .product_id = descriptor.idProduct,
    };
    return true;
}

static void usb_publish_hotplug(cflow_usb_impl *impl,
                                const cflow_usb_hotplug_event *event) {
    turbo_mutex_lock(&impl->gate);
    if (atomic_load(&impl->stop_requested)) {
        impl->hotplug_suppressed =
            usb_saturating_add(impl->hotplug_suppressed, 1u);
        turbo_mutex_unlock(&impl->gate);
        return;
    }
    if (event->kind == CFLOW_USB_HOTPLUG_RESCAN_REQUIRED) {
        impl->hotplug_suppressed =
            usb_saturating_add(impl->hotplug_suppressed, 1u);
        if (!impl->hotplug_awaiting_rescan) {
            impl->hotplug_awaiting_rescan = true;
            impl->hotplug_rescan_pending = true;
            impl->hotplug_rescan_delivered = false;
            impl->hotplug_rescan_required =
                usb_saturating_add(impl->hotplug_rescan_required, 1u);
        }
        turbo_mutex_unlock(&impl->gate);
        return;
    }
    if (event->kind == CFLOW_USB_HOTPLUG_LEFT) {
        cflow_usb_device_impl *device = impl->devices;
        size_t index;
        while (device != NULL) {
            if (usb_identity_equal(&device->identity, &event->identity))
                device->lost = true;
            device = device->next;
        }
        for (index = 0u; index < impl->transfer_capacity; ++index) {
            cflow_usb_transfer_slot *slot = &impl->transfers[index];
            if (slot->state == CFLOW_USB_SLOT_SUBMITTED &&
                usb_identity_equal(&slot->device->identity,
                                   &event->identity))
                (void)libusb_cancel_transfer(slot->native);
        }
    }
    if (impl->hotplug_awaiting_rescan ||
        impl->hotplug_count == impl->hotplug_capacity) {
        impl->hotplug_suppressed =
            usb_saturating_add(impl->hotplug_suppressed, 1u);
        if (!impl->hotplug_awaiting_rescan) {
            impl->hotplug_awaiting_rescan = true;
            impl->hotplug_rescan_pending = true;
            impl->hotplug_rescan_delivered = false;
            impl->hotplug_rescan_required =
                usb_saturating_add(impl->hotplug_rescan_required, 1u);
        }
        turbo_mutex_unlock(&impl->gate);
        return;
    }
    impl->hotplug_events[impl->hotplug_tail] = *event;
    impl->hotplug_tail = (impl->hotplug_tail + 1u) % impl->hotplug_capacity;
    ++impl->hotplug_count;
    turbo_mutex_unlock(&impl->gate);
}

static int LIBUSB_CALL usb_hotplug_callback(libusb_context *native,
                                             libusb_device *device,
                                             libusb_hotplug_event native_event,
                                             void *user) {
    cflow_usb_impl *impl = (cflow_usb_impl *)user;
    cflow_usb_hotplug_event event;
    (void)native;
    if (!usb_device_identity(device, &event.identity)) {
        event = (cflow_usb_hotplug_event){
            .kind = CFLOW_USB_HOTPLUG_RESCAN_REQUIRED,
        };
    } else {
        event.kind = native_event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED
            ? CFLOW_USB_HOTPLUG_ARRIVED : CFLOW_USB_HOTPLUG_LEFT;
    }
    usb_publish_hotplug(impl, &event);
    return 0;
}

static void LIBUSB_CALL usb_transfer_callback(struct libusb_transfer *native) {
    cflow_usb_transfer_slot *slot =
        (cflow_usb_transfer_slot *)native->user_data;
    cflow_usb_impl *impl = slot->owner;
    size_t actual = native->actual_length > 0
        ? (size_t)native->actual_length : 0u;
    if (slot->kind == CFLOW_USB_TRANSFER_CONTROL &&
        native->status == LIBUSB_TRANSFER_COMPLETED &&
        slot->borrowed_buffer != NULL &&
        (libusb_control_transfer_get_setup(native)->bmRequestType &
         LIBUSB_ENDPOINT_IN) != 0u) {
        if (actual > slot->requested_length)
            actual = slot->requested_length;
        memcpy(slot->borrowed_buffer,
               libusb_control_transfer_get_data(native), actual);
    }
    turbo_mutex_lock(&impl->gate);
    if (slot->state == CFLOW_USB_SLOT_SUBMITTED) {
        slot->terminal_status = usb_transfer_status(native->status);
        slot->bytes_transferred = actual;
        slot->state = CFLOW_USB_SLOT_TERMINAL;
    }
    turbo_mutex_unlock(&impl->gate);
}

static void usb_event_thread(void *user) {
    cflow_usb_impl *impl = (cflow_usb_impl *)user;
    while (!atomic_load(&impl->stop_requested)) {
        struct timeval timeout;
        int status;
        timeout.tv_sec = (long)(impl->event_poll_timeout_ms / 1000u);
        timeout.tv_usec =
            (long)((impl->event_poll_timeout_ms % 1000u) * 1000u);
        status = libusb_handle_events_timeout_completed(impl->native,
                                                        &timeout, NULL);
        if (status != LIBUSB_SUCCESS && status != LIBUSB_ERROR_INTERRUPTED) {
            turbo_mutex_lock(&impl->gate);
            impl->event_thread_status = usb_error(status);
            turbo_mutex_unlock(&impl->gate);
            break;
        }
    }
}

static void usb_free_context(cflow_usb_impl *impl, size_t allocated) {
    size_t index;
    if (impl == NULL)
        return;
    for (index = 0u; index < allocated; ++index)
        libusb_free_transfer(impl->transfers[index].native);
    free(impl->hotplug_events);
    free(impl->control_storage);
    free(impl->transfers);
    if (impl->native != NULL)
        libusb_exit(impl->native);
    free(impl);
}

int cflow_usb_context_init(cflow_usb_context *context,
                           const cflow_usb_context_config *config) {
    cflow_usb_impl *impl;
    size_t storage_stride;
    size_t storage_bytes;
    size_t index;
    int status;
    if (context == NULL || context->impl != NULL || config == NULL ||
        config->device_capacity == 0u || config->transfer_capacity == 0u ||
        config->control_payload_capacity == 0u ||
        config->control_payload_capacity > UINT16_MAX ||
        config->hotplug_capacity == 0u ||
        config->event_poll_timeout_ms == 0u ||
        config->event_poll_timeout_ms > 1000u ||
        config->transfer_capacity >
            SIZE_MAX / sizeof(cflow_usb_transfer_slot) ||
        config->hotplug_capacity >
            SIZE_MAX / sizeof(cflow_usb_hotplug_event) ||
        config->control_payload_capacity >
            SIZE_MAX - LIBUSB_CONTROL_SETUP_SIZE)
        return TURBO_EINVAL;
    storage_stride =
        config->control_payload_capacity + LIBUSB_CONTROL_SETUP_SIZE;
    if (!usb_multiply(config->transfer_capacity, storage_stride,
                      &storage_bytes))
        return TURBO_EINVAL;
    impl = (cflow_usb_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return TURBO_ENOMEM;
    status = libusb_init(&impl->native);
    if (status != LIBUSB_SUCCESS) {
        free(impl);
        return usb_error(status);
    }
    impl->transfers = (cflow_usb_transfer_slot *)calloc(
        config->transfer_capacity, sizeof(*impl->transfers));
    impl->control_storage = (unsigned char *)calloc(1u, storage_bytes);
    impl->hotplug_events = (cflow_usb_hotplug_event *)calloc(
        config->hotplug_capacity, sizeof(*impl->hotplug_events));
    if (impl->transfers == NULL || impl->control_storage == NULL ||
        impl->hotplug_events == NULL) {
        usb_free_context(impl, 0u);
        return TURBO_ENOMEM;
    }
    for (index = 0u; index < config->transfer_capacity; ++index) {
        cflow_usb_transfer_slot *slot = &impl->transfers[index];
        slot->native = libusb_alloc_transfer(0);
        if (slot->native == NULL) {
            usb_free_context(impl, index);
            return TURBO_ENOMEM;
        }
        slot->owner = impl;
        slot->control_storage =
            impl->control_storage + index * storage_stride;
    }
    impl->device_capacity = config->device_capacity;
    impl->transfer_capacity = config->transfer_capacity;
    impl->control_payload_capacity = config->control_payload_capacity;
    impl->hotplug_capacity = config->hotplug_capacity;
    impl->event_poll_timeout_ms = config->event_poll_timeout_ms;
    impl->config = *config;
    impl->next_id = 1u;
    impl->event_thread_status = TURBO_OK;
    turbo_mutex_init(&impl->gate);
    atomic_init(&impl->stop_requested, false);
    atomic_init(&impl->driver_active, false);
    if (config->hotplug != NULL) {
        if (!libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
            turbo_mutex_destroy(&impl->gate);
            usb_free_context(impl, config->transfer_capacity);
            return TURBO_ENOTSUP;
        }
        status = libusb_hotplug_register_callback(
            impl->native,
            LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
                LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
            LIBUSB_HOTPLUG_NO_FLAGS, LIBUSB_HOTPLUG_MATCH_ANY,
            LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY,
            usb_hotplug_callback, impl, &impl->hotplug_handle);
        if (status != LIBUSB_SUCCESS) {
            turbo_mutex_destroy(&impl->gate);
            usb_free_context(impl, config->transfer_capacity);
            return usb_error(status);
        }
        impl->hotplug_registered = true;
    }
    status = turbo_thread_create(&impl->event_thread, usb_event_thread, impl);
    if (status != TURBO_OK) {
        if (impl->hotplug_registered)
            libusb_hotplug_deregister_callback(impl->native,
                                               impl->hotplug_handle);
        turbo_mutex_destroy(&impl->gate);
        usb_free_context(impl, config->transfer_capacity);
        return status;
    }
    impl->event_thread_started = true;
    context->impl = impl;
    return TURBO_OK;
}

int cflow_usb_enumerate(cflow_usb_context *context,
                        cflow_usb_device_info *out,
                        size_t out_capacity,
                        size_t *out_count) {
    cflow_usb_impl *impl;
    libusb_device **devices = NULL;
    ssize_t count;
    size_t required;
    size_t index;
    int status = TURBO_OK;
    if (context == NULL || context->impl == NULL || out_count == NULL ||
        ((out == NULL) != (out_capacity == 0u)))
        return TURBO_EINVAL;
    impl = (cflow_usb_impl *)context->impl;
    if (atomic_load(&impl->stop_requested))
        return TURBO_ESHUTDOWN;
    *out_count = 0u;
    count = libusb_get_device_list(impl->native, &devices);
    if (count < 0)
        return usb_error((int)count);
    required = (size_t)count;
    turbo_mutex_lock(&impl->gate);
    impl->enumerations = usb_saturating_add(impl->enumerations, 1u);
    impl->devices_observed =
        usb_saturating_add(impl->devices_observed, required);
    if (required > impl->device_capacity || required > out_capacity) {
        impl->enumeration_overflow =
            usb_saturating_add(impl->enumeration_overflow, 1u);
        status = TURBO_ENOBUFS;
    }
    turbo_mutex_unlock(&impl->gate);
    *out_count = required;
    if (status != TURBO_OK) {
        libusb_free_device_list(devices, 1);
        return status;
    }
    for (index = 0u; index < required; ++index) {
        struct libusb_device_descriptor descriptor;
        int descriptor_status =
            libusb_get_device_descriptor(devices[index], &descriptor);
        if (descriptor_status != LIBUSB_SUCCESS) {
            status = usb_error(descriptor_status);
            break;
        }
        out[index] = (cflow_usb_device_info){
            .bus_number = libusb_get_bus_number(devices[index]),
            .device_address = libusb_get_device_address(devices[index]),
            .port_number = libusb_get_port_number(devices[index]),
            .vendor_id = descriptor.idVendor,
            .product_id = descriptor.idProduct,
            .device_version = descriptor.bcdDevice,
            .device_class = descriptor.bDeviceClass,
            .device_subclass = descriptor.bDeviceSubClass,
            .device_protocol = descriptor.bDeviceProtocol,
            .configuration_count = descriptor.bNumConfigurations,
        };
    }
    libusb_free_device_list(devices, 1);
    if (status != TURBO_OK) {
        *out_count = 0u;
        return status;
    }
    return TURBO_OK;
}

int cflow_usb_device_open(cflow_usb_context *context,
                          const cflow_usb_device_identity *identity,
                          cflow_usb_device *device) {
    cflow_usb_impl *impl;
    cflow_usb_device_impl *opened;
    libusb_device **devices = NULL;
    libusb_device_handle *native = NULL;
    ssize_t count;
    ssize_t index;
    int status = LIBUSB_ERROR_NO_DEVICE;
    if (context == NULL || context->impl == NULL || identity == NULL ||
        device == NULL || device->impl != NULL)
        return TURBO_EINVAL;
    impl = (cflow_usb_impl *)context->impl;
    if (atomic_load(&impl->stop_requested))
        return TURBO_ESHUTDOWN;
    count = libusb_get_device_list(impl->native, &devices);
    if (count < 0)
        return usb_error((int)count);
    for (index = 0; index < count; ++index) {
        cflow_usb_device_identity observed;
        if (!usb_device_identity(devices[index], &observed) ||
            !usb_identity_equal(identity, &observed))
            continue;
        status = libusb_open(devices[index], &native);
        break;
    }
    libusb_free_device_list(devices, 1);
    if (status != LIBUSB_SUCCESS)
        return usb_error(status);
    opened = (cflow_usb_device_impl *)calloc(1u, sizeof(*opened));
    if (opened == NULL) {
        libusb_close(native);
        return TURBO_ENOMEM;
    }
    opened->owner = impl;
    opened->native = native;
    opened->identity = *identity;
    turbo_mutex_lock(&impl->gate);
    if (impl->active_devices == impl->device_capacity) {
        turbo_mutex_unlock(&impl->gate);
        libusb_close(native);
        free(opened);
        return TURBO_ENOBUFS;
    }
    opened->next = impl->devices;
    impl->devices = opened;
    ++impl->active_devices;
    turbo_mutex_unlock(&impl->gate);
    device->impl = opened;
    return TURBO_OK;
}

int cflow_usb_device_set_configuration(cflow_usb_device *device,
                                       int configuration) {
    cflow_usb_device_impl *impl;
    int status;
    if (device == NULL || device->impl == NULL || configuration < 0)
        return TURBO_EINVAL;
    impl = (cflow_usb_device_impl *)device->impl;
    turbo_mutex_lock(&impl->owner->gate);
    if (impl->lost || impl->claimed_count != 0u ||
        impl->active_transfers != 0u) {
        status = impl->lost ? TURBO_ENODEV : TURBO_EBUSY;
        turbo_mutex_unlock(&impl->owner->gate);
        return status;
    }
    turbo_mutex_unlock(&impl->owner->gate);
    return usb_error(libusb_set_configuration(impl->native, configuration));
}

int cflow_usb_device_claim_interface(cflow_usb_device *device,
                                     uint8_t interface_number) {
    cflow_usb_device_impl *impl;
    int status;
    if (device == NULL || device->impl == NULL)
        return TURBO_EINVAL;
    impl = (cflow_usb_device_impl *)device->impl;
    turbo_mutex_lock(&impl->owner->gate);
    if (impl->lost || impl->claimed[interface_number]) {
        status = impl->lost ? TURBO_ENODEV : TURBO_EALREADY;
        turbo_mutex_unlock(&impl->owner->gate);
        return status;
    }
    turbo_mutex_unlock(&impl->owner->gate);
    status = libusb_claim_interface(impl->native, (int)interface_number);
    if (status != LIBUSB_SUCCESS)
        return usb_error(status);
    turbo_mutex_lock(&impl->owner->gate);
    impl->claimed[interface_number] = true;
    ++impl->claimed_count;
    turbo_mutex_unlock(&impl->owner->gate);
    return TURBO_OK;
}

int cflow_usb_device_release_interface(cflow_usb_device *device,
                                       uint8_t interface_number) {
    cflow_usb_device_impl *impl;
    int status;
    if (device == NULL || device->impl == NULL)
        return TURBO_EINVAL;
    impl = (cflow_usb_device_impl *)device->impl;
    turbo_mutex_lock(&impl->owner->gate);
    if (!impl->claimed[interface_number]) {
        turbo_mutex_unlock(&impl->owner->gate);
        return TURBO_ENOENT;
    }
    if (impl->active_transfers != 0u) {
        turbo_mutex_unlock(&impl->owner->gate);
        return TURBO_EBUSY;
    }
    turbo_mutex_unlock(&impl->owner->gate);
    status = libusb_release_interface(impl->native, (int)interface_number);
    if (status != LIBUSB_SUCCESS && status != LIBUSB_ERROR_NO_DEVICE)
        return usb_error(status);
    turbo_mutex_lock(&impl->owner->gate);
    impl->claimed[interface_number] = false;
    --impl->claimed_count;
    turbo_mutex_unlock(&impl->owner->gate);
    return status == LIBUSB_ERROR_NO_DEVICE ? TURBO_ENODEV : TURBO_OK;
}

int cflow_usb_device_close(cflow_usb_device *device) {
    cflow_usb_device_impl *impl;
    cflow_usb_device_impl **link;
    if (device == NULL || device->impl == NULL)
        return TURBO_EINVAL;
    impl = (cflow_usb_device_impl *)device->impl;
    turbo_mutex_lock(&impl->owner->gate);
    if (impl->active_transfers != 0u || impl->claimed_count != 0u) {
        turbo_mutex_unlock(&impl->owner->gate);
        return TURBO_EBUSY;
    }
    link = &impl->owner->devices;
    while (*link != NULL && *link != impl)
        link = &(*link)->next;
    if (*link == NULL) {
        turbo_mutex_unlock(&impl->owner->gate);
        return TURBO_EINVAL;
    }
    *link = impl->next;
    --impl->owner->active_devices;
    turbo_mutex_unlock(&impl->owner->gate);
    libusb_close(impl->native);
    free(impl);
    device->impl = NULL;
    return TURBO_OK;
}

static bool usb_request_valid(const cflow_usb_impl *impl,
                              const cflow_usb_transfer_request *request) {
    if (request == NULL || request->complete == NULL ||
        (request->length != 0u && request->buffer == NULL) ||
        request->length > (size_t)INT_MAX ||
        request->kind > CFLOW_USB_TRANSFER_INTERRUPT)
        return false;
    if (request->kind == CFLOW_USB_TRANSFER_CONTROL)
        return request->length <= impl->control_payload_capacity &&
               request->length <= UINT16_MAX;
    return (request->endpoint & LIBUSB_ENDPOINT_ADDRESS_MASK) != 0u;
}

int cflow_usb_submit(cflow_usb_context *context,
                     cflow_usb_device *device,
                     const cflow_usb_transfer_request *request,
                     cflow_usb_transfer_id *out_id) {
    cflow_usb_impl *impl;
    cflow_usb_device_impl *device_impl;
    cflow_usb_transfer_slot *slot = NULL;
    size_t index;
    int status;
    if (out_id != NULL)
        *out_id = CFLOW_USB_INVALID_TRANSFER_ID;
    if (context == NULL || context->impl == NULL || device == NULL ||
        device->impl == NULL || out_id == NULL)
        return TURBO_EINVAL;
    impl = (cflow_usb_impl *)context->impl;
    device_impl = (cflow_usb_device_impl *)device->impl;
    if (device_impl->owner != impl || !usb_request_valid(impl, request))
        return TURBO_EINVAL;
    turbo_mutex_lock(&impl->gate);
    if (atomic_load(&impl->stop_requested) ||
        impl->event_thread_status != TURBO_OK || device_impl->lost) {
        status = atomic_load(&impl->stop_requested) ? TURBO_ESHUTDOWN
            : (device_impl->lost ? TURBO_ENODEV : impl->event_thread_status);
        turbo_mutex_unlock(&impl->gate);
        return status;
    }
    if (impl->next_id == CFLOW_USB_INVALID_TRANSFER_ID) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_ERANGE;
    }
    for (index = 0u; index < impl->transfer_capacity; ++index) {
        if (impl->transfers[index].state == CFLOW_USB_SLOT_FREE) {
            slot = &impl->transfers[index];
            break;
        }
    }
    if (slot == NULL) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_ENOBUFS;
    }
    slot->device = device_impl;
    slot->borrowed_buffer = request->buffer;
    slot->requested_length = request->length;
    slot->id = impl->next_id++;
    slot->kind = request->kind;
    slot->complete = request->complete;
    slot->complete_user = request->complete_user;
    slot->terminal_status = TURBO_EBUSY;
    slot->bytes_transferred = 0u;
    slot->state = CFLOW_USB_SLOT_SUBMITTED;
    slot->accepted = false;
    ++impl->active_transfers;
    ++device_impl->active_transfers;
    if (request->kind == CFLOW_USB_TRANSFER_CONTROL) {
        libusb_fill_control_setup(slot->control_storage,
                                  request->control.request_type,
                                  request->control.request,
                                  request->control.value,
                                  request->control.index,
                                  (uint16_t)request->length);
        if ((request->control.request_type & LIBUSB_ENDPOINT_IN) == 0u &&
            request->length != 0u)
            memcpy(slot->control_storage + LIBUSB_CONTROL_SETUP_SIZE,
                   request->buffer, request->length);
        libusb_fill_control_transfer(slot->native, device_impl->native,
                                     slot->control_storage,
                                     usb_transfer_callback, slot,
                                     request->timeout_ms);
    } else if (request->kind == CFLOW_USB_TRANSFER_BULK) {
        libusb_fill_bulk_transfer(slot->native, device_impl->native,
                                  request->endpoint,
                                  (unsigned char *)request->buffer,
                                  (int)request->length,
                                  usb_transfer_callback, slot,
                                  request->timeout_ms);
    } else {
        libusb_fill_interrupt_transfer(slot->native, device_impl->native,
                                       request->endpoint,
                                       (unsigned char *)request->buffer,
                                       (int)request->length,
                                       usb_transfer_callback, slot,
                                       request->timeout_ms);
    }
    turbo_mutex_unlock(&impl->gate);
    status = libusb_submit_transfer(slot->native);
    if (status != LIBUSB_SUCCESS) {
        turbo_mutex_lock(&impl->gate);
        slot->state = CFLOW_USB_SLOT_FREE;
        slot->accepted = false;
        --impl->active_transfers;
        --device_impl->active_transfers;
        turbo_mutex_unlock(&impl->gate);
        return usb_error(status);
    }
    turbo_mutex_lock(&impl->gate);
    slot->accepted = true;
    *out_id = slot->id;
    turbo_mutex_unlock(&impl->gate);
    return TURBO_OK;
}

int cflow_usb_cancel(cflow_usb_context *context,
                     cflow_usb_transfer_id id) {
    cflow_usb_impl *impl;
    struct libusb_transfer *native = NULL;
    size_t index;
    int status;
    if (context == NULL || context->impl == NULL ||
        id == CFLOW_USB_INVALID_TRANSFER_ID)
        return TURBO_EINVAL;
    impl = (cflow_usb_impl *)context->impl;
    turbo_mutex_lock(&impl->gate);
    for (index = 0u; index < impl->transfer_capacity; ++index) {
        if (impl->transfers[index].id == id &&
            impl->transfers[index].state != CFLOW_USB_SLOT_FREE) {
            if (impl->transfers[index].state == CFLOW_USB_SLOT_TERMINAL) {
                turbo_mutex_unlock(&impl->gate);
                return TURBO_EALREADY;
            }
            native = impl->transfers[index].native;
            break;
        }
    }
    turbo_mutex_unlock(&impl->gate);
    if (native == NULL)
        return TURBO_ENOENT;
    status = libusb_cancel_transfer(native);
    return status == LIBUSB_ERROR_NOT_FOUND ? TURBO_EALREADY
                                            : usb_error(status);
}

static bool usb_take_transfer(cflow_usb_impl *impl,
                              cflow_usb_transfer_result *result,
                              cflow_usb_transfer_fn *complete,
                              void **complete_user) {
    size_t visited;
    for (visited = 0u; visited < impl->transfer_capacity; ++visited) {
        size_t index =
            (impl->delivery_cursor + visited) % impl->transfer_capacity;
        cflow_usb_transfer_slot *slot = &impl->transfers[index];
        if (slot->state != CFLOW_USB_SLOT_TERMINAL || !slot->accepted)
            continue;
        *result = (cflow_usb_transfer_result){
            .id = slot->id,
            .kind = slot->kind,
            .status = slot->terminal_status,
            .bytes_transferred = slot->bytes_transferred,
        };
        *complete = slot->complete;
        *complete_user = slot->complete_user;
        --slot->device->active_transfers;
        --impl->active_transfers;
        impl->transfers_delivered =
            usb_saturating_add(impl->transfers_delivered, 1u);
        slot->state = CFLOW_USB_SLOT_FREE;
        slot->accepted = false;
        slot->device = NULL;
        slot->borrowed_buffer = NULL;
        slot->complete = NULL;
        slot->complete_user = NULL;
        impl->delivery_cursor = (index + 1u) % impl->transfer_capacity;
        return true;
    }
    return false;
}

int cflow_usb_run_ready(cflow_usb_context *context, size_t max_events,
                        size_t *delivered) {
    cflow_usb_impl *impl;
    bool expected = false;
    size_t count = 0u;
    int runtime_status;
    if (context == NULL || context->impl == NULL || max_events == 0u ||
        delivered == NULL)
        return TURBO_EINVAL;
    impl = (cflow_usb_impl *)context->impl;
    *delivered = 0u;
    if (!atomic_compare_exchange_strong(&impl->driver_active, &expected, true))
        return TURBO_EBUSY;
    while (count < max_events) {
        cflow_usb_transfer_result result;
        cflow_usb_transfer_fn complete = NULL;
        void *complete_user = NULL;
        cflow_usb_hotplug_event hotplug;
        bool have_hotplug = false;
        turbo_mutex_lock(&impl->gate);
        if (!usb_take_transfer(impl, &result, &complete, &complete_user)) {
            if (impl->hotplug_count != 0u) {
                hotplug = impl->hotplug_events[impl->hotplug_head];
                impl->hotplug_head =
                    (impl->hotplug_head + 1u) % impl->hotplug_capacity;
                --impl->hotplug_count;
                have_hotplug = true;
            } else if (impl->hotplug_rescan_pending) {
                impl->hotplug_rescan_pending = false;
                impl->hotplug_rescan_delivered = true;
                impl->hotplug_rescan_delivery_suppressed =
                    impl->hotplug_suppressed;
                hotplug = (cflow_usb_hotplug_event){
                    .kind = CFLOW_USB_HOTPLUG_RESCAN_REQUIRED,
                };
                have_hotplug = true;
            }
        }
        turbo_mutex_unlock(&impl->gate);
        if (complete != NULL)
            complete(complete_user, &result);
        else if (have_hotplug && impl->config.hotplug != NULL)
            impl->config.hotplug(impl->config.hotplug_user, &hotplug);
        else
            break;
        ++count;
    }
    atomic_store(&impl->driver_active, false);
    *delivered = count;
    turbo_mutex_lock(&impl->gate);
    runtime_status = impl->event_thread_status;
    turbo_mutex_unlock(&impl->gate);
    return count == 0u && runtime_status != TURBO_OK
        ? runtime_status : TURBO_OK;
}

int cflow_usb_acknowledge_hotplug_rescan(cflow_usb_context *context) {
    cflow_usb_impl *impl;
    if (context == NULL || context->impl == NULL)
        return TURBO_EINVAL;
    impl = (cflow_usb_impl *)context->impl;
    turbo_mutex_lock(&impl->gate);
    if (!impl->hotplug_awaiting_rescan ||
        !impl->hotplug_rescan_delivered) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EALREADY;
    }
    if (impl->hotplug_suppressed !=
        impl->hotplug_rescan_delivery_suppressed) {
        impl->hotplug_rescan_pending = true;
        impl->hotplug_rescan_delivered = false;
        impl->hotplug_rescan_required = usb_saturating_add(
            impl->hotplug_rescan_required, 1u);
    } else {
        impl->hotplug_awaiting_rescan = false;
        impl->hotplug_rescan_delivered = false;
    }
    turbo_mutex_unlock(&impl->gate);
    return TURBO_OK;
}

bool cflow_usb_get_stats(const cflow_usb_context *context,
                         cflow_usb_stats *out) {
    const cflow_usb_impl *impl;
    if (context == NULL || context->impl == NULL || out == NULL)
        return false;
    impl = (const cflow_usb_impl *)context->impl;
    turbo_mutex_lock((turbo_mutex_t *)&impl->gate);
    *out = (cflow_usb_stats){
        .device_capacity = impl->device_capacity,
        .enumerations = impl->enumerations,
        .devices_observed = impl->devices_observed,
        .enumeration_overflow = impl->enumeration_overflow,
        .transfer_capacity = impl->transfer_capacity,
        .active_devices = impl->active_devices,
        .active_transfers = impl->active_transfers,
        .transfers_delivered = impl->transfers_delivered,
        .hotplug_queued = usb_saturating_add(
            impl->hotplug_count,
            impl->hotplug_rescan_pending ? 1u : 0u),
        .hotplug_suppressed = impl->hotplug_suppressed,
        .hotplug_rescan_required = impl->hotplug_rescan_required,
    };
    turbo_mutex_unlock((turbo_mutex_t *)&impl->gate);
    return true;
}

int cflow_usb_context_destroy(cflow_usb_context *context) {
    cflow_usb_impl *impl;
    size_t index;
    int status = TURBO_OK;
    if (context == NULL || context->impl == NULL)
        return TURBO_EINVAL;
    impl = (cflow_usb_impl *)context->impl;
    if (atomic_load(&impl->driver_active))
        return TURBO_EBUSY;
    turbo_mutex_lock(&impl->gate);
    if (impl->active_devices != 0u || impl->active_transfers != 0u ||
        impl->hotplug_count != 0u || impl->hotplug_rescan_pending) {
        turbo_mutex_unlock(&impl->gate);
        return TURBO_EBUSY;
    }
    atomic_store(&impl->stop_requested, true);
    turbo_mutex_unlock(&impl->gate);
    libusb_interrupt_event_handler(impl->native);
    if (impl->event_thread_started) {
        if (turbo_thread_join(&impl->event_thread) != TURBO_OK)
            return TURBO_EIO;
        turbo_thread_destroy(&impl->event_thread);
        impl->event_thread_started = false;
    }
    if (impl->hotplug_registered)
        libusb_hotplug_deregister_callback(impl->native,
                                           impl->hotplug_handle);
    for (index = 0u; index < impl->transfer_capacity; ++index)
        libusb_free_transfer(impl->transfers[index].native);
    turbo_mutex_destroy(&impl->gate);
    free(impl->hotplug_events);
    free(impl->control_storage);
    free(impl->transfers);
    libusb_exit(impl->native);
    free(impl);
    context->impl = NULL;
    return status;
}
