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

The first delivered control-plane API owns a libusb context and produces a
bounded caller-owned enumeration snapshot. A size query uses `out == NULL` and
`out_capacity == 0`; `TURBO_ENOBUFS` returns the required count without a
partial committed snapshot. Bus address is an observed identity, not a stable
reconnect key, so callers must re-enumerate after unplug/replug.

Asynchronous endpoint transfers, interface ownership, hotplug delivery, and
disconnect settlement remain tracked by issue #114 and are not exposed by the
current narrow API.
