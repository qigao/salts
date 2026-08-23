# Serialization/Data Binding Design Amendment — Generic Type Foundation

日期：2026-08-23
状态：Approved amendment to `2026-08-23-serialization-data-binding-design.md`; implementation foundation completed by `#36` and `#37`.

## 1. Why this amendment exists

Before CMeta can say that a value has semantic shape `SEQUENCE<T>`, `SET<T>`, `MAP<K,V>` or `OPTION<T>`, the underlying generic type application must first be a valid CMeta type.

Therefore the implementation order is corrected from:

```text
semantic descriptor -> container binding
```

to:

```text
TYPE<A...> well-formedness
    -> real generic consumers (TurboSTL)
    -> semantic shape resolution
    -> CBind
    -> CSerde/format adapters
```

This amendment does not change the approved TurboUtils/TurboParser ownership boundary.

## 2. CMeta generic application is foundational

CMeta already has:

```text
cmeta_generic_desc
cmeta_type_identity
CMETA_TYPE_APPLY
constructor + args + arity
```

The canonical invariant is now explicit:

```text
TYPE<A...> is well formed iff
  constructor is valid
  arity is accepted by the constructor
  every argument identity exists
  every argument identity is recursively well formed
```

Pointer equality is not type identity. Constructor `stable_id` plus recursive argument identity defines cross-TU equality.

Type well-formedness is intentionally separate from operation capability. For example, `Set<T>` can be a valid type application while a concrete `set_init()` rejects `T` because comparison traits are missing.

This contract was implemented by `#36`.

## 3. TurboSTL becomes the first real generic consumer

TurboSTL already stores concrete type bindings on handles:

```text
Vec/Deque/List/Stack/Queue/Heap/Set/HashSet -> element_type
HashMap/Map/MultiMap/BTree/BPlusTree         -> key_type + value_type
```

The missing contract was constructor identity. TurboSTL now exposes canonical generic constructors and lets CMeta introspect a concrete instance as:

```text
Vec<int>
Set<int>
Map<int,long>
HashMap<string,User>
```

without generating a new per-`T` facade or rewriting raw algorithms.

Canonical constructor stable IDs are TurboSTL-owned, including:

```text
turbostl.Vec
turbostl.Set
turbostl.Map
turbostl.HashMap
```

The complete supported list and exact arities were implemented by `#37`.

## 4. One versioned container extension root

`cmeta_container_desc` must not grow a new top-level field for every later feature. `#37` appended exactly one extension pointer:

```c
const cmeta_container_ext *ext;
```

The extension object is versioned and initially exposes generic type metadata. Later semantic and construction support extend this versioned object rather than changing `cmeta_container_desc` again.

This supersedes the earlier idea of directly appending `construct` to `cmeta_container_desc`.

The follow-on semantic amendment additionally requires true prefix-size validation before the first optional tail field is appended; see `2026-08-23-serialization-data-binding-semantic-foundation-amendment.md`.

## 5. Semantic containers derive from generic identity

After generic applications are proven, semantic binding can classify them without duplicating generic arguments:

```text
sequence-like constructors -> SEQUENCE semantic shape
Set/HashSet                -> SET semantic shape
Map/HashMap/BTree/...      -> MAP semantic shape
```

A semantic descriptor must not become a second source of truth for `T`, `K`, or `V` when the validated type application already owns those arguments.

`MultiMap<K,V>` requires an explicit semantic decision rather than silently pretending to be an ordinary single-value MAP.

The exact post-`#37` mapping, including Heap/MultiMap non-mapping, is defined by the semantic-foundation amendment and its implementation plan.

## 6. Option<T> is not field optionality

CMeta already has a real storage form created by:

```c
typed(Option, Name, T)
```

but that generated value currently lacks a complete `CMETA_TYPE_APPLY(Option,T)` identity. Therefore semantic OPTIONAL is not deleted permanently; it is **blocked until the value-generic metadata itself is real**.

The following remain separate forever:

```text
Option<T> value type     -> CMeta semantic type, once generic identity exists
field may be absent      -> CBind/schema field-presence policy
field present with NULL  -> nullable/token policy
```

Missing, null, and present-value are distinct states and must not be collapsed into one `OPTIONAL` flag.

## 7. Current implementation order

Completed:

```text
0. TurboParser natural TurboSTL migration — turbo-parser PR #1
1. CMeta generic application contract       — turbo-utils #36
2. CMeta container versioned extension root — turbo-utils #37
3. TurboSTL generic constructor/introspection — turbo-utils #37
```

Current work:

```text
4. CMeta semantic data descriptor foundation
5. TurboSTL semantic projection from proven generic applications
```

Then:

```text
6. container construction + static nested-field type application
7. CSerde canonical token protocol
8. CBind
9. TurboParser format adapters / DataBind / TBE migration
```

Historical implementation plan for steps 1–3:

`docs/superpowers/plans/2026-08-23-cmeta-generic-type-applications.md`

Current implementation plan:

`docs/superpowers/plans/2026-08-23-cmeta-semantic-data-descriptors.md`
