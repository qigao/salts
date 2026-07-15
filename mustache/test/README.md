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

```bash
cmake --build build --target test_spec_runner
ctest --test-dir build -R test_spec_runner
```
