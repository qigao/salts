# Qigao.Re2c.Tools

Prebuilt host executables and stdlib data for upstream re2c 4.6.

This package is intended to remove repeated re2c bootstrap work from Salts and
other C/C++ CI pipelines.

## Layout

- `tools/linux-x64/bin/re2c` plus `tools/linux-x64/share/re2c/stdlib/`
- `tools/windows-x64/bin/re2c.exe` plus `tools/windows-x64/share/re2c/stdlib/`
- `tools/macos-x64/bin/re2c` or `tools/macos-arm64/bin/re2c`, each with `share/re2c/stdlib/`

Use the executable matching the build host. NuGet/Actions ZIP extraction may not preserve
Unix executable bits, so Linux/macOS consumers should run `chmod +x` on the restored
`bin/re2c` file before invoking it.

Android is intentionally not included:
re2c runs on the host and generates source code before the Android cross-build.

Upstream project: https://github.com/skvadrik/re2c
Package version: 4.6.3 (upstream re2c 4.6; packaging revision 3)
Upstream release: 4.6
