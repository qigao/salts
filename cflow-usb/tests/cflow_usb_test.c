#include <cflow/usb.h>
#include <turbo/error_codes.h>
#include <tinytest.h>

#include <stdlib.h>

spec("CFlow USB") {
    it("declares an opaque enumeration context") {
        cflow_usb_context context = {0};
        cflow_usb_context_config config = {
            .device_capacity = 16u,
            .transfer_capacity = 4u,
            .control_payload_capacity = 64u,
            .hotplug_capacity = 4u,
            .event_poll_timeout_ms = 10u,
        };
        cflow_usb_device_info info = {0};
        cflow_usb_device device = {0};
        cflow_usb_transfer_request request = {0};
        cflow_usb_stats stats = {0};

        check_null(context.impl);
        check_equal(config.device_capacity, (size_t)16u);
        check_equal(info.vendor_id, (uint16_t)0u);
        check_null(device.impl);
        check_equal(request.kind, CFLOW_USB_TRANSFER_CONTROL);
        check_equal(stats.enumerations, (size_t)0u);
        check_true(_Generic(&cflow_usb_context_init,
            int (*)(cflow_usb_context *, const cflow_usb_context_config *): 1,
            default: 0));
        check_true(_Generic(&cflow_usb_enumerate,
            int (*)(cflow_usb_context *, cflow_usb_device_info *,
                    size_t, size_t *): 1,
            default: 0));
        check_true(_Generic(&cflow_usb_get_stats,
            bool (*)(const cflow_usb_context *, cflow_usb_stats *): 1,
            default: 0));
        check_true(_Generic(&cflow_usb_context_destroy,
            int (*)(cflow_usb_context *): 1,
            default: 0));
        check_true(_Generic(&cflow_usb_device_open,
            int (*)(cflow_usb_context *, const cflow_usb_device_identity *,
                    cflow_usb_device *): 1,
            default: 0));
        check_true(_Generic(&cflow_usb_submit,
            int (*)(cflow_usb_context *, cflow_usb_device *,
                    const cflow_usb_transfer_request *,
                    cflow_usb_transfer_id *): 1,
            default: 0));
        check_true(_Generic(&cflow_usb_run_ready,
            int (*)(cflow_usb_context *, size_t, size_t *): 1,
            default: 0));
    }

    it("initializes enumerates within a hard bound and restores zero state") {
        cflow_usb_context context = {0};
        const size_t device_capacity = 4096u;
        cflow_usb_context_config config = {
            .device_capacity = device_capacity,
            .transfer_capacity = 8u,
            .control_payload_capacity = 4096u,
            .hotplug_capacity = 8u,
            .event_poll_timeout_ms = 10u,
        };
        cflow_usb_device_info *devices = NULL;
        cflow_usb_stats stats = {0};
        size_t required = 0u;
        size_t actual = 0u;
        int status;

        check_equal(cflow_usb_context_init(NULL, &config), TURBO_EINVAL);
        check_equal(cflow_usb_context_init(&context, &config), TURBO_OK);
        status = cflow_usb_enumerate(&context, NULL, 0u, &required);
        if (required == 0u) {
            check_equal(status, TURBO_OK);
        } else {
            check_equal(status, TURBO_ENOBUFS);
            devices = (cflow_usb_device_info *)calloc(required,
                                                       sizeof(*devices));
            check_not_null(devices);
            check_equal(cflow_usb_enumerate(
                            &context, devices, required, &actual), TURBO_OK);
            check_equal(actual, required);
        }
        check_true(cflow_usb_get_stats(&context, &stats));
        check_equal(stats.device_capacity, device_capacity);
        check_true(stats.enumerations >= (size_t)1u);
        check_equal(cflow_usb_context_destroy(&context), TURBO_OK);
        check_null(context.impl);
        free(devices);
    }

    it("fails fast for missing identities and invalid transfer state") {
        cflow_usb_context context = {0};
        cflow_usb_context_config config = {
            .device_capacity = 4096u,
            .transfer_capacity = 2u,
            .control_payload_capacity = 64u,
            .hotplug_capacity = 2u,
            .event_poll_timeout_ms = 10u,
        };
        cflow_usb_device_identity missing = {
            .device_address = 0u,
        };
        cflow_usb_device device = {0};
        cflow_usb_transfer_request request = {
            .kind = CFLOW_USB_TRANSFER_BULK,
            .endpoint = 0x81u,
            .length = 1u,
        };
        cflow_usb_transfer_id id = CFLOW_USB_INVALID_TRANSFER_ID;
        size_t delivered = 99u;

        check_equal(cflow_usb_context_init(&context, &config), TURBO_OK);
        check_equal(cflow_usb_device_open(&context, &missing, &device),
                    TURBO_ENODEV);
        check_null(device.impl);
        check_equal(cflow_usb_submit(&context, &device, &request, &id),
                    TURBO_EINVAL);
        check_equal(id, CFLOW_USB_INVALID_TRANSFER_ID);
        check_equal(cflow_usb_cancel(&context,
                                     CFLOW_USB_INVALID_TRANSFER_ID),
                    TURBO_EINVAL);
        check_equal(cflow_usb_run_ready(&context, 1u, &delivered), TURBO_OK);
        check_equal(delivered, (size_t)0u);
        check_equal(cflow_usb_context_destroy(&context), TURBO_OK);
    }
}
