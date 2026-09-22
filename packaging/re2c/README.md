# Qigao.Re2c.Tools

Prebuilt host executables for upstream re2c 4.6.

This package is intended to remove repeated re2c bootstrap work from Salts and
other C/C++ CI pipelines.

## Layout

- `tools/linux-x64/re2c`
- `tools/windows-x64/re2c.exe`
- `tools/macos-x64/re2c` or `tools/macos-arm64/re2c`

Use the executable matching the build host. NuGet/Actions ZIP extraction may not preserve\nUnix executable bits, so Linux/macOS consumers should run `chmod +x` on the restored\n`re2c` file before invoking it.\n\nAndroid is intentionally not included:
re2c runs on the host and generates source code before the Android cross-build.

Upstream project: https://github.com/skvadrik/re2c
Upstream release: 4.6
