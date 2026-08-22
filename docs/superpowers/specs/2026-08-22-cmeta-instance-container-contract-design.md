# CMeta Instance Container Contract Design

## Status and scope

This design refines the approved TurboSTL self-describing natural API after migration of the generated `IntVec`/`IntMap` public surface exposed a missing runtime contract: generated associative wrappers previously carried Range/Collector entry metadata, while ordinary `vec_t`, `map_t`, `btree_t`, and related handles do not yet provide equivalent instance-level CMeta semantics.

This design covers only the metadata/lifetime contract required to remove generated TurboSTL wrapper types safely. It does not change container algorithms, CFlow execution semantics, or application-type registration.

The base dependency remains:

```text
TurboSTL -> CMeta
```

CMeta must not gain a TurboSTL dependency.

## Goals

1. Every one of the 13 TurboSTL container kinds has a real CMeta container descriptor on its ordinary runtime handle.
2. `Vec(T,name)`, `List(T,name)`, `Map(K,V,name)`, and the other declaration DSLs remain the only typed user declaration model.
3. Users never need generated types such as `IntVec`, `OrderedIntMap`, `OwnedEntryMap_entry`, or generated `Type_method` functions.
4. Sequence, set, and associative Range/Collector behavior previously covered by generated wrappers is preserved through instance metadata.
5. Associative entries have one non-generated runtime value representation with safe borrowed/owned lifetime semantics.
6. Hash and ordered associative entry descriptors advertise only capabilities that are actually guaranteed by values admitted to their views.
7. Existing raw byte APIs and container algorithms remain unchanged.

## Non-goals

- No compiler-specific `typeof` or C++ template emulation.
- No new application-type registration mechanism in this task.
- No generated hidden `MapEntry<K,V>` C type under a different name.
- No change to the meaning of `cmeta_range`, `cmeta_collector`, container capacity, ordering, or mutation detection.
- No CFlow trait-aware transport rewrite in this task.
- No structural `Entry<K,V>` CMeta type-identity extension in this task.

## Kind-level descriptor model

Each ordinary TurboSTL handle carries a `cmeta_container_header` whose descriptor represents the container **kind**, not a generated concrete type.

Examples:

```c
Vec(int, values);
List(User, users);
Map(int, User, by_id);
BTree(int, User, index);
```

bind the equivalent of:

```text
values.cmeta.descriptor -> stl_vec_container_desc
users.cmeta.descriptor  -> stl_list_container_desc
by_id.cmeta.descriptor  -> stl_map_container_desc
index.cmeta.descriptor  -> stl_btree_container_desc
```

The descriptors stored in `cmeta_container_desc.element_type`, `.key_type`, and `.value_type` remain `NULL` for open-ended runtime kinds. Concrete type descriptors come from the instance handle and are exposed by Range/Collector factories.

This avoids generating a new container descriptor for every application type while preserving kind identity.

### All 13 kinds

The contract applies to:

```text
Vec
Deque
List
Stack
Queue
Heap
Set
HashSet
HashMap
Map
MultiMap
BTree
BPlusTree
```

`Stack` and `Queue` currently wrap `vec_t`/`deque_t`. They must carry their own `cmeta_container_header` rather than accidentally presenting the embedded Vec/Deque descriptor when erased through CMeta. Their internal raw storage still delegates to Vec/Deque.

## Declaration DSL contract

Declaration macros bind both the kind descriptor and concrete type descriptors without allocation.

Conceptually:

```c
#define Vec(T, name) \
    vec_t name = { \
        .cmeta = { &stl_vec_container_desc }, \
        .element_type = CMETA_TYPEOF(T) \
    }

#define Map(K, V, name) \
    map_t name = { \
        .cmeta = { &stl_map_container_desc }, \
        .key_type = CMETA_TYPEOF(K), \
        .value_type = CMETA_TYPEOF(V) \
    }
```

The exact initializer spelling may differ by layout, but these invariants are mandatory:

- declaration performs no allocation;
- the kind descriptor is available before runtime initialization;
- concrete type bindings survive `destroy()`;
- `*_init()` validates the bindings rather than receiving type tokens from the user.

## Range view contract

### One-type containers

Sequence and set-like containers expose their element type from the instance.

`CMETA_CONTAINER_VIEW_DEFAULT` returns the natural element Range.

The Range preserves the existing kind-specific flags. Examples:

- Vec: sized, ordered, contiguous, random-access, reusable;
- List: sized, ordered, reusable;
- Set: sized, ordered, sorted, unique, reusable;
- HashSet: sized, unique, reusable;
- Heap: the existing heap iteration guarantees only; it must not advertise sorted order merely because heap operations expose a minimum/maximum element.

Mutation detection continues to use the container generation/version counter.

### Associative containers

Associative containers expose all three explicit views:

```text
CMETA_CONTAINER_VIEW_KEYS
CMETA_CONTAINER_VIEW_VALUES
CMETA_CONTAINER_VIEW_ENTRIES
```

and restore the generated-wrapper default:

```text
CMETA_CONTAINER_VIEW_DEFAULT -> entries
```

This applies to:

```text
HashMap
Map
MultiMap
BTree
BPlusTree
```

CFlow code that wants values continues to request `CMETA_CONTAINER_VIEW_VALUES` explicitly. It must not depend on the default view.

Keys and values use the handle's bound `key_type` and `value_type` directly as `cmeta_range.element_type`.

Entries use the non-generated `cmeta_entry` representation described below.

Raw-byte associative handles with no valid CMeta key/value binding do not expose CMeta entry views or collectors. A view factory returns failure rather than inventing type/lifecycle semantics.

## `cmeta_entry`: one runtime associative entry value

CMeta owns the generic entry value because `CMETA_CONTAINER_VIEW_ENTRIES` is a CMeta abstraction and must not depend on TurboSTL.

The conceptual representation is:

```c
typedef struct cmeta_entry {
    const cmeta_type_desc *key_type;
    const cmeta_type_desc *value_type;
    const void *key;
    const void *value;

    /* NULL for borrowed entries. When non-NULL these are the allocation
     * owners/base pointers; key/value may point at aligned addresses within
     * those allocations. */
    void *key_storage;
    void *value_storage;
} cmeta_entry;
```

The implementation may add flags or allocation bookkeeping, but users must not need a generated C struct containing concrete K/V fields.

A Range writes a **borrowed** entry:

```text
entry.key_type      = map->key_type
entry.value_type    = map->value_type
entry.key           = address of key owned by container
entry.value         = address of value owned by container
entry.key_storage   = NULL
entry.value_storage = NULL
```

The borrowed pointers obey the same lifetime as the Range owner. Mutation/destroy invalidates the Range through the existing generation check.

## Entry copy/move/destroy semantics

### Copy construction

`cmeta_entry` copy construction converts either a borrowed or owned source entry into an independently owned destination entry.

The operation:

1. validates non-null key/value pointers and valid non-null `key_type`/`value_type` descriptors;
2. requires key and value `COPY | DESTROY` traits;
3. allocates correctly aligned storage for the key;
4. copy-constructs the key;
5. allocates correctly aligned storage for the value;
6. copy-constructs the value;
7. on any value allocation/copy failure, destroys the copied key and frees all partial storage;
8. leaves the destination zero/empty on failure.

CMeta must implement aligned storage without depending on Platform. A portable over-allocation/alignment helper is acceptable; plain `malloc(sizeof(T))` is insufficient for over-aligned descriptors.

### Move construction

Move construction moves the **entry wrapper ownership**, not the container's underlying key/value objects.

- if the source is owned, storage ownership and pointers transfer to the destination and the source becomes empty;
- if the source is borrowed, the borrowed pointers/type descriptors transfer to the destination and the source becomes empty;
- moving a borrowed entry must never invoke the key/value move traits and therefore must never mutate the source container.

This makes entry move total and non-failing.

### Destroy

Destroy is a no-op for borrowed key/value pointers.

For owned storage it:

1. invokes the value `DESTROY` trait when owned;
2. invokes the key `DESTROY` trait when owned;
3. releases backing allocations;
4. zeroes the entry.

Destroy is safe on a zero entry and on a moved-from entry.

## Entry semantic descriptors

A single all-powerful descriptor would falsely advertise capabilities. The same `cmeta_entry` C representation therefore has distinct static semantic descriptors.

### Hash entry

```text
cmeta_type_hash_entry
```

advertises:

```text
COPY | MOVE | DESTROY | EQUAL | HASH
```

`equal` and `hash` operate on the key through `entry.key_type->traits`. HashMap uses this descriptor for its entry Range/Collector.

HashMap may expose this entry descriptor only when its bound key/value descriptors satisfy the lifecycle requirements needed by entry copy/destroy and the key satisfies HashMap's `EQUAL | HASH` requirements. Otherwise the typed init/view fails rather than exposing a descriptor whose advertised traits are not usable for values from that instance.

### Ordered entry

```text
cmeta_type_ordered_entry
```

advertises:

```text
COPY | MOVE | DESTROY | COMPARE
```

`compare` operates on the key through `entry.key_type->traits`. Map, MultiMap, BTree, and BPlusTree use this descriptor.

These containers may expose this entry descriptor only when their bound key/value descriptors satisfy the lifecycle requirements needed by entry copy/destroy and the key satisfies the ordered container's `COMPARE` requirement.

The callbacks are null-safe and reject invalid entries rather than dereferencing missing descriptors.

The descriptors describe semantic capability, while every entry value itself carries the concrete `key_type` and `value_type`.

## Dynamic key/value type validation

Because generated type names are removed, the entry descriptor alone no longer encodes `K,V` in its C type name. Therefore collectors must validate the entry's runtime binding.

An associative collector accepts an entry only when:

```c
cmeta_type_equal(entry->key_type, output->key_type) &&
cmeta_type_equal(entry->value_type, output->value_type)
```

Otherwise it returns `CMETA_TYPE_MISMATCH` without modifying the output.

This dynamic validation is mandatory for every accepted entry, not merely at collector begin.

The collector's `input_type` remains the appropriate semantic entry descriptor (`cmeta_type_hash_entry` or `cmeta_type_ordered_entry`). Collector begin must perform both checks:

1. `cmeta_type_equal(input, expected_entry_descriptor)`; and
2. `cmeta_type_require_traits(input, required_entry_traits)`.

Therefore an equivalent copied descriptor whose `.traits` is missing is rejected with `CMETA_TRAIT_MISSING` rather than silently admitted by name/size/alignment equality.

This design intentionally chooses runtime K/V validation over generating a per-`K,V` `cmeta_type_desc`. Entry streams are therefore typed as hash-entry or ordered-entry at the CMeta Range level, with concrete K/V carried by each entry value. A future structural `Entry<K,V>` type-identity facility may refine static graph typing without changing the C representation, but it is outside this task.

## Collector contract

Every generated-wrapper collector behavior needed by TurboSTL regressions moves to the instance handle.

### One-type containers

Collector begin validates its input type against the handle's bound `element_type`, initializes the output with the requested limit, and accepts values by pointer using the natural operation (`vec_push`, `list_push_back`, `set_add`, etc.).

Abort destroys runtime storage while preserving the declaration-time binding/kind descriptor so the handle remains reusable.

### Associative containers

Collector begin validates the semantic entry descriptor required by the kind, requires its advertised semantic/lifecycle traits, and initializes the output.

Each accept:

1. receives a `cmeta_entry`;
2. validates dynamic `key_type/value_type` against the output handle;
3. forwards `entry.key` and `entry.value` to the natural `*_put()` operation.

The container copies according to its normal ownership rules before the callback returns.

Abort preserves the declaration binding just like explicit `destroy()`.

## Descriptor ownership and lifetime

Kind descriptors and the two semantic entry descriptors have static program lifetime.

Concrete element/key/value descriptors are borrowed from CMeta/application registration and are stored on the handle exactly as in the approved self-describing design.

`cmeta_entry` owned allocations belong to the entry value and are released only by its descriptor's destroy operation (or explicit equivalent helper if one is exposed).

No descriptor points at stack-local metadata.

## Public API shape

The end-user path remains:

```c
Vec(int, values);
vec_init(&values, 32u);
vec_push(&values, &number);

Map(int, long, scores);
map_init(&scores, 32u);
map_put(&scores, &id, &score);

cmeta_range entries;
if (cmeta_container_range_view(&scores,
                               CMETA_CONTAINER_VIEW_ENTRIES,
                               &entries)) {
    cmeta_entry entry = {0};
    cmeta_range_cursor cursor = {0};
    for (;;) {
        cmeta_gen_status status = cmeta_range_next(&entries, &cursor, &entry);
        if (status == CMETA_GEN_DONE)
            break;
        if (status != CMETA_GEN_VALUE && status != CMETA_GEN_VALUE_AND_DONE)
            break;

        const int *key = (const int *)entry.key;
        const long *value = (const long *)entry.value;
        /* borrowed until owner mutation/destroy */

        if (status == CMETA_GEN_VALUE_AND_DONE)
            break;
    }
}
```

There is no:

```text
IntMap
IntMap_entry
IntMap_entries_range
IntMap_collector
```

in the TurboSTL public model.

## Compatibility and migration

The migration sequence is:

1. add CMeta `cmeta_entry` plus hash/ordered descriptors and focused lifetime tests;
2. attach true kind descriptors to all 13 ordinary TurboSTL handles;
3. implement instance-level sequence/set Range and Collector adapters;
4. implement associative keys/values/entries Range adapters and entry collectors;
5. migrate representative List/Map/BTree regressions from generated wrappers;
6. migrate the composed entry ownership regression to `cmeta_entry`;
7. migrate the remaining generated-wrapper TurboSTL tests;
8. delete `legacy_generated_typed.h` and the temporary wrapper test sources;
9. continue the already-planned removal of `turbo_*` aliases/source names only after the instance metadata path is green.

Generated CMeta container infrastructure may remain in CMeta for non-TurboSTL users; TurboSTL simply stops depending on it as its public/normal path.

## Error handling and rollback

- Missing type bindings: `STL_INVALID_ARGUMENT`, no runtime mutation.
- Missing lifecycle/semantic traits required by an instance view: typed init/view is rejected rather than exposing an unusable entry descriptor.
- Entry key/value type mismatch at collector accept: `CMETA_TYPE_MISMATCH`, collector abort semantics apply.
- Missing semantic traits on the supplied collector input descriptor: `CMETA_TRAIT_MISSING`.
- Missing key/value lifecycle traits during entry copy: copy returns false and destination remains empty.
- Allocation failure during entry copy: copy returns false, all partial ownership is rolled back.
- Mutated Range owner: existing `CMETA_GEN_MUTATED` behavior remains unchanged and cursor/output are not advanced by the failed call.
- Container insertion errors propagate through the existing STL→CMeta status mapping.

## Testing contract

The implementation must preserve or add compile/runtime coverage for:

1. all 13 declaration DSLs expose a non-null kind descriptor without generated types;
2. `init -> use -> destroy -> init` retains type binding and kind descriptor;
3. sequence/set default Ranges preserve existing order/uniqueness flags;
4. HashMap exposes keys, values, and hash-entry views;
5. Map/MultiMap/BTree/BPlusTree expose ordered keys, values, and ordered-entry views;
6. default associative view equals entries;
7. a borrowed entry can be copy-constructed into an owned transient;
8. owned entry move transfers ownership without invoking container-value move operations;
9. moving a borrowed entry does not mutate the container value;
10. value-copy failure after key copy rolls back key ownership exactly once;
11. entry destroy balances key/value owned lifetimes;
12. associative collector rejects mismatched runtime K/V bindings without mutation;
13. an equivalent entry descriptor with missing traits is rejected at collector begin;
14. collector abort preserves handle declaration binding;
15. raw-byte handles without CMeta bindings do not expose invented entry views;
16. existing capacity/order/mutation/ownership regressions remain unchanged;
17. public C11 and C++17 header tests compile without generated container names;
18. final Linux and Windows CI pass on the final head.

No source-spelling/grep test is introduced.

## Interaction with CFlow

CFlow already consumes `cmeta_range.element_type`. In this task:

- explicit `VALUES` views continue to carry the concrete value descriptor;
- entry views carry `cmeta_type_hash_entry` or `cmeta_type_ordered_entry` and produce `cmeta_entry` values;
- concrete entry K/V identity is validated at runtime by consumers/collectors rather than encoded in a generated C type;
- no CFlow execution behavior is changed.

CFlow's existing byte-copy transport for non-trivial values remains a separate follow-up. This task must not silently widen into a CFlow ownership rewrite.

## Design consequences

After this work, TurboSTL has one coherent model:

```text
CMeta type descriptors / traits
            ↓
Vec(T,name) / Map(K,V,name)
            ↓
ordinary self-describing handle
            ↓
natural vec_* / map_* operations
            ↓
instance Range / Collector metadata
```

Generated typed wrappers are no longer required to recover metadata, entry semantics, or Range/Collector behavior. That removes the architectural reason for `IntVec_*` / `IntMap_*` from TurboSTL while preserving the semantic proof surface that those wrappers previously supplied.