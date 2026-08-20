# TurboUtils C11 Stream

A bounded, zero-allocation C11 stream module that separates four concerns:

```text
Container / Live Producer
        ↓
Source / Cursor
        ↓
Mutable ordered Pipeline
        ↓
Terminal / event loop
```

## Build

As part of TurboUtils, link the exported CMake target:

```cmake
target_link_libraries(my_app PRIVATE TurboUtils::Stream)
```

The repository regression test is available as target `test_stream` and through
CTest as `test_stream`.

The standalone examples can also be compiled directly.

Finite/container examples:

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic \
    stream.c stream_container.c examples/container_example.c -o container_example
```

Custom `STREAMABLE_REF(...)` example:

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic \
    stream.c stream_container.c examples/streamable_example.c -o streamable_example
```

Temporal/live example:

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic \
    stream.c stream_live.c examples/temporal_example.c -o temporal_example
```

## Core Mutable Pipeline

Intermediate operations mutate one stable `stream_t` and return `self`:

```c
stream_t stream;
stream_t *s = &stream;

if (STREAM_OF(s, int, 1, 2, 3, 4, 5) != STREAM_OK) {
    return 1;
}

s->filter(s, predicate)
 ->map(s, sizeof(Output), mapper)
 ->peek(s, inspect)
 ->take(s, 10)
 ->for_each(s, consume);
```

`STREAM_OF` is the C equivalent of Java `Stream.of(...)`: it accepts one or
more typed values directly, copies them into bounded stream-owned storage, and
requires neither a container nor a source-state variable. `STREAM_EMPTY(s,
Type)` creates an empty typed stream. C++17 callers may also write
`stream_of<int>(s, {1, 2, 3})`.

For C++ range-based iteration and fluent-style intermediate operations, include
`stream.hpp` and call methods directly on `turbo::stream::from<T>(stream)`:

```cpp
#include "stream.hpp"

size_t total = 0;
stream_t stream{};
STREAM_OF(&stream, int, 1, 2, 3, 4, 5, 6);

for (int value : turbo::stream::from<int>(stream)
                       .filter(is_even)
                       .skip(1)
                       .take(2)
                       .peek(peek_count)) {
    total += value;
}

for (int value : turbo::stream::from<int>(stream)
                       .map<int>(sizeof(int), square_int)) {
    ...;
}

turbo::stream::from<int>(stream).boxed().for_each([](const int *value) {
    if (value) { /* ... */ }
});

stream_t stream2{};
size_t total = 0;
auto view = turbo::stream::from<int>(stream2);
view.count(total);           // terminal count

int first = 0;
view.find_first(first);      // terminal find_first

int sum = 0;
view.reduce(sum, sum_reducer);
```

`stream.hpp` is a thin adaptor and keeps using the same C pipeline semantics
(`stream_t` and source state ownership unchanged).

The copied value slots remain valid through `reset` and `clear`. Pointer-bearing
elements still copy only the pointer/view, so their referenced payload must
outlive consumption. Direct values and stateful operators share
`STREAM_MAX_STATE_SIZE`; exhaustion returns `STREAM_ERROR` and records
`STREAM_ERR_STATE_FULL`.

The stream stores an ordered operation program, so operation order is exact.
No pipeline node allocation is required.

The complete pointer-style example is
[`examples/turbo_containers_example.c`](examples/turbo_containers_example.c):

```c
stream_t stream;
stream_t *s = &stream;

stream_from_turbo_vec(s, &values);
s->filter(s, is_even)
 ->map(s, sizeof(int), square)
 ->take(s, 3)
 ->for_each(s, print_int);
```

For a borrowed-string pipeline over `turbo_list_t`, see
[`examples/advanced_strings_example.c`](examples/advanced_strings_example.c).
It uses the typed `tstr_v_list_t_from(&words, source, count)` constructor, then
demonstrates string predicates, mapping, `for_each`, `clear`, and `reset`. The
list owns copies of the `tstr_v` structures; the referenced string bytes remain
owned by their original source.

Typed TurboUtils containers support the same bulk-construction style:

```c
TURBO_VEC_DEFINE(int_vec_t, int)
const int numbers[] = {1, 2, 3};
int_vec_t numbers_vec;
int_vec_t_from(&numbers_vec, numbers, 3);

TURBO_MAP_DEFINE(int_map_t, int, int)
const int_map_t_entry entries[] = {{1, 10}, {2, 20}};
int_map_t values_by_id;
int_map_t_from(&values_by_id, entries, 2);
```

`vec`, `deque`, `list`, `set`, and `heap` accept element arrays. `hash_map`,
`map`, `multimap`, `tree_map`, and `bplus_tree` generate a typed
`name_entry` structure and accept entry arrays. Elements, keys, and values are
copied into container-owned slots. Pointer-bearing values such as `tstr_v`
retain their normal shallow-copy/borrowed-payload semantics.

Implemented operators:

- `filter`
- `map`
- `peek`
- `boxed` (maps each element to a pointer token)
- `take`
- `limit` (Java-compatible alias for `take`)
- `take_while`
- `drop_while`
- `skip`
- `distinct(max_unique, equals)`
- `sorted(max_items, compare)`
- `flat_map(output_size, max_outputs, mapper)`
- `concat(other, max_items)`
- `window`
- `debounce` (count based)
- `debounce_ms` (timestamp based)

Implemented terminal operations:

- `for_each`
- `count`
- `reduce`
- `find_first`
- `find_any` (sequential alias for `find_first`)
- `any_match`
- `all_match`
- `none_match`
- `contains(target, equals, &out)`
- `min_value` / `stream_min`
- `max_value` / `stream_max`
- `to_array` / `STREAM_TO_ARRAY`
- `collect`

Terminal operations use explicit output parameters so source status remains
distinguishable. A completed finite drain returns `STREAM_END`; a successful
short circuit such as a matching `find_first` or `any_match` returns
`STREAM_OK`. `STREAM_AGAIN` means a live source is empty for now. For match
operations, the boolean returned with `STREAM_AGAIN` is provisional.

`to_array` copies into caller-owned fixed storage and requires the destination
element size to equal the current pipeline element size. If capacity is
reached, it returns `STREAM_FULL` before pulling another source value. The
`STREAM_TO_ARRAY` convenience macro infers capacity and element size from a
real C array; use `stream_to_array` directly for pointer-plus-capacity storage.

```c
size_t count = 0;
int sum = 0;
bool matched = false;

STREAM_OF(s, int, 1, 2, 3, 4, 5);
s->filter(s, is_even)->count(s, &count);       /* STREAM_END, count == 2 */

STREAM_OF(s, int, 1, 2, 3, 4, 5);
s->reduce(s, &sum, add_int);                   /* STREAM_END, sum == 15 */

STREAM_OF(s, int, 1, 2, 3, 4, 5);
s->any_match(s, is_even, &matched);            /* STREAM_OK, true */

STREAM_OF(s, int, 1, 2, 3, 4, 5);
s->contains(s, &(int){4}, int_equal, &matched); /* STREAM_OK, true */
```

See [`examples/java_style_example.c`](examples/java_style_example.c) for a
direct-values example using no traversal loop.

`collect` drains sequentially into a caller-initialized result object. The
accumulator receives that object and one stream value per call. There is no
combiner because this module currently has no parallel-stream execution mode;
callback failure records `STREAM_ERR_COLLECT_FAILED`.

`stream_collect_turbo_map_count` is also available in
[`stream_turbo_containers.h`](stream_turbo_containers.h): it groups values by a
key extracted by callback and stores key->count in an initialized `turbo_map_t`
whose value type is `size_t`.

## Range Source

`STREAM_RANGE` is the C equivalent of `IntStream.range`: it creates a finite,
resettable stream of `int64_t` values with an exclusive end. A stepped variant
supports ascending and descending ranges without heap allocation:

```c
int64_t output[8];
size_t count = 0;

STREAM_RANGE(s, -2, 10);
STREAM_TO_ARRAY(
    s->drop_while(s, is_negative)
     ->take_while(s, less_than_four),
    output,
    &count);                         /* {0, 1, 2, 3} */

STREAM_RANGE_STEP(s, 5, -1, -2);   /* {5, 3, 1} */
```

The range step must be nonzero. A step whose sign cannot move toward the end
creates an empty stream. Boundary arithmetic is checked without overflowing
signed `int64_t`. `take_while` consumes the first failing boundary item and
then stops permanently; `drop_while` tests only the prefix and passes every
remaining value unchanged. `reset` restores both operator states.

## Iterate and Generate Sources

Unlike Java's potentially infinite sources, the C11 forms always require a
hard item limit:

```c
int powers[5];
size_t count = 0;

STREAM_ITERATE(s, int, 1, 4, double_int);
STREAM_TO_ARRAY(s, powers, &count);       /* {1, 2, 4, 8} */

STREAM_GENERATE(s, int, 4, square_at);   /* indices 0 through 3 */
```

`iterate` copies both its seed and current value into stream-owned state.
`generate` passes a zero-based index to its callback. Reset restarts the owned
seed/index; deterministic replay still requires deterministic callbacks. A
callback result other than `STREAM_OK` is a source failure.

## Bounded Materialization

`sorted` and `flat_map` retain fluent pointer syntax but are eager bounded
barriers rather than lazy per-item operators:

```c
STREAM_OF(s, int, 2, 3);
s->flat_map(s, sizeof(int), 4, emit_value_and_square)
 ->sorted(s, 4, int_compare)
 ->collect(s, &summary, summarize_int);
```

Each barrier first drains the pipeline declared before it, copies the result
into the stream's bounded state arena, then replaces that pipeline with a new
owned, resettable source. Operators declared afterward apply to the
materialized values. This makes borrowed mapper output safe and keeps one
mutable owner, but differs from Java's lazy evaluation and requires finite
input. Receiving `STREAM_AGAIN` records `STREAM_ERR_NEEDS_FINITE_SOURCE`.
If a barrier fails after consumption starts, its partial materialized output is
not exposed, while the original source and earlier stateful operators remain
selected but already advanced. A resettable source can be reset before retry;
non-resettable/live input must therefore not be passed to these barriers.

`sorted` is stable and uses bottom-up merge sort: time is `O(n log n)` and its
temporary state is `2 * max_items * element_size`, plus alignment. Reading an
item beyond `max_items` records `STREAM_ERR_SORT_FULL`. `flat_map` copies each
emitted value through `stream_emitter_t`; exceeding `max_outputs` returns
`STREAM_FULL` to the mapper and records `STREAM_ERR_FLAT_MAP_FULL`. No barrier
silently grows, drops output, or retains emitted pointers. Pointer-bearing
values retain their usual shallow-copy payload lifetime.

`concat` appends a second finite stream in encounter order. It requires equal
element sizes and consumes both current pipelines; the second stream remains
owned by its caller and is not reset or closed by the first stream. The result
is resettable through the first stream. Reaching `max_items` records
`STREAM_ERR_CONCAT_FULL` before pulling another value.

`distinct` preserves first-seen order and copies at most `max_unique` values
into the stream state arena. Its memory use is bounded by
`max_unique * current_element_size`; the multiplication and remaining arena
capacity are checked when the operator is declared. Encountering another new
value after the limit returns `STREAM_ERROR` with
`STREAM_ERR_DISTINCT_FULL`. Its current implementation uses equality scanning,
so time is `O(n * max_unique)` and state is `O(max_unique * element_size)`.

`min_value` and `max_value` drain the current pipeline into a caller-provided
item buffer. The direct function forms are `stream_min` and `stream_max`.
The member names avoid collisions with the Windows SDK `min/max` macros. The
`out_found` flag distinguishes an empty stream. With a live source,
`STREAM_AGAIN` means the current extremum is provisional.

## Container Stream

Finite containers implement a cursor protocol. Each stream owns its own cursor.
Built-ins currently include:

- array view
- fixed-storage vector
- intrusive list
- custom containers through `STREAMABLE(...)` / `STREAMABLE_REF(...)`

Versioned containers are fail-fast: structural mutation during traversal returns
`STREAM_MODIFIED`.

## TurboUtils Containers

Include `stream_turbo_containers.h` to adapt containers from
`turbo_containers.h` without copying or transferring ownership:

- `stream_from_turbo_vec`
- `stream_from_turbo_deque`
- `stream_from_turbo_list`
- `stream_from_turbo_hash_keys` / `stream_from_turbo_hash_values`
- `stream_from_turbo_map_keys` / `stream_from_turbo_map_values`
- `stream_from_turbo_set`
- `stream_from_turbo_multimap_keys` / `stream_from_turbo_multimap_values`
- `stream_from_turbo_heap`
- `stream_from_turbo_tree_map_keys` / `stream_from_turbo_tree_map_values`
- `stream_from_turbo_bplus_tree_keys` / `stream_from_turbo_bplus_tree_values`
- `stream_collect_turbo_list`
- `stream_collect_turbo_set`
- `stream_collect_turbo_map`
- `stream_collect_turbo_map_with_conflict`
- `stream_collect_turbo_map_count`
- `stream_collect_turbo_multimap`
- `stream_collect_turbo_partition`
- `stream_collect_turbo_partition_count`
- `stream_collect_turbo_partition_reduce`

The adapter also provides bounded terminal collectors:

```c
turbo_vec_t output;
turbo_vec_init(&output, sizeof(int));

STREAM_OF(s, int, 1, 2, 3, 4);
stream_collect_turbo_vec(s->filter(s, is_even), &output, 8);

turbo_vec_destroy(&output);

turbo_set_t uniques;
turbo_set_init(&uniques, sizeof(int), NULL, NULL, NULL);
STREAM_OF(s, int, 1, 2, 2, 3);
stream_collect_turbo_set(s, &uniques, 8);
turbo_set_destroy(&uniques);

turbo_map_t by_remainder_two;
turbo_map_init(
    &by_remainder_two, sizeof(int), sizeof(int), NULL, NULL, NULL);
STREAM_OF(s, int, 1, 2, 3, 4, 5, 6);
stream_collect_turbo_map(
    s,
    &by_remainder_two,
    8,
    sizeof(int),
    sizeof(int),
    key_by_remainder_two,
    value_as_one,
    sum_reducer);

STREAM_OF(s, int, 1, 2, 3, 4, 5, 6);
stream_collect_turbo_map_with_conflict(
    s,
    &by_remainder_two,
    8,
    sizeof(int),
    sizeof(int),
    STREAM_TURBO_MAP_MERGE,
    key_by_remainder_two,
    value_as_one,
    sum_reducer);
turbo_map_destroy(&by_remainder_two);

turbo_list_t selected;
turbo_list_init(&selected, sizeof(int));
STREAM_OF(s, int, 1, 2, 3, 4);
stream_collect_turbo_list(s->filter(s, is_even), &selected, 8);
turbo_list_destroy(&selected);

turbo_map_t grouped;
turbo_map_init(&grouped, sizeof(int), sizeof(size_t), NULL, NULL, NULL);
STREAM_OF(s, int, 10, 11, 18, 24, 27, 34, 39, 41);
stream_collect_turbo_map_count(
    s,
    &grouped,
    8,
    sizeof(int),
    key_selector);
turbo_map_destroy(&grouped);

turbo_multimap_t grouped_values;
turbo_multimap_init(&grouped_values, sizeof(int), sizeof(int), NULL, NULL, NULL);
STREAM_OF(s, int, 10, 11, 18, 24, 27, 34, 39, 41);
stream_collect_turbo_multimap(
    s,
    &grouped_values,
    8,
    sizeof(int),
    sizeof(int),
    key_selector,
    value_as_one);
turbo_multimap_destroy(&grouped_values);

turbo_map_t parity_counts;
turbo_map_init(&parity_counts, sizeof(uint8_t), sizeof(size_t), NULL, NULL, NULL);
STREAM_OF(s, int, 1, 2, 3, 4, 5, 6);
stream_collect_turbo_partition_count(
    s,
    &parity_counts,
    2,
    is_even);
turbo_map_destroy(&parity_counts);

turbo_map_t parity_sums;
turbo_map_init(&parity_sums, sizeof(uint8_t), sizeof(int), NULL, NULL, NULL);
STREAM_OF(s, int, 10, 11, 12, 14, 15, 16);
stream_collect_turbo_partition_reduce(
    s,
    &parity_sums,
    2,
    sizeof(int),
    is_even,
    value_as_one,
    sum_reducer);
turbo_map_destroy(&parity_sums);

turbo_list_t long_words;
turbo_list_t short_words;
turbo_list_init(&long_words, sizeof(int));
turbo_list_init(&short_words, sizeof(int));
STREAM_OF(s, int, 1, 2, 3, 4, 5, 6);
stream_collect_turbo_partition(
    s,
    &long_words,
    8,
    &short_words,
    8,
    is_even);
turbo_list_destroy(&long_words);
turbo_list_destroy(&short_words);
```

where `key_selector` maps each source value to a key buffer of `sizeof(int)` bytes and
`value_as_one` maps each source value to a stored value.

`STREAM_TURBO_MAP_KEEP_LAST` replaces the mapped value,  
`STREAM_TURBO_MAP_KEEP_FIRST` keeps the first mapped value,  
`STREAM_TURBO_MAP_REJECT` returns `STREAM_ERROR` on duplicate keys,  
`STREAM_TURBO_MAP_MERGE` reduces duplicate keys with `value_reducer`.

`stream_collect_turbo_partition` partitions stream values to two destination lists
using predicate semantics. `max_true_items` and `max_false_items` are independent
hard limits. If both are already reached, the collector returns `STREAM_FULL`
without pulling another source value; if the next value belongs to a full
destination, it also returns `STREAM_FULL` without adding.

`max_items` is the hard final-size limit for the call. Reaching it
returns `STREAM_FULL` without pulling another source value. The collector
reserves capacity before consumption; reserve failure records
`STREAM_ERR_COLLECT_FAILED` and leaves the source untouched. Vector slots own
their copies, while pointer-bearing payloads remain shallow/borrowed.

`stream_collect_turbo_multimap` is the Java-like "groupingBy" variant for
multi-value buckets. It preserves all mapped values for each key; duplicate keys
remain as distinct entries and are still bounded by `max_items`.

`stream_collect_turbo_partition_count` is the Java-like `partitioningBy`
specialization where the partition key is the predicate result (`0` for false,
`1` for true) and each bucket stores an occurrence count in `size_t`.

`stream_collect_turbo_partition_reduce` is the Java-like `partitioningBy`
specialization with reducer support: each bucket stores one mapped value for key `0`
or `1`. `value_mapper` maps each source value to a reduced payload, and
`value_reducer` merges it with the bucket's existing value; when
`value_reducer == NULL`, duplicates replace the existing bucket value.

The stream cursor borrows container storage. The container must outlive the
stream and must not be structurally modified during traversal. The adapters
detect changes to observable storage, size, capacity, head, hash slot state, or
comparator state and return `STREAM_MODIFIED`. In-place value replacement is not a
structural change and is governed by the caller's single-threaded/external-lock
protocol.

`turbo_heap` is traversed in internal heap-array order (not sorted priority
pop order). Other container adapters are implemented where element contracts are
explicit; `turbo_multimap` adapters stream keys and values in entry order with
duplicate keys emitted per value.

## Live Stream

`stream_live_ring_t` is a fixed-capacity, non-blocking producer/consumer source.
It is intentionally separate from fail-fast container cursors.

Source result semantics:

```text
STREAM_OK      item produced
STREAM_AGAIN   no item now, producer may produce later
STREAM_END     producer closed and queue drained
STREAM_ERROR   failure
```

The public `STREAM_AGAIN` now has only this meaning. Pipeline-internal filter/
skip/window/debounce drops use a private flow state and never leak as AGAIN.
Source callback failures are normalized to `STREAM_ERROR` and recorded as
`STREAM_ERR_SOURCE_FAILED`; structural invalidation remains distinguishable as
`STREAM_MODIFIED` with `STREAM_ERR_SOURCE_MODIFIED`.

Backpressure policies:

- `STREAM_BP_REJECT_NEW`
- `STREAM_BP_DROP_NEWEST`
- `STREAM_BP_DROP_OLDEST`
- `STREAM_BP_LATEST_ONLY`

`stream_live_ring_t` is not internally synchronized. Use one event-loop thread
or external synchronization. A lock-free/thread-safe SPSC source can be added
as a separate source implementation without changing the pipeline engine.

## Window

```c
s->window(s, 5)
```

changes the current stream value from `T` to `stream_window_t`. The window owns
copies in stream state storage and preserves the actual sequence/timestamp of
items that survived upstream operators.

Because `stream_window_t::data` is a borrowed view, another stateful `window` or
`debounce*` cannot consume it directly. Such a pipeline fails fast with
`STREAM_ERR_BAD_ARGUMENT`. A `map` may materialize the window into an owned value
before another stateful operator.

```c
typedef struct stream_window {
    const void *data;
    size_t count;
    size_t element_size;
    uint64_t first_sequence;
    uint64_t last_sequence;
    uint64_t first_timestamp_ns;
    uint64_t last_timestamp_ns;
} stream_window_t;
```

## Debounce

Count based:

```c
s->debounce(s, 3, value_equal);
```

emits a value once after three consecutive equal observations, then suppresses
that same stable value until the state changes and another value stabilizes.

Time based:

```c
s->debounce_ms(s, 80, value_equal);
```

uses `stream_item.timestamp_ns`, so it is appropriate for variable FPS/event
rates.

## Zero-copy / borrowed input

Container adapters may return borrowed element pointers. `filter` and terminal
consumption can therefore be zero-copy. `map` never writes into borrowed
storage; generated values alternate between stream-owned scratch buffers.

## Stateful operator memory

No heap allocation is used by the pipeline. Direct `STREAM_OF` values,
`window`, and `debounce*` share a fixed per-stream state arena:

```c
#define STREAM_MAX_STATE_SIZE 8192
```

Exhaustion sets `STREAM_ERR_STATE_FULL`.

## Important semantic split

```text
Container Stream                    Live Stream
----------------                    -----------
finite snapshot-like traversal      temporal producer
independent cursor                  queue/source state
mutation -> STREAM_MODIFIED         producer mutation is expected
empty -> STREAM_END                 empty/open -> STREAM_AGAIN
reset usually supported             reset usually unsupported
```

This split keeps collection/range semantics and real-time stream semantics
clear while sharing the exact same mutable pipeline engine.
