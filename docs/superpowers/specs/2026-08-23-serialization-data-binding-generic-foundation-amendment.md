# Serialization/Data Binding Design Amendment — Generic Type Foundation

日期：2026-08-23
状态：Approved amendment to `2026-08-23-serialization-data-binding-design.md`

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

## 3. TurboSTL becomes the first real generic consumer

TurboSTL already stores concrete type bindings on handles:

```text
Vec/Deque/List/Stack/Queue/Heap/Set/HashSet -> element_type
HashMap/Map/MultiMap/BTree/BPlusTree         -> key_type + value_type
```

The missing contract is constructor identity. TurboSTL must expose canonical generic constructors and let CMeta introspect a concrete instance as:

```text
Vec<int>
Set<int>
Map<int,long>
HashMap<string,User>
```

without generating a new per-`T` facade or rewriting raw algorithms.

Canonical constructor stable IDs are TurboSTL-owned, e.g.:

```text
turbostl.Vec
turbostl.Set
turbostl.Map
turbostl.HashMap
```

The complete supported list and exact arities are defined by the implementation plan.

## 4. One versioned container extension root

`cmeta_container_desc` must not grow a new top-level field for every later feature. Append one extension pointer:

```c
const cmeta_container_ext *ext;
```

The extension object starts versioned and initially exposes generic type metadata. Later container construction support (`bind_types`, prepare/capacity/move-commit if required) extends this versioned object rather than changing `cmeta_container_desc` again.

This supersedes the earlier idea of directly appending `construct` to `cmeta_container_desc`.

## 5. Semantic containers derive from generic identity

After generic applications are proven, semantic binding can classify them without duplicating generic arguments:

```text
sequence-like constructors -> SEQUENCE semantic shape
Set/HashSet                -> SET semantic shape
Map/HashMap/BTree/...      -> MAP semantic shape
```

A semantic descriptor must not become a second source of truth for `T`, `K`, or `V` when the validated type application already owns those arguments.

`MultiMap<K,V>` requires an explicit semantic decision rather than silently pretending to be an ordinary single-value MAP.

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

## 7. Corrected implementation order

```text
0. TurboParser natural TurboSTL migration — already completed by turbo-parser PR #1
1. CMeta generic application contract
2. CMeta container versioned extension root
3. TurboSTL generic constructor + argument introspection
4. regenerate CMeta semantic data descriptor plan
5. CMeta semantic data descriptors (SEQUENCE/SET/MAP/VARIANT; OPTION only after value-generic identity)
6. container construction extension
7. CSerde canonical token protocol
8. CBind
9. TurboParser format adapters / DataBind / TBE migration
```

Steps 1–3 are specified by `docs/superpowers/plans/2026-08-23-cmeta-generic-type-applications.md`.
