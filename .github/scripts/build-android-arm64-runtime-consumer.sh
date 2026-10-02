#!/usr/bin/env bash
set -euo pipefail

: "$GITHUB_WORKSPACE"
: "$RUNNER_TEMP"
: "$ANDROID_NDK_HOME"

sdk_prefix="$GITHUB_WORKSPACE/stage/sdk/android-arm64-v8a"
consumer_src="$RUNNER_TEMP/salts-android-runtime-consumer"
consumer_build="$RUNNER_TEMP/salts-android-runtime-build"
bundle="$GITHUB_WORKSPACE/stage/runtime-bundle"

test -f "$sdk_prefix/lib/cmake/Salts/SaltsConfig.cmake"
test -f "$GITHUB_WORKSPACE/cflow/tests/android_runtime_probe.c"
test -f "$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"

rm -rf "$consumer_src" "$consumer_build" "$bundle"
mkdir -p "$consumer_src" "$bundle"

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
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-24 \
  -DANDROID_STL=c++_shared \
  -DSalts_DIR="$sdk_prefix/lib/cmake/Salts"

cmake --build "$consumer_build" --parallel 2

probe="$consumer_build/salts_android_runtime_probe"
test -f "$probe"
cp "$probe" "$bundle/salts_android_runtime_probe"

while IFS= read -r library; do
  cp "$library" "$bundle/$(basename "$library")"
done < <(find "$sdk_prefix" -type f -name '*.so' | sort)

libcxx="$(
  find -L "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt" \
    -type f -path '*/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so' \
    -print -quit
)"
if [[ -n "$libcxx" ]]; then
  cp "$libcxx" "$bundle/libc++_shared.so"
fi

clang_bin="$(
  find -L "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt" \
    -type f -path '*/bin/clang' -print -quit
)"
test -n "$clang_bin"
clang_version="$("$clang_bin" --version | head -n 1)"

python3 - <<'PY'
import json, os, pathlib
root = pathlib.Path(os.environ["GITHUB_WORKSPACE"])
bundle = root / "stage" / "runtime-bundle"
result = {
    "schema": "salts-android-package-consumer/v1",
    "consumer": "find_package(Salts CONFIG REQUIRED)",
    "source_tree_linkage": False,
    "abi": "arm64-v8a",
    "android_platform": "android-24",
    "ndk_revision": os.environ["SALTS_ANDROID_NDK_REVISION"],
    "clang": os.environ["SALTS_ANDROID_CLANG_VERSION"],
    "probe": "salts_android_runtime_probe",
    "bundle_files": sorted(p.name for p in bundle.iterdir() if p.is_file()),
}
(root / "android-cross-build-results").mkdir(parents=True, exist_ok=True)
(root / "android-cross-build-results" / "package-consumer.json").write_text(
    json.dumps(result, indent=2, sort_keys=True) + "\n"
)
PY
