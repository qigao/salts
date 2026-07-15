# TurboUtils Mustache Module

A C11 Mustache template engine with JSON and XML data providers, reusable
compiled templates, callback-based output, and TurboUtils arena support.

## Features

- Mustache variables, sections, inverted sections, comments, delimiter changes,
  partials, dotted names, and optional lambdas
- JSON integration through `TurboUtils::JsonParser`
- XML integration through cxml
- HTML-escaped, unescaped, custom streaming, and arena-backed output
- Immutable compiled templates that can be reused across renders
- Bounded partial/lambda expansion for untrusted templates
- Mustache specification fixtures and focused JSON/XML/runtime regressions

## Build and Link

Inside the TurboUtils build, link the Mustache target and the parser used by the
application:

```cmake
target_link_libraries(json_app PRIVATE
  TurboUtils::Mustache
  TurboUtils::JsonParser)

target_link_libraries(xml_app PRIVATE
  TurboUtils::Mustache
  cxml)
```

Installed consumers can obtain the exported TurboUtils targets with:

```cmake
find_package(TurboUtils CONFIG REQUIRED)
```

## Quick Start: JSON to String

This is a complete example with all required ownership and error checks:

```c
#include "json_parser.h"
#include "mustache_json.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
  static const char template_text[] =
      "Hello {{name}}! You have {{count}} messages.";
  static const char json_text[] =
      "{\"name\":\"John\",\"count\":5}";
  json_value_t *data = NULL;
  MUSTACHE_TEMPLATE *templ = NULL;
  MUSTACHE_STRING_RENDERER renderer = {0};
  char *result = NULL;
  int renderer_ready = 0;
  int status = 1;

  data = json_parse(json_text, sizeof(json_text) - 1);
  if (!data) {
    fprintf(stderr, "JSON parse failed: %s\n", json_get_error());
    goto cleanup;
  }

  templ = mustache_compile(template_text, sizeof(template_text) - 1,
                           NULL, NULL, 0);
  if (!templ) {
    fputs("template compilation failed\n", stderr);
    goto cleanup;
  }

  if (mustache_string_renderer_init(&renderer) != 0) {
    fputs("renderer initialization failed\n", stderr);
    goto cleanup;
  }
  renderer_ready = 1;

  if (mustache_render_json(templ, data, &renderer.base, &renderer,
                           NULL, NULL) != 0) {
    fputs("template rendering failed\n", stderr);
    goto cleanup;
  }

  result = mustache_string_renderer_get(&renderer);
  if (!result) {
    fputs("result allocation failed\n", stderr);
    goto cleanup;
  }

  puts(result);
  status = 0;

cleanup:
  free(result);
  if (renderer_ready) mustache_string_renderer_free(&renderer);
  mustache_release(templ);
  json_free(data);
  return status;
}
```

Output:

```text
Hello John! You have 5 messages.
```

## Template Syntax

```mustache
{{name}}             HTML-escaped variable
{{{name}}}           Unescaped variable
{{&name}}            Unescaped variable, alternative form
{{#users}}...{{/users}}
                     Section; iterates arrays/lists
{{^users}}...{{/users}}
                     Inverted section
{{>header}}           Partial
{{! comment }}        Comment
{{=<% %>=}}           Change delimiters
{{user.name}}         Dotted lookup
```

## Provider Semantics

### JSON

| JSON value | Interpolation | Section behavior |
| --- | --- | --- |
| Object | `[object]` | Truthy scalar; named children are addressable |
| Array | `[object]` | Iterates elements; an empty array is falsey |
| String | Original bytes | Empty string is falsey |
| Number | Original JSON number text | Truthy, including zero |
| `true` | `true` | Truthy |
| `false` | Empty | Falsey |
| `null` | Empty | Falsey |

Number interpolation uses `json_number_text()`, so integer values such as
`9007199254740993` and `18446744073709551615` are not converted through
`double` and remain exact.

JSON object lookup uses length-delimited `tstr_v` keys. A Mustache tag name
does not need to be copied or NUL-terminated before lookup.

### XML

XML element and attribute names are matched case-sensitively by qualified name:

- `{{Foo}}` and `{{foo}}` are different lookups.
- `{{a:item}}`, `{{b:item}}`, and `{{item}}` are different lookups.
- Repeated sibling elements with the same qualified name form an iterable
  section.
- Attributes and leaf-element text interpolate as strings.
- An element with element children interpolates as `[object]`.

This exact-name behavior prevents namespace-local-name collisions. Templates
that previously relied on case-insensitive or local-name-only lookup must be
updated to use the XML qualified name.

## API Overview

| API | Purpose | Ownership/result |
| --- | --- | --- |
| `mustache_compile()` / `mustache_compile_v()` | Compile template text | Returns owned template or `NULL`; release with `mustache_release()` |
| `mustache_process()` | Render with a custom provider | Uses the default expansion limit; returns `0` on success |
| `mustache_process_ex()` | Render with an explicit expansion limit | `max_render_depth` must be nonzero |
| `mustache_render_json()` | One-call JSON provider setup and render | Borrows JSON data and renderer |
| `mustache_render_xml()` | One-call XML provider setup and render | Borrows the cxml tree and renderer |
| `mustache_string_renderer_get()` | Copy accumulated output | Returns a `malloc` allocation; caller uses `free()` |
| `mustache_string_renderer_get_arena()` | Borrow arena-backed output | Do not `free()`; invalid after renderer/pool release or later mutation |

`mustache_process()` remains the compatibility entry point and uses
`MUSTACHE_DEFAULT_MAX_RENDER_DEPTH` (currently 128). Use
`mustache_process_ex()` to choose a lower bound for untrusted templates:

```c
int rc = mustache_process_ex(templ, &renderer.base, &renderer,
                             &provider.base, &provider, 16);
```

The limit counts nested partial and lambda expansions. Exceeding it aborts the
render with a nonzero result; output already emitted before the failure is not
rolled back.

## Arena-Backed Output

Use a `mem_pool_t` when the rendered bytes should share an arena lifetime:

```c
#include "turbo_buffer.h"

mem_pool_t pool = {0};
MUSTACHE_STRING_RENDERER_ARENA renderer = {0};

if (mem_init(&pool, 0) != 0) return -1;
if (mustache_string_renderer_init_arena(&renderer, &pool, 4096) != 0) {
  mem_destroy(&pool);
  return -1;
}

int rc = mustache_render_json(templ, data, &renderer.base, &renderer,
                              NULL, NULL);
char *result = rc == 0 ? mustache_string_renderer_get_arena(&renderer) : NULL;
if (result) puts(result); /* borrowed: do not free */

mustache_string_renderer_free_arena(&renderer);
mem_destroy(&pool);
```

The arena renderer reserves space for a trailing NUL. The returned pointer is a
borrowed view and can change when more output is appended.

## Partials and Lambdas

Partial callbacks return borrowed compiled templates. The provider owns and
normally caches them; the renderer never calls `mustache_release()` on a partial:

```c
#include <string.h>

typedef struct PARTIAL_CACHE {
  MUSTACHE_TEMPLATE *header;
} PARTIAL_CACHE;

static MUSTACHE_TEMPLATE *lookup_partial(const char *name, size_t size,
                                         void *user_data) {
  PARTIAL_CACHE *cache = (PARTIAL_CACHE *)user_data;
  return size == 6 && memcmp(name, "header", 6) == 0
             ? cache->header
             : NULL;
}
```

Compile cached partials before rendering, keep them valid through the render,
and release them when the cache is destroyed. `NULL` means “partial not found”
and is not distinguishable from a loader I/O failure, so fallible loading should
be completed before rendering.

For lambda providers:

- `is_lambda(node, provider_data)` selects lambda nodes.
- `call_lambda(...)` returns `0` on success and nonzero on failure.
- On success, `*out_text` must be allocated with `malloc`; Mustache frees it.
- `*out_text == NULL` is valid only when `*out_len == 0`.
- Interpolation lambdas receive empty input; section lambdas receive the raw
  section source.
- Lambda output is compiled and rendered as a nested template and therefore
  consumes expansion depth.

## Custom Renderers and Providers

Both renderer callbacks must return `0` on success and nonzero on failure.
Failures abort rendering and propagate to the caller:

```c
static int file_out(const char *output, size_t size, void *renderer_data) {
  FILE *file = (FILE *)renderer_data;
  return fwrite(output, 1, size, file) == size ? 0 : -1;
}

MUSTACHE_RENDERER renderer = {file_out, file_out};
```

Set `out_escaped` equal to `out_verbatim` only when the target format requires
no escaping. A custom `MUSTACHE_DATAPROVIDER` must provide `dump`, `get_root`,
`get_child_by_name`, and `get_child_by_index`; `get_partial`, `is_lambda`, and
`call_lambda` are optional as documented in `mustache.h`.

## Errors and Partial Output

- Compile functions return `NULL` for invalid arguments, syntax errors, integer
  overflow, or allocation failure. A custom `MUSTACHE_PARSER.parse_error`
  callback receives syntax details.
- Renderer, provider, lambda, allocation, and expansion-depth failures cause
  processing functions to return nonzero.
- Rendering is streaming: bytes emitted before an error remain in the target.
  Callers that require atomic output should render to a temporary string first.
- `mustache_string_renderer_get()` returns `NULL` on invalid state or allocation
  failure.
- `mustache_render_xml()` checks internal provider allocation status. When using
  `mustache_xml_provider_init()` with `mustache_process()` directly, call
  `mustache_xml_provider_status()` before freeing the provider.
- `json_parse()` returns `NULL` on failure; `json_get_error()` provides the
  parser diagnostic.

## Thread Safety

- A compiled template is immutable after compilation and may be shared across
  threads.
- Do not mutate JSON/XML trees while they are being rendered.
- Use a separate renderer and provider instance per concurrent render.
- A shared partial cache must keep templates immutable and synchronize cache
  mutation outside active renders.
- Arena-backed output follows the thread-safety and lifetime rules of its
  `mem_pool_t`.

## Examples

- `examples/json_example.c`: JSON rendering
- `examples/xml_example.c`: XML attributes, dotted names, and repeated elements

The CMake targets are `mustache_json_example` and `mustache_xml_example`.

## Tests

With the repository presets configured, run the focused Release tests:

```powershell
cmake --build --preset win-release-user --target `
  test_mustache_json test_mustache_spec test_spec_runner test_scope `
  test_mustache_runtime test_mustache_xml

ctest --preset win-release-user `
  -R "^(test_mustache_json|test_mustache_spec|test_spec_runner|test_scope|test_mustache_runtime|test_mustache_xml)$" `
  --output-on-failure
```

`win-dev-user` enables MSVC AddressSanitizer and can be substituted for
`win-release-user` to exercise the same tests under ASan.

The specification fixtures come from the official
[Mustache specification](https://github.com/mustache/spec). Focused regressions
cover arena escaping, exact 64-bit JSON numbers, lambda/output error propagation,
recursive expansion limits, XML case/namespace matching, and repeated elements.
