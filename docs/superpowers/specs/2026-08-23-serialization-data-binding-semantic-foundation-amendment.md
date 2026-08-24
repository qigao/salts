# Serialization/Data Binding Design Amendment — Semantic Foundation After Genericization

日期：2026-08-23
状态：Delivered foundation amendment（API/行为已按当前 `master` 校正）

本 amendment 建立在已合并的 generic foundation 上：

- `#36` 已完成 `TYPE<A...>` application well-formedness 与跨 TU structural identity；
- `#37` 已完成 versioned `cmeta_container_ext` root 与 TurboSTL canonical generic constructors / runtime argument introspection；
- `master` 基线 merge commit 为 `93e68ef443b86aa887cff21d2ceeb134ad32e0e4`。

本文件 supersede 原设计中 semantic model 的 container/optional 实现细节。TurboUtils/TurboParser ownership boundary、CSerde/CBind 分层和 DataBind/TBE 迁移方向不变。

## 1. Semantic shape 消费已经证明的 TYPE application

TurboSTL typed object 已能提供：

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
Vec<int>          -> TYPE = turbostl.Vec<int>, semantic = SEQUENCE
Set<User>         -> TYPE = turbostl.Set<User>, semantic = SET
Map<string,User>  -> TYPE = turbostl.Map<string,User>, semantic = MAP
```

实际 T/K/V 永远继续来自：

```c
cmeta_container_type_constructor(object)
cmeta_container_type_arity(object)
cmeta_container_type_argument(object, index)
cmeta_container_type_application_valid(object)
```

semantic API 不提供平行的 element/key/value argument accessor。

`cmeta_container_data()` 本身只发现 category，不把上述两项检查合并成一个
结果。typed consumer 必须同时取得 semantic category 并验证 TYPE application；
这样 raw-byte handle 可说明自身是 sequence-like storage，却不会被误当成
`Vec<T>`。

## 2. `cmeta_container_ext` 必须先成为真正 append-only ABI

`#37` 当前定义：

```c
typedef struct cmeta_container_ext {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_container_type_ops *type;
} cmeta_container_ext;
```

目标是以后 append semantic / construction capability，而不是继续扩大 `cmeta_container_desc`。

但当前 `container_type.c` 使用：

```c
ext->struct_size < sizeof(*ext)
ops->struct_size < sizeof(*ops)
```

一旦 header append 新字段，新 consumer 就会把旧 prefix-sized v1 object 判 invalid。这与 `struct_size` 的设计目的冲突。

第一次 append 之前必须改成 field-end prefix validation：

```text
base extension accessor -> require bytes through ext.type
current type ops        -> require bytes through ops.argument
semantic accessor       -> separately require bytes through ext.data
future construct        -> separately require bytes through ext.construct
```

`abi_version` 表示 incompatible contract break；append optional tail 不自动 bump version。

semantic projection通过显式 `ext.data` link 完成，不需要再公开一套 constructor matcher；`cmeta_type_identity_equal()` 现有内部 stable-ID equality 保持为 TYPE identity 实现细节即可。

## 3. Semantic kinds v1

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

两个关键结论：

1. `SET` 是独立 semantic kind。Uniqueness 是数据语义，不能因为 JSON 可写 array 就退化成 SEQUENCE。
2. `OPTIONAL` 暂不进入 v1。`typed(Option, Name, T)` 当前只有 C storage shape，还没有完整 `CMETA_TYPE_APPLY(Option,T)` identity。

永久区分：

```text
Option<T> value type     -> CMeta semantic type（等 TYPE identity 完成后）
field may be absent      -> CBind/schema presence policy
field present with NULL  -> nullable/token policy
```

## 4. `cmeta_data_desc` v1

第一版只定义已有明确 contract 的字段：

```c
enum { CMETA_DATA_DESC_ABI_VERSION = 1u };

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

含义：

- `stable_id` 是 semantic descriptor logical identity，不是 schema fingerprint；
- `storage_type` 描述 C storage；共享 container category descriptor 可以为 NULL；
- `shape` 指向 immutable kind-specific descriptor；
- v1 不提前放一个没有签名/生命周期语义的 `cmeta_data_ops *` 占位字段。

`cmeta_data_desc` 自己也从 v1 起遵守 append-only prefix-size 规则。当前 validator 最低只要求 bytes 覆盖到 `shape` 字段结束：

```c
offsetof(cmeta_data_desc, shape) + sizeof(desc->shape)
```

不得要求：

```c
struct_size >= sizeof(cmeta_data_desc)
```

这样未来 append fingerprint/access/lifecycle tail 时，旧 descriptor 仍然可被新 consumer 读取其已知 prefix。

Semantic fingerprint 算法仍然需要设计，但在 descriptor graph contract 稳定之后单独实现。

## 5. Kind-specific immutable shapes

基础 scalar shapes：

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

整数 signedness 已由 `CMETA_DATA_SINT` / `CMETA_DATA_UINT` 表达，不再重复 `is_signed`。

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

`external_name / aliases / required / default / emit` 继续属于 CBind/schema policy。

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

v1 `cmeta_data_variant_case.tag` 的 ABI 是 `int64_t`。即使 tag descriptor 的
kind 为 `UINT`，所有 schema case tag 与 runtime discriminator 也必须可表示为
`int64_t`（最大 `INT64_MAX`）；超过该边界必须在 lookup 前 fail fast，禁止
cast/truncation。完整 `uint64_t` discriminator 需要未来新增带版本的 tag-value
表示，不能重新解释现有字段。

v1 只描述 immutable semantic structure、validation 和 lookup。Variant active-storage lifecycle/transaction 由后续 binding/lifecycle contract 处理。

## 6. Container semantic projection 通过 `ext.data`，不复制 TYPE

完成 prefix-safe ABI 后，append：

```c
struct cmeta_data_desc;

typedef struct cmeta_container_ext {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_container_type_ops *type;
    const struct cmeta_data_desc *data;
} cmeta_container_ext;
```

`type` 是唯一 generic constructor + argument source；`data` 只是 semantic category。

共享 immutable categories：

```c
extern const cmeta_data_desc cmeta_data_sequence;
extern const cmeta_data_desc cmeta_data_set;
extern const cmeta_data_desc cmeta_data_map;
```

以及：

```c
const cmeta_data_desc *cmeta_container_data(const void *object);
```

该 helper 只投影 descriptor 所声明的 semantic category，并验证 extension tail
与 data descriptor；它不证明 runtime generic application 有效。需要具体 T/K/V
或 typed binding 的调用方必须另外要求：

```c
cmeta_container_type_application_valid(object) == true
```

因此 raw-byte container 使用 canonical Vec descriptor 时可以投影为 semantic
SEQUENCE，但仍没有可用的 `Vec<T>` type application，不能进入 typed binding。

实际 T/K/V 继续只来自：

```c
cmeta_container_type_argument(object, index)
```

## 7. TurboSTL v1 mapping

明确映射：

```text
Vec<T>         -> SEQUENCE
Deque<T>       -> SEQUENCE
List<T>        -> SEQUENCE
Stack<T>       -> SEQUENCE
Queue<T>       -> SEQUENCE

Set<T>         -> SET
HashSet<T>     -> SET

HashMap<K,V>   -> MAP
Map<K,V>       -> MAP
BTree<K,V>     -> MAP
BPlusTree<K,V> -> MAP
```

明确 unresolved：

```text
Heap<T>        -> NULL
MultiMap<K,V>  -> NULL
```

原因：

- Heap iteration/storage order 不是稳定 sequence semantic order；
- MultiMap 允许同 key 多 value，不能静默伪装成 ordinary MAP。

以后需要 BAG/PRIORITY_QUEUE/MULTIMAP 等 semantic kind 时显式增加，不能通过损失语义获得表面统一。

## 8. Nested erased field 的边界

`#37` 证明的是已经带 runtime type binding 的 object，例如：

```c
Vec(int, values);
```

但：

```c
struct X {
    vec_t values;
};
```

zero state 下没有 `element_type`，不能凭 storage spelling 推断 `Vec<int>`。

本 semantic plan 不通过重新加入 `sequence.element = Int` 掩盖这个事实。

后续 construction/static-type plan 必须建立：

```text
static field TYPE application
        -> cmeta_container_bind_types(empty_object, declared_type)
        -> object becomes valid Vec<T>/Map<K,V>
        -> Collector transaction
```

因此当前只承诺：

```text
semantic introspection of already-typed container instances
```

不宣称 nested zero-field decode 已解决。

## 9. Implementation order after #37

```text
#36  TYPE<A...> contract                         DONE
#37  container extension + TurboSTL generic      DONE

C0   append-only container-extension ABI hardening
C1   CMeta semantic descriptor core
C2   TurboSTL semantic projection through ext.data

next construction + static field TYPE application
next CSerde canonical token protocol
next CBind
next TurboParser adapters / DataBind / TBE migration
```

Current implementation plan:

`docs/superpowers/plans/2026-08-23-cmeta-semantic-data-descriptors.md`
