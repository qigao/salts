#!/usr/bin/env bash
set -euo pipefail

: "$GITHUB_WORKSPACE"

bundle="$GITHUB_WORKSPACE/stage/runtime-bundle"
results="$GITHUB_WORKSPACE/android-runtime-results"
cross_build="$GITHUB_WORKSPACE/android-cross-build-results/cross-build.json"
consumer_meta="$GITHUB_WORKSPACE/android-cross-build-results/package-consumer.json"

test -f "$bundle/salts_android_runtime_probe"
test -f "$cross_build"
test -f "$consumer_meta"

rm -rf "$results"
mkdir -p "$results"

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

while IFS= read -r file; do
  adb push "$file" "$remote/$(basename "$file")" >/dev/null
done < <(find "$bundle" -maxdepth 1 -type f | sort)

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

export ABI="$abi"
export API="$api"
export MODEL="$model"
export FINGERPRINT="$fingerprint"
export QEMU="$qemu"
export UNAME_TEXT="$uname_text"
export MACHINE="$machine"
export RUNTIME_COMMAND="$runtime_command"
export PROBE_JSON="$probe_json"

python3 - <<'PY'
import json, os, pathlib

root = pathlib.Path(os.environ["GITHUB_WORKSPACE"])
cross_build = json.loads(
    (root / "android-cross-build-results" / "cross-build.json").read_text()
)
consumer = json.loads(
    (root / "android-cross-build-results" / "package-consumer.json").read_text()
)
result = {
    "schema": "salts-android-runtime-evidence/v1",
    "cross_build": cross_build,
    "package_consumer": consumer,
    "runtime": {
        "api_level": int(os.environ["API"]),
        "abi": os.environ["ABI"],
        "machine": os.environ["MACHINE"],
        "model": os.environ["MODEL"],
        "fingerprint": os.environ["FINGERPRINT"],
        "ro_kernel_qemu": os.environ["QEMU"],
        "uname": os.environ["UNAME_TEXT"],
        "system_image": "system-images;android-30;aosp_atd;arm64-v8a",
        "acceleration": "off",
    },
    "execution": {
        "command": os.environ["RUNTIME_COMMAND"],
        "probe": json.loads(os.environ["PROBE_JSON"]),
    },
}
path = root / "android-runtime-results" / "runtime-evidence.json"
path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
PY

cat "$results/runtime-evidence.json"
