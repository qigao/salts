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
    ->skip(&pipeline, 1u)
    ->take(&pipeline, 2u);
```

`skip` and `take` are CFlow Graph operations, not TurboSTL container
algorithms. They count values at their exact pipeline position and preserve
encounter order. Every evaluation creates independent counters. `take(0)`
performs no Range/Source resume; reaching a positive limit stops unneeded
upstream work as normal completion rather than user cancellation. Interpreted
collection supports managed values with the same COPY/MOVE/DESTROY ownership
rules as other CFlow nodes. Direct compiled plans currently reject slice
nodes explicitly.

`turbostl_stream_count(&pipeline)` is the counting terminal:

```c
turbostl_count_result result = turbostl_stream_count(&pipeline);
if (!result.ok) {
    /* result.error is a borrowed diagnostic. */
}
```

It executes the complete interpreted Graph and counts values after every
preceding operator. Empty input succeeds with zero. It does not short-circuit,
so an unbounded source needs an upstream bound such as `take`. Each call owns a
fresh accumulator; repeated evaluation still requires the bound Range to be
`REUSABLE`. Terminal callbacks only borrow managed values and add no
count-specific COPY/MOVE/DESTROY operation. Failure, including Range mutation,
callback/runtime error, or count overflow, returns `ok == false` and
`count == 0`; `error` remains borrowed from CFlow. No global `count(...)` macro
is defined because it would intercept C++ algorithms such as `std::count(...)`.

Captured predicates use `lambda(filter, ...)` or `cmeta_bindable(filter, ...)`
and enter the same Graph as immutable callable values. Filtering should not be
reimplemented separately for `Vec`, `List`, `Set`, or other container kinds.

New convenience operations follow the same ownership boundary:

- An element operation such as a new transform or selection rule belongs in
  CFlow as a typed operator accepting a CMeta callable.
- A positional operation such as `skip` or `take` belongs in CFlow Graph/Run;
  its immutable bound is Graph metadata and its mutable position belongs to
  one execution.
- A result operation such as counting, matching, or finding belongs in the
  terminal/collector layer, with explicit short-circuit, ownership, error, and
  capacity semantics.
- TurboSTL remains responsible for container storage and Range/collector
  adapters; it does not duplicate the CFlow algorithm for each container kind.

Names alone do not create API. A new operation is public only after its Graph or
terminal semantics, managed-value lifetime behavior, failure contract, and
tests are all present.

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

The erased form validates its descriptor at runtime; the typed form provides
the stronger compile-time output pointer check. Collection is transactional.
On failure, a generated wrapper is reset to zero. An erased raw handle releases
its storage while retaining the descriptor and type binding supplied by its
declaration or `*Of(...)` initializer, so it can be initialized again.
The core `TurboUtils::STL` target does not depend on CFlow.

### Worker-scheduler terminal

`collect_async_typed()` submits the complete pipeline to an existing CFlow
worker scheduler. TurboSTL does not create or own another thread pool. Multiple
independent handles may share that scheduler; one handle remains ordered and
its Collector is never invoked concurrently. This is pipeline-level
concurrency, not implicit per-element parallelism.

The input container, output container, and scheduler are borrowed through
execution destruction. Cleanup order is therefore execution, scheduler,
output, Stream Graph, then input. Capacity is still a hard transaction bound:
failure or cancellation aborts the output instead of publishing a partial
container.

```c
#include <turbostl/stream.h>

typed(List, AsyncIntList, int);

int main(void) {
    AsyncIntList input = {0};
    AsyncIntList output = {0};
    turbostl_stream_t pipeline = {0};
    turbostl_stream_execution_t execution = {0};
    cflow_stream_execution_snapshot snapshot = {0};
    cflow_scheduler workers = {0};
    int exit_code = 1;

    if (AsyncIntList_init(&input, 2u) != STL_OK ||
        AsyncIntList_push_back(&input, 10) != STL_OK ||
        AsyncIntList_push_back(&input, 20) != STL_OK ||
        stream(&input, &pipeline) == NULL ||
        !cflow_scheduler_worker_init(&workers, 2u))
        goto cleanup;

    if (collect_async_typed(&execution, &pipeline, &workers,
                            AsyncIntList, &output, 2u) !=
            CFLOW_STREAM_EXECUTION_OK ||
        cflow_stream_execution_wait(&execution) !=
            CFLOW_STREAM_EXECUTION_OK ||
        !cflow_stream_execution_get_snapshot(&execution, &snapshot) ||
        snapshot.state != CFLOW_STREAM_EXECUTION_COMPLETED)
        goto cleanup;
    exit_code = 0;

cleanup:
    (void)cflow_stream_execution_destroy(&execution);
    if (cflow_scheduler_valid(&workers))
        cflow_scheduler_destroy(&workers);
    AsyncIntList_destroy(&output);
    turbostl_stream_destroy(&pipeline);
    AsyncIntList_destroy(&input);
    return exit_code;
}
```
