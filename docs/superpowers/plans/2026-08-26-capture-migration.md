# TurboMedia Capture Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the TurboMedia audio/video/screen capture abstraction to
TurboUtils as the optional, installed `TurboUtils::Capture` component without
changing its public C behavior.

**Architecture:** Keep capture in an independent shared library with a stable C
header and backend-neutral video device adapter. Select exactly one native
backend set at configure time, keep frame delivery synchronous and borrowed,
and link media/platform dependencies privately.

**Tech Stack:** C11, Objective-C on Apple platforms, CMake Presets, vcpkg
manifest features, TinyTest, miniaudio, libyuv, Media Foundation/DXGI, V4L2,
PipeWire/X11, AVFoundation/CoreGraphics, Android NDK camera/media APIs.

**Spec:** `docs/superpowers/specs/2026-08-26-capture-migration-design.md`

## Execution status (2026-08-26)

The component, all listed native source sets, package export, presets,
documentation, and deterministic tests are implemented. Windows Release was
fresh-configured and built, all 148 registered tests passed, and the installed
consumer compiled and linked `TurboUtils::Capture`. Android arm64 Debug also
compiled the `turbo_capture` target.

Native Linux, macOS, and iOS configure/link/runtime validation remains assigned
to their corresponding runners; WSL and Apple SDKs are not available on this
Windows workstation. Other Android ABIs also remain runner work. These are
validation gaps rather than fallback paths: their CMake branches and sources
are present, but this document does not infer native success from source review.

## Global Constraints

- Preserve `turbo_capture.h`, all public C symbol names, enum numeric values,
  struct layouts, callback signatures, and capture error semantics.
- Keep `TURBO_ENABLE_CAPTURE=OFF` by default; existing presets and exported
  targets remain unchanged unless a capture profile is selected.
- Do not migrate playback, codecs, RTSP, WebRTC, recorder, Java, or Swift code.
- A frame/audio callback receives a borrowed view valid only until return; no
  queue, implicit retention, or unbounded allocation may be added.
- `stop` drains native/in-flight callbacks before `destroy` releases storage.
- Use TurboUtils tlog only at error-consumption boundaries and never per frame.
- Run the smallest focused test before adjacent regressions and package smoke.

---

### Task 1: Establish the optional component boundary with a failing consumer

**Files:**
- Modify: `CMakeOptions.cmake`
- Modify: `CMakeLists.txt`
- Create: `capture/CMakeLists.txt`
- Create: `capture/tests/CMakeLists.txt`
- Create: `capture/tests/test_capture_contract.c`

**Interfaces:**
- Consumes: `cmake_config_target()` and `cmake_add_test()` from
  `cmake/CmakeUtils.cmake`.
- Produces: option `TURBO_ENABLE_CAPTURE`, target `turbo_capture`, and alias
  `TurboUtils::Capture`.

- [ ] **Step 1: Add the contract test before the public header exists**

  Create a TinyTest file that includes `<turbo_capture.h>` and asserts these
  hand-derived results:

  ```c
  #include <turbo_capture.h>
  #include <tinytest.h>

  suite("capture portable contract") {
    it("rounds rational frame rates to the nearest integer") {
      turbo_video_native_mode_t ntsc = {1920, 1080, 30000u, 1001u,
                                        TURBO_VIDEO_CAPTURE_FORMAT_NV12, 1u};
      check_equal(turbo_video_mode_fps(&ntsc), 30);
    }

    it("rejects invalid device adapter arguments") {
      turbo_video_device_t *device = NULL;
      check_equal(turbo_video_device_open(NULL, NULL),
                  TURBO_CAPTURE_ERR_FORMAT);
      check_equal(turbo_video_device_create_capture(NULL, NULL, NULL),
                  TURBO_CAPTURE_ERR_FORMAT);
      check_null(device);
    }
  }
  ```

- [ ] **Step 2: Register the test against the wished-for target**

  In `capture/tests/CMakeLists.txt`, call:

  ```cmake
  cmake_add_test(
    capture_contract_test
    SOURCES test_capture_contract.c
    LIBS TurboUtils::Capture TurboUtils::TinyTest
    FOLDER "capture/tests")
  ```

  Add `TURBO_ENABLE_CAPTURE` to `CMakeOptions.cmake`, conditionally add the
  `capture/` directory from the root, and create only the target declaration in
  `capture/CMakeLists.txt`; do not add production sources yet.

- [ ] **Step 3: Run RED and record the expected cause**

  Run from `VsDevCmd.bat`:

  ```bat
  cmake --fresh --preset win-capture-dev-user
  cmake --build --preset win-capture-dev-user --target capture_contract_test
  ```

  Expected result: compilation fails because `turbo_capture.h` and its
  functions do not yet exist. A preset/dependency/toolchain failure is not an
  acceptable RED and must be repaired before continuing.

### Task 2: Migrate the stable public API and backend-neutral mode adapter

**Files:**
- Create: `capture/include/turbo_capture_export.h`
- Create: `capture/include/turbo_capture.h`
- Create: `capture/src/capture_video_backend.h`
- Create: `capture/src/capture_video.c`
- Modify: `capture/CMakeLists.txt`
- Modify: `capture/tests/test_capture_contract.c`

**Interfaces:**
- Consumes: source API from
  `C:/projects/cpp/turbonet/turbomedia/media/include/turbo_capture.h`.
- Produces: the compatible `turbo_capture_*`, `turbo_video_device_*`, and
  `turbo_video_mode_*` surface plus internal `turbo_video_backend_ops_t`.

- [ ] **Step 1: Extend RED coverage for rational and filtering boundaries**

  Add literal cases for `0/1`, `24000/1001`, `30000/1001`, `60000/1001`, and
  denominator zero. Assert standard filtering accepts rounded 24/30/60 and
  rejects 23. Include capacity-zero and null-output cases for both mode-list
  functions. Run the focused target and confirm missing functions/behavior
  fail for the expected reason.

- [ ] **Step 2: Add the public header without changing ABI data**

  Copy the source declarations and documentation, replace only
  `TURBO_MEDIA_API` with `TURBO_CAPTURE_API`, and define the component export
  macro as:

  ```c
  #if defined(_WIN32) && defined(TURBO_CAPTURE_SHARED)
  #  if defined(TURBO_CAPTURE_BUILD)
  #    define TURBO_CAPTURE_API __declspec(dllexport)
  #  else
  #    define TURBO_CAPTURE_API __declspec(dllimport)
  #  endif
  #else
  #  define TURBO_CAPTURE_API
  #endif
  ```

- [ ] **Step 3: Add the minimal backend-neutral implementation**

  Adapt `capture_video.c` and `capture_video_backend.h` from TurboMedia. Keep
  the opaque device handle, exact `mode_id` validation, rounded frame-rate
  calculation, standard-rate table, and capacity semantics unchanged. Use
  checked multiplication/addition before allocations introduced by the
  migration.

- [ ] **Step 4: Run GREEN**

  Build and run `capture_contract_test`. Confirm all portable cases pass and
  no device hardware is accessed by this test.

### Task 3: Add dependency isolation and capture-specific presets

**Files:**
- Modify: `vcpkg.json`
- Modify: `CMakeUserPresets.json`
- Modify: `capture/CMakeLists.txt`

**Interfaces:**
- Consumes: vcpkg ports `miniaudio` and `libyuv`; system `Threads`, PipeWire
  0.3, X11, and Xext on Linux.
- Produces: manifest feature `capture`; configure/build/test/install presets
  `win-capture-{dev,release}-user` and
  `linux-capture-{dev,release}-user`.

- [ ] **Step 1: Add the manifest feature and isolated profiles**

  Define:

  ```json
  "capture": {
    "description": "Build the optional native audio/video/screen capture component",
    "dependencies": ["miniaudio", "libyuv"]
  }
  ```

  Each capture configure preset sets `TURBO_ENABLE_CAPTURE=ON`,
  `VCPKG_MANIFEST_FEATURES=capture`, a distinct capture binary directory, and a
  runtime path rooted at that directory. Add matching build, test, and
  `install-...` build presets. Do not change non-capture preset semantics.

- [ ] **Step 2: Make dependency discovery fail fast only inside the component**

  In `capture/CMakeLists.txt`, locate `miniaudio.h`, `libyuv`, and platform
  packages only when the component is enabled. Linux must require
  `PkgConfig::PIPEWIRE`, `X11::X11`, and `X11::Xext`; Windows and Apple must
  link only their native libraries/frameworks. Remove `stb_sprintf.h` includes
  because migrated code uses standard `snprintf`.

- [ ] **Step 3: Validate preset discovery and configure**

  Run `cmake --list-presets`, `cmake --build --list-presets`, and
  `ctest --list-presets`, then run a fresh Windows capture configure. Expected:
  normal presets remain listed, capture presets use their isolated directories,
  and missing dependencies fail with their exact package/header name.

### Task 4: Migrate and verify the Windows desktop backend

**Files:**
- Create: `capture/src/capture_win32.c`
- Create: `capture/src/capture_audio_miniaudio.c`
- Create: `capture/src/miniaudio_impl.c`
- Create: `capture/src/capture_video_win32.c`
- Create: `capture/src/capture_screen_win32.c`
- Create: `capture/tests/test_capture_windows.c`
- Modify: `capture/CMakeLists.txt`

**Interfaces:**
- Consumes: miniaudio, Media Foundation source reader, DirectShow camera
  controls, libyuv, D3D11/DXGI duplication, and TurboUtils tlog.
- Produces: complete Windows implementations for enumeration, creation,
  callback setup, start/stop/destroy, camera controls/crop, and screen capture.

- [ ] **Step 1: Write Windows RED tests at observable boundaries**

  Test null/zero enumeration arguments, state transitions for a testable
  created device when available, unique native mode IDs, exact-mode mismatch
  rejection, and safe repeated stop/destroy. Device absence must be represented
  by zero enumerated devices, not a passing fake. Confirm the test fails before
  Windows sources are attached.

- [ ] **Step 2: Migrate the active Windows sources**

  Copy the four active source files and the miniaudio implementation unit.
  Keep Media Foundation asynchronous read/flush and callback-drain events.
  Replace the five direct stderr diagnostics with one bounded tlog error at the
  operation boundary; do not log inside successful frame delivery. Do not copy
  inactive `capture_audio_win32.c`.

- [ ] **Step 3: Verify callback ownership and shutdown**

  Ensure native or conversion bytes are borrowed only during `video_cb`, every
  conversion buffer is capture-owned, `mf_video_stop` prevents new reads and
  waits for callbacks in flight, and destroy disconnects the callback before
  releasing the reader/source. Reject overflow before allocating I420/NV12
  conversion or crop buffers.

- [ ] **Step 4: Run Windows GREEN and adjacent regressions**

  Build `capture_contract_test` and `capture_windows_test`, run both through
  the capture CTest preset, then run the existing platform/core focused tests.
  Hardware-dependent cases must report their observed device count and skip
  capture assertions when it is zero; they must not manufacture success.

### Task 5: Migrate Linux and Apple desktop backends

**Files:**
- Create: `capture/src/capture_linux.c`
- Create: `capture/src/capture_video_linux.c`
- Create: `capture/src/capture_screen_linux.c`
- Create: `capture/src/capture_macos.c`
- Create: `capture/src/capture_video_macos.m`
- Create: `capture/src/capture_screen_macos.m`
- Create: `capture/tests/test_capture_posix.c`
- Modify: `capture/CMakeLists.txt`

**Interfaces:**
- Consumes: V4L2/mmap/poll/pthread, PipeWire/X11, and Apple capture frameworks.
- Produces: the same public API and borrowed callback protocol as Windows.

- [ ] **Step 1: Add platform RED tests**

  Under native compile guards, test invalid enumeration inputs, zero-device
  behavior, unique mode IDs, start failure state, idempotent stop, and the
  absence of callbacks after stop. Run on the corresponding native runner and
  confirm the backend is missing before migration.

- [ ] **Step 2: Migrate Linux with bounded mmap ownership**

  Preserve V4L2 buffer count limits, one producer thread per capture, poll
  cancellation, `VIDIOC_STREAMOFF` shutdown, and `munmap` ownership. PipeWire
  and X11 screen paths remain explicit native choices; do not add a fallback
  when required initialization fails.

- [ ] **Step 3: Migrate macOS with Objective-C/framework selection**

  Enable Objective-C only on Apple builds, retain/release native capture
  objects according to the existing ARC mode, preserve AVFoundation format
  selection, and ensure the dispatch/session callback is quiescent before
  capture storage is released. Avoid duplicate common lifecycle symbols from
  the miniaudio unit by keeping dispatch exclusively in `capture_macos.c`.

- [ ] **Step 4: Run native GREEN**

  Use the Linux capture dev/release presets and the repository's Apple CI entry
  to compile and run the contract plus POSIX tests. Record unavailable hardware
  separately from compile, link, or lifecycle failures.

### Task 6: Migrate Android and iOS native capture backends

**Files:**
- Create: `capture/src/android/capture_android.c`
- Create: `capture/src/android/capture_audio_android.c`
- Create: `capture/src/android/capture_video_android.c`
- Create: `capture/src/android/capture_screen_android.c`
- Create: `capture/src/ios/capture_ios.m`
- Create: `capture/src/ios/capture_audio_ios.m`
- Create: `capture/src/ios/capture_video_ios.m`
- Create: `capture/src/ios/capture_video_ios_backend.h`
- Create: `capture/src/ios/capture_screen_ios.m`
- Modify: `capture/CMakeLists.txt`
- Modify: `CMakeUserPresets.json`

**Interfaces:**
- Consumes: Android NDK camera/media/OpenSL ES/native-window/JNI libraries and
  iOS AVFoundation/ReplayKit/CoreVideo frameworks.
- Produces: platform-native implementations of the unchanged capture API.

- [ ] **Step 1: Enable capture in Android profiles and run RED configure**

  Add `TURBO_ENABLE_CAPTURE=ON` and manifest feature `capture` to Android
  profiles. Fresh-configure `android-arm64-v8a-debug-win`; expected RED is
  missing Android capture sources or symbols, not a host-header fallback.

- [ ] **Step 2: Migrate Android sources and logging**

  Copy only native C capture files. Replace local `LOGI/LOGE` macros with
  bounded TurboUtils logging at create/start/stop failure boundaries. Preserve
  camera/session/image-reader ownership, pthread synchronization, JNI handle
  validation, and libyuv conversion bounds. Java MediaProjection handoff is
  outside this component and unsupported screen setup must fail explicitly.

- [ ] **Step 3: Migrate iOS sources**

  Copy only Objective-C capture files and the backend header. Preserve camera,
  microphone, and ReplayKit buffer borrowing; do not copy Swift wrappers or
  mobile optimizer code. Add framework links only under the iOS condition.

- [ ] **Step 4: Run platform builds**

  Build all configured Android ABI capture targets, then compile and run native
  iOS tests on an Apple runner. Do not claim iOS runtime verification from a
  Windows source-only check.

### Task 7: Export, install, document, and verify the package

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `cmake/VerifyInstalledPackage.cmake`
- Modify: `tests/install_consumer/CMakeLists.txt`
- Modify: `tests/install_consumer/consumer.c`
- Create: `capture/README.md`
- Modify: `README.md`

**Interfaces:**
- Consumes: configured `turbo_capture` target and public header.
- Produces: installed target `TurboUtils::Capture`, installed headers, package
  smoke consumer, and migration instructions for TurboMedia.

- [ ] **Step 1: Write the installed-consumer RED path**

  Add `TURBOUTILS_EXPECT_CAPTURE`. When true, require
  `TurboUtils::Capture`, compile a consumer that includes
  `<turbo_capture.h>`, constructs a literal `30000/1001` mode, and returns
  success only when `turbo_video_mode_fps()` returns `30`. Run
  `verify_installed_package` and confirm it fails before export/install wiring.

- [ ] **Step 2: Export and install the component**

  Install the shared library in `TurboUtilsTargets`, install both headers, set
  export name `Capture`, and pass the capture expectation into package smoke.
  Because dependencies are private to the shared library, do not expose
  miniaudio/libyuv types or add unrelated global dependency discovery to
  `TurboUtilsConfig.cmake.in`.

- [ ] **Step 3: Document usage and downstream migration**

  Document enablement, platform dependencies, callback borrowing, external
  control-plane serialization, shutdown ordering, and:

  ```cmake
  find_package(TurboUtils CONFIG REQUIRED)
  target_link_libraries(app PRIVATE TurboUtils::Capture)
  ```

  State that TurboMedia should switch its `TurboMedia::Device` callers to the
  installed target in a separate commit, verify them, and only then delete its
  duplicate capture sources.

- [ ] **Step 4: Run the final verification matrix**

  On Windows, fresh-configure the capture dev profile; build and run focused
  capture tests; run adjacent platform/core regressions; build
  `verify_installed_package`; then repeat Release configure/build/test/install.
  Run `git diff --check`, inspect `git status --short`, and report native
  platforms not executed as residual risk rather than inferred success.
