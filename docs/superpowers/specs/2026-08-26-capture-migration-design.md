# TurboMedia Capture Migration Design

## Decision

Migrate the capture abstraction from
`C:\projects\cpp\turbonet\turbomedia\media` into a separately installable
Salts component named `Salts::Capture`. Preserve the installed
`salts_capture.h` include path, C symbol names, enum values, callback
signatures, and device/mode semantics. Do not merge capture into
`Salts::Core`, and do not migrate playback, codec, RTSP, WebRTC, recorder,
or mobile Java/Swift wrappers.

The migration copies and adapts the capture-owned desktop and mobile native
backends plus the single miniaudio implementation translation unit. TurboMedia
continues to own its existing copy until an installed-package consumer test
proves `Salts::Capture`; switching TurboMedia and deleting its copy is a
separate downstream change so rollback remains a link-target change.

## Component and file boundary

The new `capture/` module owns:

- `include/salts_capture.h`: the compatible public C API;
- `include/salts_capture_export.h`: component-local shared-library decoration;
- `src/capture_video.c` and `src/capture_video_backend.h`: backend-neutral video
  device/mode adapter;
- desktop platform dispatchers and audio/video/screen backends under `src/`;
- Android and iOS native capture backends under `src/android/` and `src/ios/`;
- `src/miniaudio_impl.c`: exactly one `MINIAUDIO_IMPLEMENTATION` definition;
- focused TinyTest contract and platform integration tests under `tests/`.

`capture_audio_win32.c` is not part of the active TurboMedia target and is not
migrated. Windows, Linux, and macOS continue using the active miniaudio capture
path. Formatting uses the C library `snprintf`; unused `stb_sprintf.h` includes
are removed, so capture does not acquire an unnecessary stb dependency.

## Build and package contract

`SALTS_ENABLE_CAPTURE` controls the component and defaults to `OFF`. This keeps
existing Salts configure/build/package behavior unchanged for users who
do not request media-device support. When enabled, CMake creates the shared
target `salts_capture`, build-tree alias and installed export
`Salts::Capture`, and installs both public headers.

The vcpkg manifest feature `capture` owns `miniaudio` and `libyuv`. Dedicated
Windows and Linux capture user presets enable both `SALTS_ENABLE_CAPTURE` and
`VCPKG_MANIFEST_FEATURES=capture` in isolated build trees. Android public
profiles enable the same pair because Android capture is the platform-native
implementation of this component. Linux additionally requires PipeWire 0.3
and X11/Xext through pkg-config/CMake and fails during configure when either is
missing. Windows links Media Foundation, DirectShow, WASAPI-supporting system
libraries, D3D11, and DXGI. macOS/iOS links the existing Apple frameworks.

`Salts::Capture` links `Salts::Core` privately for bounded diagnostic
logging. Private third-party types do not appear in `salts_capture.h`.
Installed-package verification conditionally compiles and links a real
consumer against `Salts::Capture` when the component was built.

## Public API compatibility

The migration preserves all `salts_*capture*`, `salts_video_device_*`, and
`salts_video_mode_*` declarations. `SALTS_MEDIA_API` is replaced only as a
declaration decorator by `SALTS_CAPTURE_API`; this does not rename symbols or
change C layouts. `salts_capture_t` remains source-compatible for the first
migration so existing consumers can re-link before a later opaque-handle
redesign is considered.

Errors remain in the `salts_capture_result_t` domain. Invalid arguments fail
fast with the existing capture error values. Device absence, permissions, and
unsupported controls are not silently converted into a different backend.
Native mode IDs remain backend-local and valid only for the open device handle
and connection that produced them.

## Frame and callback protocol

| Item | Contract |
|---|---|
| Data unit | One PCM audio block or one complete video/screen frame. MJPEG contains one complete JPEG image. |
| Fact source | The active native capture backend and its current native or conversion buffer. |
| Ownership | The capture instance owns backend handles and conversion buffers. The callback receives a borrowed, read-only byte view. |
| Lifetime | Callback bytes are valid only until that callback returns. A consumer retaining data must copy it before return. The view must not cross callback return, stop, destroy, or a coroutine suspension. |
| Topology | One native producer path per capture instance and one synchronous user callback. The library does not add a queue or fan-out. |
| Ordering | Callback order is the order delivered by the selected native stream. No cross-stream total order is promised. |
| Capacity | Native buffers and existing bounded conversion buffers define capacity. Size calculations must reject overflow before allocation. No unbounded frame accumulation is added. |
| Backpressure | The callback executes synchronously on the backend callback/thread. Slow consumers may cause native frame loss or increased latency; the library neither queues indefinitely nor silently allocates a fallback queue. |
| Failure | Allocation, format negotiation, permission, or device failures return the matching capture error or `NULL`. A failed start enters `ERROR`; it does not report `RUNNING`. |
| Shutdown | `stop` first prevents new reads, cancels/flushes the backend, and drains in-flight callbacks before `destroy` releases buffers and native handles. No callback may observe a freed capture. |
| Observability | Errors are logged once where a platform error becomes a capture failure. Per-frame paths emit no INFO/WARN/ERROR logs. |

Lifecycle functions and callback setters are control-plane operations and
require external serialization for a given capture instance. A callback may
call neither `stop` nor `destroy` on its own capture unless a backend explicitly
documents reentrant shutdown. `salts_capture_get_state` is an observation of
the serialized control-plane state, not a synchronization primitive.

## Logging

Remove direct `fprintf(stderr, ...)` and Android-local logging macros from the
migrated component. Platform failures are translated once at the public
operation boundary using Salts `tlog`, with operation, device identifier
or mode summary, native error code, and failure phase. Frame callbacks and
native polling loops do not log per event. Shutdown timeout diagnostics are
bounded to one message per stop operation.

## Testing and validation

Deterministic TinyTest coverage owns the portable contract: frame-rate
rounding, standard-rate filtering, invalid arguments, exact native-mode
identity validation, lifecycle state transitions through a test backend, and
borrowed callback lifetime documentation through a synchronous test probe.
Hardware integration tests enumerate available devices without requiring that
a camera or microphone exists. Tests requiring an actual capture device are
explicitly hardware-labelled and are not CI success gates.

Validation order is:

1. observe the new capture contract test fail before production files exist;
2. build and run the focused contract test;
3. build and run Windows capture enumeration/lifecycle integration tests;
4. run adjacent Salts regressions;
5. install to the package-smoke prefix and compile the installed consumer;
6. configure/build native Linux, Android, macOS, and iOS profiles on their
   corresponding runners before claiming those backends verified.

## Compatibility risks and rollback

- **MED — dependency surface (`事实`)**: enabling capture adds miniaudio,
  libyuv, and platform SDK dependencies. The default-off option and manifest
  feature isolate this from existing builds.
- **MED — callback timing (`推论`)**: consumers that block in callbacks can lose
  frames because the library deliberately adds no queue. This matches the
  source behavior and is made explicit rather than changed during migration.
- **MED — platform verification (`事实`)**: this workstation can validate the
  Windows backend but cannot prove Linux/macOS/iOS runtime behavior. Each
  backend requires its native runner and, for device tests, hardware access.
- **LOW — export decoration (`事实`)**: the API macro name changes from
  `SALTS_MEDIA_API` to `SALTS_CAPTURE_API`; C names and layouts remain stable.

Rollback disables `SALTS_ENABLE_CAPTURE` or removes the optional
`add_subdirectory(capture)` entry. TurboMedia remains on its original component
until its separate consumer migration is validated, so rollback does not
require restoring deleted source files.
