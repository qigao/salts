# TurboSTL

TurboSTL is TurboUtils' C11 standard-library-style collection module. It owns
the concrete data-structure algorithms, while CMeta supplies finite typed
facades and metadata around those raw algorithms.

Link the installed CMake target and include the typed header (the aggregate
header includes it as well):

```cmake
target_link_libraries(my_target PRIVATE TurboUtils::STL)
```

```c
#include <turbostl/typed.h>
```

## One declaration is enough

For a single typed container:

```c
typed(List, IntList, int);
```

For several:

```c
typed(Vec, IntVec, int);
typed(List, IntList, int);
typed(HashSet, IntSet, int);
typed(HashMap, IntLongHashMap, int, long);
typed(Map, IntLongMap, int, long);
typed(BTree, IntTree, int, long);
```

No `implement(...)`, `DeclareContainers(...)`, or `ImplementContainers(...)` call is required or exposed for typed containers.

## Self-describing raw handles

PR #53's declaration and expression initializers remain supported:

```c
Vec(int, declared_values);
Map(int, long, declared_scores);

vec_t values = VecOf(int);
map_t scores = MapOf(int, long);
turbostl_stack_t pending = StackOf(int);
```

`turbostl_stack_t` is the canonical raw Stack handle. The historical
`stack_t` alias remains available outside Darwin unless
`TURBOSTL_NO_LEGACY_STACK_T` is defined. Darwin reserves `stack_t` for its
signal-stack API, so portable code must use `turbostl_stack_t` or the
`Stack(T, name)` declaration facade.

These forms bind CMeta descriptors to erased TurboSTL handles without
allocating storage or generating a concrete C type. They are raw-handle
initializers, not additional CMeta Generic instantiations. Initialize them with
the ordinary raw operations and destroy them after use:

```c
if (vec_init(&values, 8u) != STL_OK)
    return 1;
vec_destroy(&values);
```

The `*Of(...)` compound literals also remain valid in assignments, returns,
and function arguments. Once an initialized raw handle owns storage, it must
not be copied by value.

## What is header-only?

Only the generated typed layer:

```text
IntList wrapper type
IntList_* thin forwarding functions
container descriptor
Range factories / traits
```

The forwarding functions are `static inline`, and descriptors are header-local metadata. The actual algorithms remain compiled C in TurboSTL sources:

```text
vector growth
list allocation/linking
hash probing
heap operations
B-tree/B+tree balancing
```

So "header-only typed facade" does not mean "header-only container algorithms".

## Supported kinds

```text
Vec        Deque      List
Stack      Queue      Heap
Set        HashSet
HashMap    Map        MultiMap
BTree      BPlusTree
```

Ordered kinds obtain comparison from the registered CMeta type descriptor;
their typed declarations do not take a parallel comparator token. `Map` is an
ordered red-black-tree map and requires key `COMPARE`. `HashMap` is an independent
open-addressed hash table and requires key `HASH` and `EQUAL`. `List` is an
independent node-based doubly-linked list with stable iterators across insertion.

## Typed operations

Generated operations keep the concrete Generic type visible in the function
name:

```c
IntList values = {0};
IntLongMap index = {0};

IntList_init(&values, 100u);
IntList_push_back(&values, 10);
IntList_push_back(&values, 20);

IntLongMap_init(&index, 100u);
IntLongMap_put(&index, 7, 70L);

IntLongMap_destroy(&index);
IntList_destroy(&values);
```

These generated `Type_method` names are the complete typed ABI. Raw names such
as `map_init(&scores, 100u)`, `map_put(&scores, &key, &value)`, and
`map_destroy(&scores)` remain ordinary functions; typed declarations never
intercept or reinterpret their C expressions.

## CFlow bridge

Link `TurboUtils::STLStream` for the optional modern container-operation facade.
Its purpose is to make filtering, transformation, aggregation, and typed
collection convenient for TurboSTL containers without introducing another
container implementation. Familiar fluent names improve readability, but no
external Stream API defines this facade's lifecycle, reuse, terminal, ordering,
or parallel-execution semantics.

The facade is a reusable typed CFlow Graph bound to a borrowed container Range,
not a single-use traversal object. Operators append nodes to that Graph;
evaluation creates fresh runtime and cursor state. A `REUSABLE` Range may be
evaluated again while its container remains alive and unmodified. `reduce` is a
Graph stage and collection remains an explicit terminal. Capacity arguments are
hard resource bounds: exceeding one fails the collection transaction instead of
silently truncating the result.

`TurboUtils::STLStream` is a compiled bridge target. Its `stream(...)` helpers
bind the container Range and explicitly inject TurboSTL's bounded HashSet and
Vec state backends; the core `TurboUtils::STL` target still has no CFlow
dependency.

Element predicates are already represented by CMeta callables. The callable
owns the typed signature and declared contract; CFlow owns traversal, predicate
invocation, value lifetime, and Graph execution. For example:

```c
typed(filter, value, bool, keep_even, (int value)) {
    return value % 2 == 0;
}

cflow_stream pipeline = {0};
stream(&values, &pipeline)
    ->filter(&pipeline, keep_even)
    ->distinct(&pipeline, 64u)
    ->sorted(&pipeline, 64u)
    ->skip(&pipeline, 1u)
    ->take(&pipeline, 10u);
```

Captured predicates use `lambda(filter, ...)` or `cmeta_bindable(filter, ...)`
and enter the same Graph as immutable callable values. Filtering should not be
reimplemented separately for `Vec`, `List`, `Set`, or other container kinds.

`skip(n)` and `take(n)` are positional CFlow Graph nodes rather than terminal
capacity options. They count values at their exact pipeline position and keep
encounter order. `take(n)` short-circuits upstream after its bound;
`take(0)` does not pull the source. Every evaluation owns fresh counters, so a
reusable unchanged Range can execute the same sliced Graph again. Interpreted
collection supports managed element traits. Direct compilation currently
accepts slice-only trivial-value graphs; mixing a slice with callable nodes
fails explicitly until the plan executor can preserve lazy short-circuit and
error semantics.

`distinct(max_unique)` keeps first-encounter order. The nonzero argument limits
unique retained values, not source items. A duplicate is filtered without
growing state; the next new value after the bound returns
`CFLOW_STATUS_CAPACITY_EXCEEDED`. Element descriptors must provide `HASH` and
`EQUAL`; managed elements are copied into the HashSet and destroyed when the
Run closes. Direct/compiled plans reject this stateful operator explicitly.

`sorted(max_elements)` uses a bounded TurboSTL Vec plus the stable
`O(n log n)` sort implementation. It buffers the complete upstream, requires
`COMPARE` plus lifecycle traits, preserves encounter order among equal values,
and emits no partial result if the hard element bound is exceeded. Managed
elements remain independently owned throughout collection and Run teardown.
Direct/compiled plans reject this materializing operator explicitly.

New convenience operations follow the same ownership boundary:

- An element operation such as a new transform or selection rule belongs in
  CFlow as a typed operator accepting a CMeta callable.
- A result operation such as counting, matching, or finding belongs in the
  terminal/collector layer, with explicit short-circuit, ownership, error, and
  capacity semantics.
- TurboSTL remains responsible for container storage and Range/collector
  adapters; it does not duplicate the CFlow algorithm for each container kind.

Names alone do not create API. A new operation is public only after its Graph or
terminal semantics, managed-value lifetime behavior, failure contract, and
tests are all present.

The common result operations are available through prefixed helpers so they do
not collide with generated container methods:

```c
size_t selected = 0u;
bool has_even = false;
turbostl_find_result first = {0};
const char *error = NULL;

turbostl_stream_count(&pipeline, &selected, &error);
turbostl_stream_any_match(&pipeline, keep_even, &has_even, &error);
turbostl_stream_find_first(&pipeline, &first, &error);
turbostl_find_result_destroy(&first);
```

Structured variants such as `turbostl_stream_count_result` and
`turbostl_stream_any_match_result` return the shared allocation-free
`turbostl_status_result`. `turbostl_status_result_message` returns canonical
library-owned static text; existing `bool + out_error` helpers remain available
for detailed compatibility diagnostics.

`turbostl_stream_all_match` and `turbostl_stream_for_each` complete the same
terminal family. Empty input produces count zero, any false, all true, no first
value, and no visitor calls. Matching and finding short-circuit the current
execution; count and visiting require source completion. `find_first` owns an
independent copy, including for managed element types. Predicate values retain
their CMeta typed-callable validation, while a `for_each` visitor borrows each
element only for its callback duration.

Sequence/set wrappers derive Range information in the same declaration, so
CFlow can bind an initialized typed object directly:

```c
cflow_stream s;
stream(&values, &s);
```

Associative types use explicit descriptor views:

```c
stream_keys(&map, &s);
stream_values(&map, &s);
stream_entries(&map, &s);
```

Collection names its output type and retains an explicit capacity bound:

```c
IntList output = {0};
turbostl_collect_result result =
    to_list_typed(&s, IntList, &output, 100u);
```

`collect_typed(&s, Type, &output, limit)` has the same typed terminal contract.
The generated public collector takes `Type *`, so a mismatched output wrapper
is diagnosed before CFlow receives its erased collector context.

Self-describing raw outputs retain the three-argument terminal:

```c
list_t raw_output = ListOf(int);
turbostl_collect_result raw_result = to_list(&s, &raw_output, 100u);
```

Trivial-copy streams can instead return an owned bounded byte result:

```c
cflow_result bytes = {0};
turbostl_status_result byte_status =
    to_array_result(&s, 100u, &bytes);
if (turbostl_status_result_is_ok(byte_status))
    cflow_result_destroy(&bytes);
```

`to_array_result()` distinguishes capacity, unsupported lifecycle, allocation,
source admission, and Runtime failure. The legacy `to_array()` remains its
`bool` compatibility projection. Both clear the result on failure.

The erased form validates its descriptor at runtime; the typed form provides
the stronger compile-time output pointer check. Collection is transactional.
On failure, a generated wrapper is reset to zero. An erased raw handle releases
its storage while retaining the descriptor and type binding supplied by its
declaration or `*Of(...)` initializer, so it can be initialized again.
`turbostl_collect_result.flow_status` classifies Range admission and CFlow
Runtime failures; `status` remains the exact CMeta Collector status, and
`count` remains the accepted transaction count. For example, a bounded
`sorted()` Runtime overflow reports `CFLOW_STATUS_CAPACITY_EXCEEDED` together
with the Collector's abort status, while output-collector overflow reports
`CMETA_CAPACITY_EXCEEDED` directly. `error` remains a borrowed diagnostic and
must not be freed or retained beyond its documented source lifetime.
The core `TurboUtils::STL` target does not depend on CFlow.
