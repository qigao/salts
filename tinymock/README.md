# TinyMock

TinyMock is the C test-double layer for Salts. It builds on `Salts::TinyTest`
for test failure/reporting and on `Salts::CMeta` for semantic type, interface,
and function metadata.

```text
CMeta semantic declarations
        |
        +-- CMETA_INTERFACE schema ---> mock vtable
        |
        '-- FunctionDecl schema ------> exact-ABI test replacement
                         |
                         v
                    TinyMock
             stub / ledger / verify
                         |
                         v
                    TinyTest
```

Production code does not depend on TinyMock.

## Link target

```cmake
target_link_libraries(my_test PRIVATE Salts::TinyMock)
```

`Salts::TinyMock` propagates `Salts::TinyTest + Salts::CMeta`.

## Interface mocks

For a CMeta interface, replay the same method schema:

```c
CMETA_INTERFACE(counter, COUNTER_METHODS);
TINYMOCk_INTERFACE(counter, COUNTER_METHODS);

tinymock_counter mock;
tinymock_counter_init(&mock);

counter dependency = tinymock_counter_as_interface(&mock);
```

No hand-written test vtable is required.

## Reflected free-function mocks

Production API:

```c
#include <cmeta/function.h>

FunctionDecl(value, int, dependency_add,
    (int, left, CMETA_PARAM_IN),
    (int, right, CMETA_PARAM_IN));
```

Test target:

```cmake
tinymock_add_function_mocks(my_test
  HEADERS dependency_api.h)
```

Test source:

```c
#include "dependency_api.h"
#include <tinymock_function.h>

TINYMOCk_USE(dependency_add);

mock_dependency_add_reset();
tinymock_mock_set_default_return(
    TINYMOCk_FUNCTION(dependency_add),
    TINYMOCk_RETURN(7));

check_equal(dependency_add(3, 4), 7);
tinymock_mock_verify_times(TINYMOCk_FUNCTION(dependency_add), 1);
```

The test repeats only the function name. The return type, parameter list,
parameter names, CMeta type descriptors, and parameter semantic flags still come
from the original `FunctionDecl`.

### How the portable backend works

`tinymock_add_function_mocks()` creates a C source file in the build tree. That
source loads CMeta first, overrides only the natural `FunctionDecl*` spellings,
and includes the production API header. CMeta's canonical `CMETA_FUNCTION_*`
macros remain unchanged and are replayed to publish the normal
`cmeta_function_desc`.

The same declaration tokens then produce the exact C function definition that
dispatches into TinyMock.

This avoids:

- parsing arbitrary C;
- libffi or dynamic invocation;
- compiler-specific reflection;
- a TinyMock-private function signature registry.

The first portable backend is a **replacement-definition backend**: do not link
the real implementation object for functions provided by the generated mock
translation unit. Linker interception for already-linked implementations is a
separate backend.

## Current value carrier

The current portable TinyMock carrier supports:

- signed/unsigned integer families;
- float/double/long double;
- C strings;
- `void *` / `const void *`;
- typed/opaque object pointers by pointer identity.

Arbitrary struct/union values by value, function-pointer ABI values, variadic
functions, and void-return reflected free functions are not part of the first
free-function slice.

## Invocation verification

All generated interface and free-function mocks use the same invocation ledger:

```c
tinymock_mock_verify_times(mock, 2);
tinymock_mock_verify_never(mock);
tinymock_mock_verify_at_least(mock, 1);
tinymock_mock_verify_at_most(mock, 3);

const tinymock_recorded_call_t *call =
    tinymock_mock_call_at(mock, 0);
```

CMeta answers what the function/interface is. TinyMock owns test behavior.
TinyTest owns test execution and reporting.
