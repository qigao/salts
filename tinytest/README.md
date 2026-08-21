# tinytest

BDD/TDD testing framework for C and C++. The runtime is a static library with no
dependency beyond libc. C tests include `tinytest.h`; C++ tests include
`tinytest.hpp`; both link `TurboUtils::TinyTest`.

Features: spec/describe/it, given/when/then, TDD TEST_CASE/SECTION, check/check_warn, strict-C11 generic assertions, benchmarking, TAP, JUnit XML, color output, test filtering.

## Quick Start

```c
#include "tinytest.h"

spec("strncmp") {
    it("should return 0 when strings are equal") {
        check(strncmp("foo", "foo", 12) == 0);
    }

    it("should return non-0 when strings differ") {
        check(strncmp("foo", "bar", 12) != 0);
    }
}
```

```cmake
add_executable(strncmp_spec strncmp_spec.c)
target_link_libraries(strncmp_spec PRIVATE TurboUtils::TinyTest)
```

```
strncmp
  should return 0 when strings are equal (OK)
  should return non-0 when strings differ (OK)
```

## Assertions

### check (fatal)

```c
check(x > 0);
check(ptr != NULL);
check(x > 0, "expected positive but got %d", x);
```

### check_warn (non-fatal)

```c
check_warn(a == 1);
check_warn(b == 2);
```

### Generic Assertions

C tests require C11. The generic comparisons support integer, floating-point,
boolean, string, and pointer values as appropriate. Parameter order is
`actual, expected`.

```c
check_equal(actual, expected);         check_not_equal(actual, expected);
check_greater(actual, expected);       check_greater_equal(actual, expected);
check_less(actual, expected);          check_less_equal(actual, expected);
check_in_range(actual, lo, hi);
check_within(actual, expected, margin); /* float, double, long double */
```

Every generic assertion has a non-fatal `_warn` form:

```c
check_equal_warn(actual, expected);
check_greater_equal_warn(actual, expected);
check_within_warn(actual, expected, margin);
```

### String

```c
check_equal(actual, expected);
check_not_equal(actual, expected);
check_contains(haystack, needle);
check_starts_with(str, prefix);
check_ends_with(str, suffix);
```

### Memory / Pointer

```c
check_equal(actual, expected, len);
check_not_equal(actual, expected, len);
check_null(ptr);           check_not_null(ptr);
check_equal(actual, expected);  check_not_equal(actual, expected);
```

The two-argument form compares values. The three-argument form compares exactly
`len` bytes; there is no separate `check_mem_*` family.

### Custom C value types

Strict C11 tests can register value equality without depending on CMeta.
`tinytest.h` automatically includes the historically named internal traits
adapter `tinytest_cmeta.h`:

```c
#include <stdbool.h>

typedef struct Point { int x, y; } Point;

static bool point_equal(const Point *actual, const Point *expected) {
    return actual->x == expected->x && actual->y == expected->y;
}

#define TTEST_USER_EQUAL_TRAIT_LIST , (POINT, Point, point_equal)
#include "tinytest.h"

check_equal((Point){1, 2}, (Point){1, 2});
```

Each row is `(TOKEN, C_TYPE, COMPARATOR)`. The comparator borrows two
`const C_TYPE *` values. Unregistered structures are rejected at compile time.
`tinytest_cmeta.h` and `tinytest_internal.h` are transitive implementation
headers; applications include only `tinytest.h` (or `tinytest.hpp` for C++).
Language-neutral runner, reporting, benchmark, temporary-file, and tree-management
implementations live in `tinytest.c`. The public header retains only the macros
that must expand in the test translation unit and the C++ assertion-unwind adapter.

### Boolean / Bits

```c
check_true(val);           check_false(val);
check_bits(actual, mask);  /* (actual & mask) == mask */
```

## Diagnostic Context

```c
info("request_id=%d", req_id);     /* prints on failure only */
capture(count, "%d");              /* expands to info("count=%d", count) */
```

## Test Structure

### spec / describe / it

```c
spec("my module") {
    describe("feature") {
        it("should work") { check(true); }
    }
}
```

> Test and group names passed as a single argument are treated as literal strings, never printf formats, so names may contain `%` safely.
> To build a name from a format string, pass the format plus its arguments, e.g. `it("row %zu", index)`.

### TEST_CASE / SECTION (TDD style)

```c
spec("tests") {
    TEST_CASE("arithmetic") {
        check_equal(1 + 1, 2);

        SECTION("subtraction") {
            check_equal(3 - 1, 2);
        }
    }

    TEST_CASE("strings", "[string]") {
        check_equal("a", "a");
    }
}
```

### given / when / then (BDD)

```c
spec("account") {
    given("a user with balance 100") {
        static int balance;
        before_each() { balance = 100; }

        when("withdrawing 50") {
            then("should have 50 remaining") {
                balance -= 50;
                check_equal(balance, 50);
            }
        }
    }
}
```

### Focus / Skip / Expected Failure

```c
it("normal test") { }
fit("focused - only this runs") { }
xit("skipped") { }
it_should_fail("known bug #123") { check(broken()); }
```

Focused and CLI-filtered cases that are not selected are hidden from normal console output and
reported separately as `FILTERED` in the summary. TAP and JUnit still emit those discovered cases
as skipped entries so their test plans remain complete. Explicit `xit` cases are also hidden from
normal console output and counted as `SKIPPED`; use `--list`, `--tap`, or JUnit to inspect them.

## Setup / Teardown

```c
spec("with fixtures") {
    static int counter;
    before_all() { counter = 0; }
    before_each() { counter++; }
    it("first") { check_equal(counter, 1); }
    it("second") { check_equal(counter, 2); }
}
```

Variables shared between setup and tests must be `static`.

## Mocking

C mocks use the same strict-C11 builtin trait map as generic assertions. Use the
single variadic entry for one to six parameters; zero-parameter mocks have a
separate form because portable C11 has no empty-variadic facility.

```c
#include "tinymock.h"

TINYMOCk_MOCK(int, add, int, int)
TINYMOCk_MOCK_VOID(log_value, int)
TINYMOCk_MOCK0(int, read_status)

spec("mocked add") {
    it("returns the scripted value") {
        mock_add_reset();
        mock_add_expect(TINYMOCk_ARG(2), TINYMOCk_ARG(3), TINYMOCk_RETURN(5));
        check_equal(add(2, 3), 5);
        mock_add_verify();
    }
}
```

The numbered `TINYMOCk_MOCK1/2/3` and `TINYMOCk_MOCK_DEFINE1/2` forms are not
provided. C++ tests include `tinymock.hpp` and use `tinymock::function_mock` or
the `TINYMOCK_CPP_MOCK_METHOD*` method generators.

`TINYMOCk_VALUE(value)` boxes a supported builtin value and
`TINYMOCk_VALUE_AS(type, value)` unboxes it. Both are strict-C11 `_Generic`
interfaces; the ABI-specific conversion handlers are implementation details.

## Benchmarking

```c
spec("performance") {
    bench("parser") {
        benchmark_batch("parse_json", 10000) {
            parse_json(input, len);
        }

        benchmark_ops("parse_messages", 1000, message_count) {
            for (size_t i = 0; i < message_count; ++i) {
                parse_json(messages[i].data, messages[i].len);
            }
        }

        benchmark_bytes("scan_64KiB", 1000, input_len) {
            scan(input, input_len);
        }

        benchmark_io("parse_packet_batch", 1000, packet_count, batch_bytes) {
            parse_packets(packets, packet_count);
        }
    }
}
```

Choose the macro that describes the work performed by one timed sample:

- `benchmark_batch(title, samples)` treats each block execution as one operation.
- `benchmark_ops(title, samples, operations_per_sample)` reports batched operation latency and `ops/s`.
- `benchmark_bytes(title, samples, bytes_per_sample)` reports `MiB/s` for byte-oriented work.
- `benchmark_io(title, samples, operations_per_sample, bytes_per_sample)` reports both operation and byte throughput.

The framework executes the block once per sample. It does not repeat the block
`operations_per_sample` times, so the count must match the work actually done inside the block.
Setup and allocation that are not part of the measurement should remain outside the benchmark block.
Count arguments are evaluated once, converted to `size_t`, and must be greater than zero when the
selected macro reports that unit. The macros return no value; an invalid count is a framework error.

Output keeps timed samples separate from logical work: `samples`, `ops/sample`, `bytes/sample`,
`avg/op(us)`, `min/sample(us)`, `max/sample(us)`, `ops/s`, and `MiB/s`. Sample min/max are not
divided by the batch size because they are measurements of the complete timed block.

The legacy `benchmark(title, samples, operations_per_sample)` macro remains available as a
source-compatible alias of `benchmark_ops`, but new code should use an explicit macro. In
particular, a zero legacy third argument now fails instead of silently acting as one operation.

## Custom Main

Define `TINYTEST_NO_MAIN` before including the header to suppress the built-in `main()`:

```c
#define TINYTEST_NO_MAIN
#include "tinytest.h"

int main(int argc, char **argv) {
    /* custom setup */
    return run_tests(argc, argv);
}
```

## CLI Options

```
./my_spec [options]

  --tap              TAP v13 output
  --color            Force colored output
  --no-color         Disable colored output
  --junit <file>     Generate JUnit XML report
  --list, -l         List all test names
  --filter, -f <pat> Run only tests whose name contains <pat>
  --help, -h         Show help
```

## C++ Container Assertions

Include `tinytest.hpp` to enable the C++-only container, string, generic, and
exception assertions.

```cpp
check_eq_container(vec_a, vec_b);       /* any iterable container */
check_map_eq(map_a, map_b);           /* maps with key/value diff */
check_contains(vec, value);
check_not_contains(vec, value);
check_size(vec, 3);
check_empty(vec);
check_not_empty(vec);
check_map_has_key(map, key);
check_map_not_has_key(map, key);
check_equal(s, "hello");
check_not_equal(s, "other");
check_contains(s, "ell");
check_starts_with(s, "hel");
check_ends_with(s, "llo");
check_empty(s);
check_not_empty(s);
```

## C++ Template Assertions

Generic assertions that work with any type supporting `operator==` and `operator<<`.

```cpp
check_equal(actual, expected);       /* T == T, auto-formats on failure */
check_not_equal(actual, expected);   /* T != T */
check_greater(actual, expected);     /* T > T */
check_less(actual, expected);        /* T < T */
```

## C++ Exception Testing

Lowercase assertion macros for testing exceptions.

```cpp
/* Fatal — test stops on failure */
check_throws(expr);                /* must throw any exception */
check_throws_as(expr, ExType);     /* must throw specific type */
check_throws_with(expr, "msg");    /* what() must contain "msg" */
check_nothrow(expr);               /* must not throw */

/* Non-fatal — test continues on failure */
check_throws_warn(expr);
check_throws_as_warn(expr, ExType);
check_nothrow_warn(expr);
```

Example:

```cpp
#include "tinytest.hpp"
#include <stdexcept>

int divide(int a, int b) {
    if (b == 0) throw std::invalid_argument("division by zero");
    return a / b;
}

spec("exception tests") {
    it("should throw on division by zero") {
        check_throws_as(divide(1, 0), std::invalid_argument);
        check_throws_with(divide(1, 0), "division by zero");
    }

    it("should not throw on valid input") {
        check_nothrow(divide(10, 2));
        check_equal(divide(10, 2), 5);
    }
}
```

## Known Limitations

- On C++ tests, a failed `check_*` assertion unwinds via an internal C++ exception so destructors of stack objects in the failing test body run, and is caught at the test boundary. The framework's `check_throws*`/`check_nothrow*` macros rethrow that internal exception so a failed sub-assertion is never misclassified as a thrown user exception. C builds use `setjmp`/`longjmp` instead.

- `check_warn(...)` failures are diagnostics only: they neither fail the test nor count toward the assertion totals.

- `check_int_*`/`check_uint_*` operate on 32-bit values; use `check_ll_*`/`check_ull_*` for 64-bit integers to avoid silent truncation.


## License

MIT License. Copyright (c) 2016 Dmitriy Kubyshkin.
