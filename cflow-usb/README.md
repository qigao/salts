# CFlowUSB

`TurboUtils::CFlowUSB` is a default-off shared adapter around libusb 1.0. Enable
it explicitly with both the vcpkg manifest feature and the CMake option:

```powershell
cmake --preset win-release-user -B build/Msvc-Release-usb `
  -DVCPKG_MANIFEST_FEATURES=usb -DCFLOW_ENABLE_USB=ON
```

The default package neither exports `TurboUtils::CFlowUSB` nor links libusb.
The shared target keeps libusb types and link details out of core CFlow's public
contract.

The context owns one libusb event thread, a fixed transfer-slot array, fixed
control-transfer storage, and a bounded hotplug queue. User callbacks never run
on the libusb thread: one caller drives them through `cflow_usb_run_ready()`.
Each accepted transfer reaches exactly one terminal callback. Bulk and
interrupt buffers are borrowed directly; control payloads use bounded internal
storage and successful IN data is copied back before callback delivery.

Enumeration size queries use `out == NULL` and `out_capacity == 0`.
`TURBO_ENOBUFS` returns the required count without committing a partial
snapshot. Bus address is observed identity, not a stable reconnect key, so an
unplug/replug requires enumeration and an explicit new open. A lost hotplug
event also marks matching handles lost and requests cancellation of their live
transfers.

Detailed hotplug overflow is explicit: one allocation-free
`CFLOW_USB_HOTPLUG_RESCAN_REQUIRED` marker is delivered, subsequent detailed
events are suppressed, and delivery resumes only after
`cflow_usb_acknowledge_hotplug_rescan()`.
If another hotplug change is suppressed after marker delivery while the caller
re-enumerates, acknowledgement queues another marker instead of losing that
race; repeat enumeration and acknowledgement until the marker queue stays empty.

Minimal lifecycle:

```c
#include <cflow/usb.h>
#include <turbo/error_codes.h>

#include <stdlib.h>

int main(void) {
cflow_usb_context context = {0};
cflow_usb_context_config config = {
    .device_capacity = 64,
    .transfer_capacity = 32,
    .control_payload_capacity = 4096,
    .hotplug_capacity = 64,
    .event_poll_timeout_ms = 10,
};
cflow_usb_device_info *devices = NULL;
size_t required = 0;
int status;
int result = 1;

if (cflow_usb_context_init(&context, &config) != TURBO_OK)
    return 1;
status = cflow_usb_enumerate(&context, NULL, 0, &required);
if ((required == 0 && status != TURBO_OK) ||
    (required != 0 && status != TURBO_ENOBUFS))
    goto cleanup;
if (required != 0) {
    size_t actual = 0;
    devices = calloc(required, sizeof(*devices));
    if (devices == NULL ||
        cflow_usb_enumerate(&context, devices, required, &actual) != TURBO_OK ||
        actual != required)
        goto cleanup;
}
/* Select an identity, then explicitly open and claim its required interface
 * before submitting endpoint transfers. */
result = 0;
cleanup:
free(devices);
if (cflow_usb_context_destroy(&context) != TURBO_OK)
    return 1;
return result;
}
```

Control-plane operations are externally serialized. Before destroy, cancel and
drive all transfers to terminal callbacks, release every interface, close every
device, and drain hotplug events. Isochronous transfers and automatic kernel
driver detach remain outside this API because their packet/permission contracts
need a separate surface.
