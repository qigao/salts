# CFlow Optional USB Adapter Design

Issue: [#114](https://github.com/qigao/turbo-utils/issues/114)
Parent: [#111](https://github.com/qigao/turbo-utils/issues/111)

## Decision

Add a separate optional `TurboUtils::CFlowUSB` target backed by libusb 1.0.
`CFLOW_ENABLE_USB` defaults to `OFF`; vcpkg feature `usb` adds the `libusb`
port. The target is discovered through `pkg-config` as `libusb-1.0` because the
current vcpkg port installs the canonical `.pc` metadata and no CMake config.
Core CFlow and default package consumers gain no libusb link dependency.

## Capability boundary

The adapter exposes enumeration snapshots, open/close by explicit device
identity, interface claim/release, asynchronous control, bulk, and interrupt
transfers, and hotplug events. Isochronous transfer support is capability-gated
and remains outside the first public surface because packet ownership and
per-packet status require a separate contract.

USB is not modeled as file or generic device reads. Endpoint direction,
transfer type, interface claim, configuration, timeout, stall, disconnect, and
short-transfer semantics remain explicit.

## Ownership and concurrency

One context owns the libusb context, event thread, registered hotplug callback,
opaque device handles, fixed transfer slots, and bounded completion/event
queues. Caller buffers are borrowed only after accepted submission and through
terminal callback return. libusb callbacks publish terminal facts; user
callbacks run on the single driver thread.

Unplug transitions matching handles to lost, requests cancellation for live
transfers, and completes each accepted transfer exactly once from libusb's
terminal callback. Handles never reconnect silently. Event queue loss emits a
rescan-required hotplug event.
If a change is suppressed after marker delivery while the caller re-enumerates,
acknowledgement retains the loss state and schedules another marker. Recovery
repeats until a clean generation is acknowledged.

## Build and validation

Default configure/build/install tests assert the USB target is absent and the
core export has no libusb dependency. USB-enabled configure fails fast when the
manifest feature or package is absent. Hardware-independent tests use a thin
internal libusb adapter to verify enumeration cleanup, transfer validation,
cancellation, disconnect, hotplug capability rejection, event-thread shutdown,
and exact terminal accounting. Physical-device tests are opt-in and never
required by default CI.
