# Mustache Tests

This directory contains runtime and specification tests for the Mustache engine.

## Test Suites

- `test_json_integration.c`: JSON integration tests
- `test_runtime_regressions.c`: arena, recursion, lambda, and callback regressions
- `test_xml_integration.c`: XML provider semantics
- `test_spec_adapted.c`: Mustache specification compliance tests (adapted)
- `test_spec_runner.c`: Specification test runner (JSON fixtures)
- `test_scope.c`: Scope climbing tests

## Running

Release:

```powershell
cmake --build --preset win-release-user --target `
  test_mustache_json test_mustache_spec test_spec_runner test_scope `
  test_mustache_runtime test_mustache_xml

ctest --preset win-release-user `
  -R "^(test_mustache_json|test_mustache_spec|test_spec_runner|test_scope|test_mustache_runtime|test_mustache_xml)$" `
  --output-on-failure
```

Use `win-dev-user` for the MSVC AddressSanitizer build. Start with the smallest
relevant test while debugging, then run the complete expression above before
handoff.
