from pathlib import Path

path = Path('.github/workflows/cmeta.yml')
text = path.read_text()

# Trigger the canonical workflow for the migrated shared-owner regression.
marker = '      - "tests/install_consumer/**"\n'
if text.count(marker) != 2:
    raise SystemExit(f'unexpected trigger marker count: {text.count(marker)}')
text = text.replace(marker, marker + '      - "tests/cmeta_shared_owner/**"\n')

# All canonical jobs must test the actual PR head, not GitHub's synthetic merge ref.
checkout_v4 = '      - uses: actions/checkout@v4\n'
checkout_v6 = '      - uses: actions/checkout@v6\n'
if text.count(checkout_v4) != 2 or text.count(checkout_v6) != 2:
    raise SystemExit(f'unexpected checkout counts: v4={text.count(checkout_v4)} v6={text.count(checkout_v6)}')
text = text.replace(
    checkout_v4,
    checkout_v4 + '        with:\n          ref: ${{ github.event.pull_request.head.sha || github.sha }}\n          persist-credentials: false\n',
)
text = text.replace(
    checkout_v6,
    checkout_v6 + '        with:\n          ref: ${{ github.event.pull_request.head.sha || github.sha }}\n          persist-credentials: false\n',
)

linux_marker = '''      - name: Verify installed package targets
        run: cmake --build --preset linux-release-user --target verify_installed_package

      - name: Test full Linux suite
'''
linux_insert = '''      - name: Verify installed package targets
        run: cmake --build --preset linux-release-user --target verify_installed_package

      - name: Verify CMeta shared ownership with sanitizers
        shell: bash
        run: |
          set -euxo pipefail
          cmake -S tests/cmeta_shared_owner -B build/cmeta-owner -G Ninja \\
            -DCMAKE_BUILD_TYPE=Release \\
            -DCMETA_OWNER_SANITIZERS=ON
          cmake --build build/cmeta-owner -j2
          ctest --test-dir build/cmeta-owner --no-tests=error --output-on-failure

      - name: Verify installed CMeta shared ownership
        shell: bash
        run: |
          set -euxo pipefail
          cmake -S tests/cmeta_shared_owner -B build/cmeta-owner-installed -G Ninja \\
            -DCMAKE_BUILD_TYPE=Release \\
            -DCMETA_PACKAGE_DIR="$GITHUB_WORKSPACE/external/pkgs/salts/release/lib/cmake/Salts"
          cmake --build build/cmeta-owner-installed -j2
          ctest --test-dir build/cmeta-owner-installed --no-tests=error --output-on-failure

      - name: Test full Linux suite
'''
if text.count(linux_marker) != 1:
    raise SystemExit(f'unexpected linux insertion marker count: {text.count(linux_marker)}')
text = text.replace(linux_marker, linux_insert, 1)

linux_end = '''          ctest --preset linux-dev-user \\
            -R "^(cflow_native_(socket|pipe|file)_example|cflow_io_(native|native_adapter|pipe|file)_test|cflow_process_test)$" \\
            --output-on-failure

  macos:
'''
linux_end_insert = '''          ctest --preset linux-dev-user \\
            -R "^(cflow_native_(socket|pipe|file)_example|cflow_io_(native|native_adapter|pipe|file)_test|cflow_process_test)$" \\
            --output-on-failure

      - name: Check exact head and unchanged tracked source
        if: always()
        shell: bash
        run: |
          set -euo pipefail
          test "$(git rev-parse HEAD)" = "${{ github.event.pull_request.head.sha || github.sha }}"
          test -z "$(git status --porcelain --untracked-files=no)"

  macos:
'''
if text.count(linux_end) != 1:
    raise SystemExit(f'unexpected linux end marker count: {text.count(linux_end)}')
text = text.replace(linux_end, linux_end_insert, 1)

windows_marker = '''      - name: Install release profile
        shell: cmd
        run: |
          call "%VSINSTALL%\\Common7\\Tools\\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
          if errorlevel 1 exit /b 1
          cmake --build --preset install-win-release-user

      - name: Verify installed package targets
'''
windows_insert = '''      - name: Install release profile
        shell: cmd
        run: |
          call "%VSINSTALL%\\Common7\\Tools\\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
          if errorlevel 1 exit /b 1
          cmake --build --preset install-win-release-user

      - name: Verify CMeta shared ownership
        shell: cmd
        run: |
          call "%VSINSTALL%\\Common7\\Tools\\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
          if errorlevel 1 exit /b 1
          cmake -S tests/cmeta_shared_owner -B build/cmeta-owner -G Ninja -DCMAKE_BUILD_TYPE=Release
          if errorlevel 1 exit /b 1
          cmake --build build/cmeta-owner -j2
          if errorlevel 1 exit /b 1
          ctest --test-dir build/cmeta-owner --no-tests=error --output-on-failure
          if errorlevel 1 exit /b 1
          cmake -S tests/cmeta_shared_owner -B build/cmeta-owner-installed -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMETA_PACKAGE_DIR="%GITHUB_WORKSPACE%\\external\\pkgs\\salts\\release\\lib\\cmake\\Salts"
          if errorlevel 1 exit /b 1
          cmake --build build/cmeta-owner-installed -j2
          if errorlevel 1 exit /b 1
          ctest --test-dir build/cmeta-owner-installed --no-tests=error --output-on-failure

      - name: Verify installed package targets
'''
if text.count(windows_marker) != 1:
    raise SystemExit(f'unexpected windows insertion marker count: {text.count(windows_marker)}')
text = text.replace(windows_marker, windows_insert, 1)

windows_end = '''      - name: Verify installed package targets
        shell: cmd
        run: |
          call "%VSINSTALL%\\Common7\\Tools\\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
          if errorlevel 1 exit /b 1
          cmake --build --preset win-release-user --target verify_installed_package
'''
windows_end_insert = windows_end + '''

      - name: Check exact head and unchanged tracked source
        if: always()
        shell: pwsh
        run: |
          $ErrorActionPreference = "Stop"
          $expected = "${{ github.event.pull_request.head.sha || github.sha }}"
          $actual = (git rev-parse HEAD).Trim()
          if ($actual -ne $expected) { throw "expected head $expected, got $actual" }
          $dirty = @(git status --porcelain --untracked-files=no)
          if ($dirty.Count -ne 0) { throw "tracked source changed: $($dirty -join '; ')" }
'''
if text.count(windows_end) != 1:
    raise SystemExit(f'unexpected windows end marker count: {text.count(windows_end)}')
text = text.replace(windows_end, windows_end_insert, 1)

path.write_text(text)
