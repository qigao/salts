#include <cflow/usb.h>
#include <libusb.h>
#include <tinytest.h>
#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <string.h>

typedef struct transfer_probe {
    size_t calls;
    cflow_usb_transfer_result last;
} transfer_probe;

typedef struct hotplug_probe {
    size_t calls;
    cflow_usb_hotplug_kind kinds[4];
} hotplug_probe;

static void capture_transfer(void *user,
                             const cflow_usb_transfer_result *result) {
    transfer_probe *probe = (transfer_probe *)user;
    ++probe->calls;
    probe->last = *result;
}

static void capture_hotplug(void *user,
                            const cflow_usb_hotplug_event *event) {
    hotplug_probe *probe = (hotplug_probe *)user;
    if (probe->calls < sizeof(probe->kinds) / sizeof(probe->kinds[0]))
        probe->kinds[probe->calls] = event->kind;
    ++probe->calls;
}

static cflow_usb_context_config test_config(hotplug_probe *probe) {
    cflow_usb_context_config config = {
        .device_capacity = 4u,
        .transfer_capacity = 1u,
        .control_payload_capacity = 64u,
        .hotplug_capacity = 1u,
        .event_poll_timeout_ms = 1u,
    };
    if (probe != NULL) {
        config.hotplug = capture_hotplug;
        config.hotplug_user = probe;
    }
    return config;
}

static void drive_until(cflow_usb_context *context, size_t expected,
                        size_t *total) {
    size_t attempt;
    *total = 0u;
    for (attempt = 0u; attempt < 1000u && *total < expected; ++attempt) {
        size_t delivered = 0u;
        check_equal(cflow_usb_run_ready(context, expected, &delivered),
                    TURBO_OK);
        *total += delivered;
        if (*total < expected)
            turbo_sleep_ms(1u);
    }
}

spec("CFlow USB bounded state machine") {
    it("opens claims transfers cancels and closes exactly") {
        cflow_usb_context context = {0};
        cflow_usb_device device = {0};
        cflow_usb_context_config config = test_config(NULL);
        cflow_usb_device_info info = {0};
        cflow_usb_device_identity identity;
        cflow_usb_transfer_request request;
        cflow_usb_transfer_id id = CFLOW_USB_INVALID_TRANSFER_ID;
        cflow_usb_transfer_id first_id;
        transfer_probe probe = {0};
        unsigned char payload[4] = {1u, 2u, 3u, 4u};
        size_t count = 0u;
        size_t delivered;

        check_equal(cflow_usb_context_init(&context, &config), TURBO_OK);
        check_equal(cflow_usb_enumerate(&context, &info, 1u, &count),
                    TURBO_OK);
        check_equal(count, (size_t)1u);
        identity = (cflow_usb_device_identity){
            .bus_number = info.bus_number,
            .device_address = info.device_address,
            .vendor_id = info.vendor_id,
            .product_id = info.product_id,
        };
        check_equal(cflow_usb_device_open(&context, &identity, &device),
                    TURBO_OK);
        check_equal(cflow_usb_device_set_configuration(&device, 1), TURBO_OK);
        check_equal(cflow_usb_device_claim_interface(&device, 1u), TURBO_OK);
        check_equal(cflow_usb_device_claim_interface(&device, 1u),
                    TURBO_EALREADY);

        request = (cflow_usb_transfer_request){
            .kind = CFLOW_USB_TRANSFER_BULK,
            .endpoint = 0x01u,
            .buffer = payload,
            .length = sizeof(payload),
            .timeout_ms = 100u,
            .complete = capture_transfer,
            .complete_user = &probe,
        };
        fake_libusb_allow_completion(0);
        check_equal(cflow_usb_submit(&context, &device, &request, &id),
                    TURBO_OK);
        check(id != CFLOW_USB_INVALID_TRANSFER_ID);
        first_id = id;
        check_equal(cflow_usb_submit(&context, &device, &request, &id),
                    TURBO_ENOBUFS);
        check_equal(cflow_usb_cancel(&context, id), TURBO_EINVAL);

        id = CFLOW_USB_INVALID_TRANSFER_ID;
        check_equal(cflow_usb_submit(&context, &device, &request, &id),
                    TURBO_ENOBUFS);
        check_equal(cflow_usb_cancel(&context, first_id), TURBO_OK);
        drive_until(&context, 1u, &delivered);
        check_equal(delivered, (size_t)1u);
        check_equal(probe.calls, (size_t)1u);
        check_equal(probe.last.status, TURBO_ECANCELED);
        check_equal(cflow_usb_device_release_interface(&device, 1u),
                    TURBO_OK);
        check_equal(cflow_usb_device_close(&device), TURBO_OK);
        check_equal(cflow_usb_context_destroy(&context), TURBO_OK);
    }

    it("copies control IN data before terminal delivery") {
        cflow_usb_context context = {0};
        cflow_usb_device device = {0};
        cflow_usb_context_config config = test_config(NULL);
        cflow_usb_device_identity identity = {
            .bus_number = 1u,
            .device_address = 2u,
            .vendor_id = 0x1234u,
            .product_id = 0x5678u,
        };
        transfer_probe probe = {0};
        unsigned char payload[8] = {0};
        cflow_usb_transfer_request request = {
            .kind = CFLOW_USB_TRANSFER_CONTROL,
            .control = {
                .request_type = 0x80u,
                .request = 1u,
            },
            .buffer = payload,
            .length = sizeof(payload),
            .complete = capture_transfer,
            .complete_user = &probe,
        };
        cflow_usb_transfer_id id;
        size_t delivered;

        check_equal(cflow_usb_context_init(&context, &config), TURBO_OK);
        check_equal(cflow_usb_device_open(&context, &identity, &device),
                    TURBO_OK);
        fake_libusb_allow_completion(0);
        check_equal(cflow_usb_submit(&context, &device, &request, &id),
                    TURBO_OK);
        fake_libusb_allow_completion(1);
        drive_until(&context, 1u, &delivered);
        check_equal(probe.last.status, TURBO_OK);
        check_equal(probe.last.bytes_transferred, sizeof(payload));
        check_equal(payload[0], (unsigned char)0xa5u);
        check_equal(payload[sizeof(payload) - 1u], (unsigned char)0xa5u);
        check_equal(cflow_usb_device_close(&device), TURBO_OK);
        check_equal(cflow_usb_context_destroy(&context), TURBO_OK);
    }

    it("turns hotplug queue loss into one acknowledged rescan marker") {
        cflow_usb_context context = {0};
        hotplug_probe probe = {0};
        cflow_usb_context_config config = test_config(&probe);
        cflow_usb_stats stats = {0};
        size_t delivered;

        check_equal(cflow_usb_context_init(&context, &config), TURBO_OK);
        fake_libusb_emit_hotplug(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED);
        fake_libusb_emit_hotplug(LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT);
        drive_until(&context, 2u, &delivered);
        check_equal(delivered, (size_t)2u);
        check_equal(probe.calls, (size_t)2u);
        check_equal(probe.kinds[0], CFLOW_USB_HOTPLUG_ARRIVED);
        check_equal(probe.kinds[1], CFLOW_USB_HOTPLUG_RESCAN_REQUIRED);
        check_true(cflow_usb_get_stats(&context, &stats));
        check_equal(stats.hotplug_suppressed, (size_t)1u);
        check_equal(stats.hotplug_rescan_required, (size_t)1u);
        fake_libusb_emit_hotplug(LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED);
        check_true(cflow_usb_get_stats(&context, &stats));
        check_equal(stats.hotplug_suppressed, (size_t)2u);
        check_equal(cflow_usb_acknowledge_hotplug_rescan(&context), TURBO_OK);
        check_true(cflow_usb_get_stats(&context, &stats));
        check_equal(stats.hotplug_queued, (size_t)1u);
        check_equal(stats.hotplug_rescan_required, (size_t)2u);
        drive_until(&context, 1u, &delivered);
        check_equal(delivered, (size_t)1u);
        check_equal(probe.calls, (size_t)3u);
        check_equal(probe.kinds[2], CFLOW_USB_HOTPLUG_RESCAN_REQUIRED);
        check_equal(cflow_usb_acknowledge_hotplug_rescan(&context), TURBO_OK);
        check_equal(cflow_usb_acknowledge_hotplug_rescan(&context),
                    TURBO_EALREADY);
        check_equal(cflow_usb_context_destroy(&context), TURBO_OK);
    }
}
