# CSTL CMeta conformance

CSTL typed containers are first-class CMeta types. Reflection consumers must not
special-case CSTL storage or infer semantics from erased handles.

## Invariants

1. Every `typed(...)` CSTL container exposes canonical CMeta type metadata.
2. Containers with a well-defined data semantic expose canonical CMeta data
   metadata and provider operations.
3. Native type identity and semantic data identity remain separate.
4. Raw erased handles never manufacture missing semantic metadata.
5. Access is capability-driven. Contiguous storage is an optimization, not the
   collection ABI.
6. Providers are bounded and borrowed unless an explicit ownership/mutation
   contract says otherwise.

## Semantic families

| CSTL family | CMeta data semantic | Required access |
| --- | --- | --- |
| Vec | SEQUENCE | foreach; contiguous read fast path |
| Deque | SEQUENCE | foreach/random access |
| List | SEQUENCE | foreach |
| Stack | SEQUENCE, subject to public-order contract | foreach |
| Queue | SEQUENCE, subject to public-order contract | foreach |
| Set | SET | foreach; ordered/sorted/unique capabilities |
| HashSet | SET | foreach; unique capability |
| Map | MAP | key/value foreach; borrowed cursor |
| HashMap | MAP | key/value foreach; borrowed cursor |
| BTree | MAP | key/value foreach; borrowed cursor; ordered/sorted capability |
| BPlusTree | MAP | key/value foreach; borrowed cursor; ordered/sorted capability |
| Heap | explicit review | do not force SEQUENCE |
| MultiMap | MAP | repeated-key foreach; borrowed cursor; ordered/sorted capability |

Maps must use a dedicated key/value provider contract. They must not be
represented as `sequence<pair<K,V>>` merely for binding convenience.
Borrowed cursors return source-owned key/value pointers and fail with
`CMETA_GEN_MUTATED` when the provider generation changes. MultiMap retains one
cursor entry per key/value pair; duplicate keys are never collapsed.

## Consumers

Jinja, Lua, QuickJS, Plugin, DataBind and future bindings consume CMeta
contracts only. None may depend on `vec_t`, CSTL iterator structs, or another
consumer's private view type.
