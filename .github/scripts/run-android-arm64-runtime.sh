#!/usr/bin/env bash
set -euo pipefail

: "$GITHUB_WORKSPACE"
: "$RUNNER_TEMP"
: "$ANDROID_HOME"
: "$SALTS_ANDROID_NDK_REVISION"

sdk_prefix="$GITHUB_WORKSPACE/stage/sdk/android-arm64-v8a"
results="$GITHUB_WORKSPACE/android-runtime-results"
consumer_src="$RUNNER_TEMP/salts-android-runtime-consumer"
consumer_build="$RUNNER_TEMP/salts-android-runtime-build"
if [[ -n "$ANDROID_NDK_HOME" ]]; then
  ndk_root="$ANDROID_NDK_HOME"
else
  ndk_root="$ANDROID_HOME/ndk/$SALTS_ANDROID_NDK_REVISION"
fi

test -f "$sdk_prefix/lib/cmake/Salts/SaltsConfig.cmake"
test -f "$GITHUB_WORKSPACE/cflow/tests/android_runtime_probe.c"
test -f "$ndk_root/build/cmake/android.toolchain.cmake"

rm -rf "$results" "$consumer_src" "$consumer_build"
mkdir -p "$results" "$consumer_src"

cat > "$consumer_src/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.25)
project(SaltsAndroidRuntimeConsumer C)

find_package(Salts CONFIG REQUIRED)

add_executable(salts_android_runtime_probe
  "$GITHUB_WORKSPACE/cflow/tests/android_runtime_probe.c")

target_link_libraries(salts_android_runtime_probe PRIVATE
  Salts::Platform
  Salts::Concurrency
  Salts::CMeta
  Salts::CFlow)

set_target_properties(salts_android_runtime_probe PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
EOF

cmake -S "$consumer_src" -B "$consumer_build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$ndk_root/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_shared \
  -DSalts_DIR="$sdk_prefix/lib/cmake/Salts"

cmake --build "$consumer_build" --parallel 2

probe="$consumer_build/salts_android_runtime_probe"
test -f "$probe"

abi="$(adb shell getprop ro.product.cpu.abi | tr -d '\r')"
api="$(adb shell getprop ro.build.version.sdk | tr -d '\r')"
model="$(adb shell getprop ro.product.model | tr -d '\r')"
fingerprint="$(adb shell getprop ro.build.fingerprint | tr -d '\r')"
qemu="$(adb shell getprop ro.kernel.qemu | tr -d '\r')"
uname_text="$(adb shell uname -a | tr -d '\r')"
machine="$(adb shell uname -m | tr -d '\r')"

if [[ "$abi" != "arm64-v8a" ]]; then
  echo "expected Android arm64-v8a runtime, got ABI=$abi" >&2
  exit 1
fi
if [[ "$api" != "30" ]]; then
  echo "expected API 30 emulator, got API=$api" >&2
  exit 1
fi
if [[ "$machine" != "aarch64" && "$machine" != "arm64" ]]; then
  echo "expected arm64 kernel machine, got $machine" >&2
  exit 1
fi

remote="/data/local/tmp/salts-android-runtime"
adb shell "rm -rf '$remote' && mkdir -p '$remote'"
adb push "$probe" "$remote/salts_android_runtime_probe" >/dev/null

while IFS= read -r library; do
  adb push "$library" "$remote/$(basename "$library")" >/dev/null
done < <(find "$sdk_prefix" -type f -name '*.so' | sort)

libcxx="$(
  find "$ndk_root/toolchains/llvm/prebuilt" \
    -type f -path '*/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so' \
    -print -quit
)"
if [[ -n "$libcxx" ]]; then
  adb push "$libcxx" "$remote/libc++_shared.so" >/dev/null
fi

adb shell "chmod 755 '$remote/salts_android_runtime_probe'"

runtime_command="cd $remote && LD_LIBRARY_PATH=$remote ./salts_android_runtime_probe"
set +e
adb shell "$runtime_command" 2>&1 | tr -d '\r' | tee "$results/runtime-output.txt"
runtime_status=$PIPESTATUS
set -e
if [[ "$runtime_status" -ne 0 ]]; then
  echo "Android runtime probe failed with status $runtime_status" >&2
  exit "$runtime_status"
fi

json_count="$(
  grep -c '^SALTS_ANDROID_RUNTIME_JSON ' "$results/runtime-output.txt" || true
)"
if [[ "$json_count" -ne 1 ]]; then
  echo "expected exactly one SALTS_ANDROID_RUNTIME_JSON record, got $json_count" >&2
  exit 1
fi

probe_json="$(
  grep '^SALTS_ANDROID_RUNTIME_JSON ' "$results/runtime-output.txt" |
    sed 's/^SALTS_ANDROID_RUNTIME_JSON //'
)"
PROBE_JSON="$probe_json" python3 - <<'PY'
import json, os
record = json.loads(os.environ["PROBE_JSON"])
required = (
    "package_consumer",
    "cmeta",
    "clock",
    "thread_condition",
    "deadline_queue",
    "executor_bounded",
    "scheduler_timer",
    "scheduler_shutdown",
    "reactive_wait_wake_cancel",
)
assert record.get("schema") == "salts-android-runtime/v1"
for key in required:
    assert record.get(key) is True, (key, record.get(key))
assert record.get("executor_runs") == 2
assert record.get("timer_runs") == 1
assert record.get("reactive_values") == 1
assert record.get("reactive_value") == 42
PY

clang_bin="$(
  find "$ndk_root/toolchains/llvm/prebuilt" \
    -type f -path '*/bin/clang' -print -quit
)"
clang_version="$("$clang_bin" --version | head -n 1)"

export ABI="$abi"
export API="$api"
export MODEL="$model"
export FINGERPRINT="$fingerprint"
export QEMU="$qemu"
export UNAME_TEXT="$uname_text"
export MACHINE="$machine"
export NDK_ROOT="$ndk_root"
export CLANG_VERSION="$clang_version"
export RUNTIME_COMMAND="$runtime_command"
export PROBE_JSON="$probe_json"

python3 - <<'PY'
import json, os, pathlib
result = {
    "schema": "salts-android-runtime-evidence/v1",
    "runtime": {
        "api_level": int(os.environ["API"]),
        "abi": os.environ["ABI"],
        "machine": os.environ["MACHINE"],
        "model": os.environ["MODEL"],
        "fingerprint": os.environ["FINGERPRINT"],
        "ro_kernel_qemu": os.environ["QEMU"],
        "uname": os.environ["UNAME_TEXT"],
    },
    "toolchain": {
        "ndk_revision": os.environ["SALTS_ANDROID_NDK_REVISION"],
        "ndk_root": os.environ["NDK_ROOT"],
        "clang": os.environ["CLANG_VERSION"],
        "compile_platform": "android-24",
        "compile_abi": "arm64-v8a",
    },
    "package": {
        "salts_prefix": "stage/sdk/android-arm64-v8a",
        "consumer": "find_package(Salts CONFIG REQUIRED)",
        "source_tree_linkage": False,
    },
    "execution": {
        "command": os.environ["RUNTIME_COMMAND"],
        "probe": json.loads(os.environ["PROBE_JSON"]),
    },
}
path = pathlib.Path(os.environ["GITHUB_WORKSPACE"]) / "android-runtime-results" / "runtime-evidence.json"
path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
PY

cat "$results/runtime-evidence.json"
