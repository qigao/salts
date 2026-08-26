#include <cflow/usb.h>

#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <libusb.h>

#include <stdint.h>
#include <stdlib.h>

typedef struct cflow_usb_impl {
    libusb_context *native;
    size_t device_capacity;
    size_t enumerations;
    size_t devices_observed;
    size_t enumeration_overflow;
    turbo_mutex_t gate;
} cflow_usb_impl;

static size_t usb_saturating_add(size_t left, size_t right) {
    return right > SIZE_MAX - left ? SIZE_MAX : left + right;
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

int cflow_usb_context_init(cflow_usb_context *context,
                           const cflow_usb_context_config *config) {
    cflow_usb_impl *impl;
    int status;
    if (context == NULL || context->impl != NULL || config == NULL ||
        config->device_capacity == 0u)
        return TURBO_EINVAL;
    impl = (cflow_usb_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return TURBO_ENOMEM;
    status = libusb_init(&impl->native);
    if (status != LIBUSB_SUCCESS) {
        free(impl);
        return usb_error(status);
    }
    turbo_mutex_init(&impl->gate);
    impl->device_capacity = config->device_capacity;
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
    };
    turbo_mutex_unlock((turbo_mutex_t *)&impl->gate);
    return true;
}

int cflow_usb_context_destroy(cflow_usb_context *context) {
    cflow_usb_impl *impl;
    if (context == NULL || context->impl == NULL)
        return TURBO_EINVAL;
    impl = (cflow_usb_impl *)context->impl;
    libusb_exit(impl->native);
    turbo_mutex_destroy(&impl->gate);
    free(impl);
    context->impl = NULL;
    return TURBO_OK;
}
