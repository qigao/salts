# Serialization/Data Binding Design Amendment — Semantic Foundation After Genericization

日期：2026-08-23
状态：Follow-on amendment to `2026-08-23-serialization-data-binding-design.md`

本 amendment 建立在已合并的 CMeta generic foundation 上：

- `#36` 已完成 `TYPE<A...>` application well-formedness 与跨 TU structural identity；
- `#37` 已完成 versioned `cmeta_container_ext` root 与 TurboSTL canonical generic constructors / runtime argument introspection；
- 当前 `master` merge commit 为 `93e68ef443b86aa887cff21d2ceeb134ad32e0e4`。

本文件 supersede 原设计中 semantic model 的 container/optional 实现细节；TurboUtils/TurboParser ownership boundary、CSerde/CBind 分层和 DataBind/TBE 迁移方向不变。

## 1. Semantic shape 必须消费已经证明的 TYPE application

现在 TurboSTL typed object 已能提供：

```text
constructor + arity + T/K/V type descriptors
            |
            v
cmeta_container_type_application_valid(object)
```

因此 semantic layer 不再维护：

```text
sequence.element
set.element
map.key
map.value
```

作为第二份类型事实。

正确关系是：

```text
TYPE application owns T/K/V
semantic projection owns only meaning/category
```

例如：

```text
Vec<int>
  TYPE source: turbostl.Vec + int
  semantic:    SEQUENCE

Set<User>
  TYPE source: turbostl.Set + User
  semantic:    SET

Map<string,User>
  TYPE source: turbostl.Map + string + User
  semantic:    MAP
```

需要 T/K/V 时继续调用既有：

```c
cmeta_container_type_constructor(object)
cmeta_container_type_arity(object)
cmeta_container_type_argument(object, index)
cmeta_container_type_application_valid(object)
```

semantic API 不提供一组平行的 element/key/value argument accessors。

## 2. `cmeta_container_ext` 的 append-only ABI 必须先修正

`#37` 引入：

```c
typedef struct cmeta_container_ext {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_container_type_ops *type;
} cmeta_container_ext;
```

设计目标是以后 append semantic / construction capability，而不是继续扩大 `cmeta_container_desc`。

但当前 validator 使用：

```c
ext->struct_size < sizeof(*ext)
ops->struct_size < sizeof(*ops)
```

这会导致 header 一旦 append 新字段，旧 prefix-sized v1 object 被新 consumer 判 invalid，违背 `struct_size` 的 append-only 目的。

在第一次 append 之前必须改成 prefix-size validation：

```text
base extension accessor:
  require bytes through ext.type only

type-ops accessor:
  require bytes through ops.argument only

future semantic accessor:
  separately require bytes through ext.data

future construct accessor:
  separately require bytes through ext.construct
```

`abi_version` 仍表示 incompatible layout/semantic break；append optional tail field 不自动 bump version。

## 3. Constructor equality 成为公开的单一规则

当前 `cmeta_type_identity_equal()` 内部已有 private `cmeta_generic_desc_equal()`，按 constructor `stable_id` 做跨 TU equality。

semantic projection 需要同一规则，不能自行复制 `strcmp(stable_id)`。

因此公开：

```c
bool cmeta_generic_desc_equal(
    const cmeta_generic_desc *a,
    const cmeta_generic_desc *b);
```

`cmeta_type_identity_equal()` 继续委托该函数。Pointer equality 仍不是跨 TU constructor identity。

## 4. Semantic kinds 的 v1 集合

新的 v1 core kinds 是：

```c
typedef enum cmeta_data_kind {
    CMETA_DATA_BOOL,
    CMETA_DATA_SINT,
    CMETA_DATA_UINT,
    CMETA_DATA_FLOAT,
    CMETA_DATA_STRING,
    CMETA_DATA_BYTES,
    CMETA_DATA_ENUM,
    CMETA_DATA_STRUCT,
    CMETA_DATA_VARIANT,
    CMETA_DATA_SEQUENCE,
    CMETA_DATA_SET,
    CMETA_DATA_MAP,
    CMETA_DATA_CUSTOM
} cmeta_data_kind;
```

相对早期设计有两个关键变化：

1. `SET` 是独立 semantic kind。Uniqueness 是数据语义，不能因为 JSON 最终使用 array 就退化为 SEQUENCE。
2. `OPTIONAL` 暂不进入 v1 core。真实 `typed(Option, Name, T)` 当前只有 C storage shape，还没有完整 `CMETA_TYPE_APPLY(Option,T)` identity。

仍然永久区分：

```text
Option<T> value type     -> CMeta semantic type（等 generic identity 完成后）
field may be absent      -> CBind/schema presence policy
present NULL             -> nullable/token policy
```

## 5. v1 `cmeta_data_desc` 只定义已经有明确 contract 的字段

第一版采用：

```c
enum {
    CMETA_DATA_DESC_ABI_VERSION = 1u
};

typedef struct cmeta_data_desc {
    size_t struct_size;
    uint32_t abi_version;
    const char *stable_id;
    const char *display_name;
    cmeta_data_kind kind;
    const cmeta_type_desc *storage_type;
    const void *shape;
} cmeta_data_desc;
```

其中：

- `stable_id` 是 semantic descriptor identity，不是 schema fingerprint；
- `storage_type` 描述已有 C storage type，abstract/container-category descriptor 可以为 NULL；
- `shape` 指向 immutable kind-specific descriptor；
- v1 不提前放一个没有签名/生命周期语义的 `cmeta_data_ops *` 占位字段。

`cmeta_data_ops` 等到 CBind/access/lifecycle contract 真正设计时，通过 versioned descriptor tail 或明确的 shape contract 增加。不要先制造一个“以后会用”的 callback 框。

同理，semantic fingerprint 算法仍是必要设计，但不与第一个 descriptor ABI PR 混在一起；先锁定 descriptor graph，再定义 fingerprint domain/version。

## 6. Kind-specific immutable shapes

v1 foundation 至少包含：

```c
typedef struct cmeta_data_integer_shape {
    uint8_t bits;
} cmeta_data_integer_shape;

typedef struct cmeta_data_float_shape {
    uint8_t bits;
} cmeta_data_float_shape;

typedef enum cmeta_data_buffer_ownership {
    CMETA_DATA_BUFFER_OWNED,
    CMETA_DATA_BUFFER_BORROWED,
    CMETA_DATA_BUFFER_CUSTOM
} cmeta_data_buffer_ownership;

typedef struct cmeta_data_buffer_shape {
    cmeta_data_buffer_ownership ownership;
} cmeta_data_buffer_shape;

typedef struct cmeta_data_enum_shape {
    const cmeta_enum_desc *meta;
} cmeta_data_enum_shape;
```

整数 signedness 已由 `CMETA_DATA_SINT` / `CMETA_DATA_UINT` 表达，不在 shape 中重复一个 `is_signed` bit。

Struct：

```c
typedef struct cmeta_data_field_desc {
    const char *stable_id;
    const char *name;
    size_t offset;
    const cmeta_data_desc *value;
} cmeta_data_field_desc;

typedef struct cmeta_data_struct_shape {
    const cmeta_struct_desc *layout;
    const cmeta_data_field_desc *fields;
    size_t field_count;
} cmeta_data_struct_shape;
```

`external_name / aliases / required / default / emit` 不进入这个 shape，继续属于 CBind/schema policy。

Variant：

```c
typedef struct cmeta_data_variant_case {
    int64_t tag;
    const char *stable_id;
    const char *name;
    size_t offset;
    const cmeta_data_desc *value;
} cmeta_data_variant_case;

typedef struct cmeta_data_variant_shape {
    size_t tag_offset;
    const cmeta_data_desc *tag;
    const cmeta_data_variant_case *cases;
    size_t case_count;
} cmeta_data_variant_shape;
```

v1 descriptor layer只描述 immutable semantic structure 与 lookup/validation；variant active-storage lifecycle/commit 由后续 binding/lifecycle contract 处理。

## 7. Container semantic projection 通过 extension 关联，不复制 constructor 或 T/K/V

在完成 prefix-safe ABI 后，append：

```c
struct cmeta_data_desc;

typedef struct cmeta_container_ext {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_container_type_ops *type;
    const struct cmeta_data_desc *data;
} cmeta_container_ext;
```

`data` 是 semantic category descriptor；`type` 仍是唯一 generic constructor + argument source。

CMeta 提供三个共享 immutable category descriptors：

```c
extern const cmeta_data_desc cmeta_data_sequence;
extern const cmeta_data_desc cmeta_data_set;
extern const cmeta_data_desc cmeta_data_map;
```

以及：

```c
const cmeta_data_desc *cmeta_container_data_descriptor(const void *object);
```

该 helper 必须先要求：

```c
cmeta_container_type_application_valid(object) == true
```

再读取 extension 的 `data` tail。这样 raw byte container 即使拥有 canonical container descriptor，也不会被错误投影为 semantic container。

实际 T/K/V 仍只来自：

```c
cmeta_container_type_argument(object, index)
```

## 8. TurboSTL v1 semantic mapping

v1 明确映射：

```text
Vec<T>        -> SEQUENCE
Deque<T>      -> SEQUENCE
List<T>       -> SEQUENCE
Stack<T>      -> SEQUENCE
Queue<T>      -> SEQUENCE

Set<T>        -> SET
HashSet<T>    -> SET

HashMap<K,V>  -> MAP
Map<K,V>      -> MAP
BTree<K,V>    -> MAP
BPlusTree<K,V>-> MAP
```

两个 constructor 明确保持 unresolved：

```text
Heap<T>       -> unresolved in v1
MultiMap<K,V> -> unresolved in v1
```

原因：

- Heap 的 iteration/storage order 不是稳定的 sequence semantic order，不能因为 wire 可以写 array 就声明为 SEQUENCE；
- MultiMap 允许同 key 多 value，不能静默伪装成 ordinary MAP。

以后如果需要 BAG/MULTISET/PRIORITY_QUEUE/MULTIMAP semantic kind，应显式增加，而不是损失语义。

## 9. Nested erased container field 的边界

`#37` 证明的是一个已经带 runtime type binding 的 container object，例如：

```c
Vec(int, values);
```

而普通 struct field：

```c
struct X {
    vec_t values;
};
```

在 zero state 下没有 `element_type`，因此不能凭 C storage spelling 推断 `Vec<int>`。

本 semantic-foundation plan 不通过重新增加 `sequence.element = Int` 来掩盖这个事实。

正确的后续 construction/type-binding plan 必须建立：

```text
static field type application contract
        -> construct.bind_types(empty_object, args...)
        -> object becomes valid Vec<T>/Map<K,V>
        -> Collector builds values transactionally
```

也就是说 semantic category 与 generic application 分开，但 concrete field application 必须仍然是 CMeta TYPE metadata，而不是 CBind 猜类型或 semantic shape 重复参数。

在 construction plan 完成前，本 amendment 只承诺：

```text
semantic introspection of already-typed container instances
```

不宣称已解决 nested zero-field decode。

## 10. Corrected implementation order after #37

```text
#36  TYPE<A...> contract                         DONE
#37  container extension root + TurboSTL generic DONE

C0   generic equality + append-only ext ABI hardening
C1   CMeta semantic descriptor core
C2   TurboSTL semantic projection through ext.data

next container construction + static field type application
next CSerde canonical token protocol
next CBind
next TurboParser adapters / DataBind / TBE migration
```

新的 implementation plan：

`docs/superpowers/plans/2026-08-23-cmeta-semantic-data-descriptors.md`
