#include <libusb.h>

#include <turbo/thread.h>

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

enum { FAKE_PENDING_CAPACITY = 16 };

struct libusb_device {
    int present;
};

struct libusb_device_handle {
    int configuration;
    int claimed_interface;
};

struct libusb_context {
    turbo_mutex_t gate;
    struct libusb_transfer *pending[FAKE_PENDING_CAPACITY];
    size_t pending_count;
    libusb_hotplug_callback_fn hotplug;
    void *hotplug_user;
};

static struct libusb_device fake_device = {1};
static libusb_context *fake_last_context;
static atomic_int fake_completion_allowed;

void fake_libusb_allow_completion(int allow) {
    atomic_store(&fake_completion_allowed, allow);
}

void fake_libusb_emit_hotplug(libusb_hotplug_event event) {
    if (fake_last_context != NULL && fake_last_context->hotplug != NULL)
        (void)fake_last_context->hotplug(fake_last_context, &fake_device,
                                         event,
                                         fake_last_context->hotplug_user);
}

int libusb_init(libusb_context **context) {
    libusb_context *created;
    if (context == NULL)
        return LIBUSB_ERROR_INVALID_PARAM;
    created = (libusb_context *)calloc(1u, sizeof(*created));
    if (created == NULL)
        return LIBUSB_ERROR_NO_MEM;
    turbo_mutex_init(&created->gate);
    atomic_store(&fake_completion_allowed, 0);
    fake_last_context = created;
    *context = created;
    return LIBUSB_SUCCESS;
}

void libusb_exit(libusb_context *context) {
    if (context == NULL)
        return;
    if (fake_last_context == context)
        fake_last_context = NULL;
    turbo_mutex_destroy(&context->gate);
    free(context);
}

ssize_t libusb_get_device_list(libusb_context *context,
                               libusb_device ***devices) {
    (void)context;
    *devices = (libusb_device **)calloc(2u, sizeof(**devices));
    if (*devices == NULL)
        return LIBUSB_ERROR_NO_MEM;
    (*devices)[0] = &fake_device;
    return 1;
}

void libusb_free_device_list(libusb_device **devices, int unref_devices) {
    (void)unref_devices;
    free(devices);
}

int libusb_get_device_descriptor(
    libusb_device *device, struct libusb_device_descriptor *descriptor) {
    if (device == NULL || descriptor == NULL || !device->present)
        return LIBUSB_ERROR_NO_DEVICE;
    *descriptor = (struct libusb_device_descriptor){
        .idVendor = 0x1234u,
        .idProduct = 0x5678u,
        .bcdDevice = 0x0102u,
        .bDeviceClass = 0xffu,
        .bNumConfigurations = 1u,
    };
    return LIBUSB_SUCCESS;
}

uint8_t libusb_get_bus_number(libusb_device *device) {
    (void)device;
    return 1u;
}

uint8_t libusb_get_device_address(libusb_device *device) {
    (void)device;
    return 2u;
}

uint8_t libusb_get_port_number(libusb_device *device) {
    (void)device;
    return 3u;
}

int libusb_open(libusb_device *device, libusb_device_handle **handle) {
    if (device == NULL || handle == NULL || !device->present)
        return LIBUSB_ERROR_NO_DEVICE;
    *handle = (libusb_device_handle *)calloc(1u, sizeof(**handle));
    return *handle != NULL ? LIBUSB_SUCCESS : LIBUSB_ERROR_NO_MEM;
}

void libusb_close(libusb_device_handle *handle) {
    free(handle);
}

int libusb_set_configuration(libusb_device_handle *handle, int configuration) {
    if (handle == NULL)
        return LIBUSB_ERROR_NO_DEVICE;
    handle->configuration = configuration;
    return LIBUSB_SUCCESS;
}

int libusb_claim_interface(libusb_device_handle *handle, int interface_number) {
    if (handle == NULL)
        return LIBUSB_ERROR_NO_DEVICE;
    handle->claimed_interface = interface_number + 1;
    return LIBUSB_SUCCESS;
}

int libusb_release_interface(libusb_device_handle *handle,
                             int interface_number) {
    if (handle == NULL)
        return LIBUSB_ERROR_NO_DEVICE;
    if (handle->claimed_interface != interface_number + 1)
        return LIBUSB_ERROR_NOT_FOUND;
    handle->claimed_interface = 0;
    return LIBUSB_SUCCESS;
}

struct libusb_transfer *libusb_alloc_transfer(int iso_packets) {
    (void)iso_packets;
    return (struct libusb_transfer *)calloc(1u,
                                             sizeof(struct libusb_transfer));
}

void libusb_free_transfer(struct libusb_transfer *transfer) {
    free(transfer);
}

int libusb_submit_transfer(struct libusb_transfer *transfer) {
    libusb_context *context = fake_last_context;
    if (context == NULL || transfer == NULL || transfer->dev_handle == NULL)
        return LIBUSB_ERROR_INVALID_PARAM;
    turbo_mutex_lock(&context->gate);
    if (context->pending_count == FAKE_PENDING_CAPACITY) {
        turbo_mutex_unlock(&context->gate);
        return LIBUSB_ERROR_BUSY;
    }
    context->pending[context->pending_count++] = transfer;
    turbo_mutex_unlock(&context->gate);
    return LIBUSB_SUCCESS;
}

int libusb_cancel_transfer(struct libusb_transfer *transfer) {
    libusb_context *context = fake_last_context;
    if (transfer == NULL)
        return LIBUSB_ERROR_NOT_FOUND;
    turbo_mutex_lock(&context->gate);
    transfer->status = LIBUSB_TRANSFER_CANCELLED;
    turbo_mutex_unlock(&context->gate);
    atomic_store(&fake_completion_allowed, 1);
    return LIBUSB_SUCCESS;
}

int libusb_handle_events_timeout_completed(libusb_context *context,
                                            struct timeval *timeout,
                                            int *completed) {
    struct libusb_transfer *transfer = NULL;
    (void)timeout;
    (void)completed;
    turbo_sleep_ms(1u);
    if (!atomic_load(&fake_completion_allowed))
        return LIBUSB_SUCCESS;
    turbo_mutex_lock(&context->gate);
    if (context->pending_count != 0u) {
        transfer = context->pending[0];
        --context->pending_count;
        memmove(&context->pending[0], &context->pending[1],
                context->pending_count * sizeof(context->pending[0]));
    }
    turbo_mutex_unlock(&context->gate);
    if (transfer == NULL)
        return LIBUSB_SUCCESS;
    if (transfer->status != LIBUSB_TRANSFER_CANCELLED)
        transfer->status = LIBUSB_TRANSFER_COMPLETED;
    if (transfer->type == 0u) {
        struct libusb_control_setup *setup =
            libusb_control_transfer_get_setup(transfer);
        transfer->actual_length = setup->wLength;
        if ((setup->bmRequestType & LIBUSB_ENDPOINT_IN) != 0u)
            memset(libusb_control_transfer_get_data(transfer), 0xa5,
                   setup->wLength);
    } else {
        transfer->actual_length = transfer->length;
    }
    transfer->callback(transfer);
    return LIBUSB_SUCCESS;
}

void libusb_interrupt_event_handler(libusb_context *context) {
    (void)context;
}

int libusb_has_capability(uint32_t capability) {
    return capability == LIBUSB_CAP_HAS_HOTPLUG;
}

int libusb_hotplug_register_callback(
    libusb_context *context, int events, int flags, int vendor_id,
    int product_id, int device_class, libusb_hotplug_callback_fn callback,
    void *user, libusb_hotplug_callback_handle *handle) {
    (void)events;
    (void)flags;
    (void)vendor_id;
    (void)product_id;
    (void)device_class;
    context->hotplug = callback;
    context->hotplug_user = user;
    if (handle != NULL)
        *handle = 1;
    return LIBUSB_SUCCESS;
}

void libusb_hotplug_deregister_callback(
    libusb_context *context, libusb_hotplug_callback_handle handle) {
    (void)handle;
    context->hotplug = NULL;
    context->hotplug_user = NULL;
}
