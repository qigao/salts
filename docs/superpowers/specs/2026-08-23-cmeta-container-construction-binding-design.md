# CMeta declared container type 与 construction binding 设计

日期：2026-08-23

## 背景

PR #41 已在 exact head `6f3951e64ee50bdb5803a781a7bacba7fe211ae7` 通过 CMeta conformance workflow #368，并以 merge commit `b1cdaa6979e1469ab57c329764ddaff9a0cf7c53` 合入 `master`。合并后的契约已经把两个维度明确分开：

- `cmeta_container_ext.type` 描述运行时对象的 generic application：`Vec<T>`、`Map<K,V>` 等；具体 T/K/V 只从 handle 上读取。
- `cmeta_container_ext.data` 只描述 format-neutral semantic category：SEQUENCE / SET / MAP，不拥有第二份 T/K/V。

现在剩下的缺口发生在对象还没有运行时类型绑定的时候。一个结构字段可以静态声明为 `Vec<int>`，但字段存储在 `struct` 中时通常以全零 handle 开始：`descriptor == NULL`、`element_type == NULL`。此时 Collector 需要 `element_type` 才能 begin，而 object-side `cmeta_container_extension(object)` 又需要 `descriptor` 才能找到能力，因此存在先有 descriptor 还是先有 T 的循环。

本设计建立声明侧到实例侧的唯一 construction 边界：

```text
static field TYPE(Vec, int)
        ↓
declared type metadata
        ↓
container construction ops
        ↓
cmeta_container_bind_types(zero field, declared type)
        ↓
runtime descriptor + T/K/V
        ↓
existing Collector
        ↓
future CBind
```

CBind 不参与本阶段实现。它以后只编排该链路，不解析 Vec/Map，不复制 T/K/V，也不认识 TurboSTL handle 布局。

## 目标

1. 为 CMeta 增加静态、只读的 declared generic type 表示，使字段元数据能够表达 `TYPE(Vec, int)` 和 `TYPE(Map, int, long)`，而不依赖对象内容或运行时 registry。
2. 在 `cmeta_container_ext` 末尾追加可选 construction capability，并保持 `.type`、`.data` 既有 ABI prefix 兼容。
3. 提供唯一公开入口 `cmeta_container_bind_types(void *object, const cmeta_declared_type *declared)`，由声明侧 construction ops 驱动全零/未初始化 handle 的 T/K/V 绑定。
4. 为所有已存在 canonical generic metadata 的 TurboSTL 容器提供 construction binding：Vec、Deque、List、Stack、Queue、Heap、Set、HashSet、HashMap、Map、MultiMap、BTree、BPlusTree。
5. 证明绑定后的空 handle 可以直接进入现有 Collector，不修改 Collector 协议。
6. 让 C/C++17 公共头都能读取新增字段类型元数据，并继续接受旧 `cmeta_container_ext` prefix。

## 非目标

- 不修改 CBind、CSerde、parser、DataBind、TBE 或 OPTIONAL。
- 不给 `cmeta_data_desc` 增加 T/K/V；semantic metadata 继续只有抽象类别。
- 不增加 constructor matcher、全局 type application registry、动态 descriptor cache 或进程级初始化。
- 不让 Collector 推导、缓存或拥有 declared T/K/V。
- 不改变 TurboSTL raw container 算法、容量语义、树/哈希实现或现有 natural instance API。
- 不在本 PR 引入“容器作为另一个容器的 owning element”所需的整容器 copy/move/destroy traits。因此 `TYPE(Vec, TYPE(Vec, int))`、`Vec<Vec<int>>` 作为 owning element 以及 `Map<K, Vec<V>>` 的值 ownership 不属于本阶段。
- 不以字符串 `"Vec<int>"` 作为类型判断依据；字符串只用于展示。

这里的“nested-container”目标是 **对象/Struct 图中嵌套的 container field**：例如一个 `Payload` 内部有全零 `Vec<int>` 字段，CBind 将来需要在读入元素前先给该字段绑定 T。它不是本 PR 顺手实现 container-of-container ownership。

## 已有约束

### `cmeta_type_identity` 已负责运行时语义身份

`cmeta_type_identity` 已支持 `CMETA_TYPE_APPLY`，generic constructor 通过 `stable_id` 比较，参数递归按 identity 比较。`cmeta_type_equal()` 在 descriptor 带 identity 时使用该语义比较，而不是 descriptor 地址。

运行时对象完成 bind 后，现有：

```text
cmeta_container_type_constructor(object)
cmeta_container_type_argument(object, i)
cmeta_container_type_application_valid(object)
```

继续是 runtime generic identity 的唯一事实源。

### header 不能静态复制 application identity

builtin atom identity（例如 int 对应 identity）目前是 `cmeta.c` 内部 static object。header 生成器可以常量初始化 `&cmeta_type_int`，但不能用 `cmeta_type_int.identity` 去静态初始化另一个全局 `cmeta_type_identity *` 数组，因为读取 extern object member 不是 ISO C static constant initializer。

因此 declared TYPE **不伪造第二个 `CMETA_TYPE_APPLY` object**，也不要求导出所有 atom identity。它保存 canonical generic constructor 和 argument descriptors；validation 时从 descriptor 取得 identity 并调用现有 `cmeta_type_application_valid()`。

这样 declared metadata 是“可构造声明”，不是第二套 type-identity 系统。

### TurboSTL handle 保存的是 `cmeta_type_desc *`

运行时 Vec/Set 等保存 `element_type`，Map/HashMap/BTree 等保存 `key_type` / `value_type`。绑定的最终产物必须仍是这些 descriptor 指针，不能把 handle 改成保存 CBind 私有 schema 或字符串。

### 真正的 zero handle 无法 object-side discover

全零 `vec_t` 同时没有 `cmeta.descriptor` 和 `element_type`。因此：

```c
cmeta_container_extension(zero_vec)
```

不能作为 construction 的起点。construction capability 必须由静态 declared type 直接携带，再由它写入 zero handle。

### Collector 已假设 output 完成类型绑定

Sequence collector factory 从 output 读取 element type；Map-family collector begin 在初始化前要求 key/value binding 合法。该前置条件是正确的，本阶段不把类型推导塞进 Collector。

## 方案比较

### 方案 A：declared constructor + argument descriptors + construction ops（采用）

字段静态元数据拥有一个 `cmeta_declared_type`。它引用：

- 实际 C storage descriptor（例如 `vec_t` 的 size/align/kind）；
- canonical generic constructor（例如 `stl_vec_generic_desc`）；
- 直接写入 handle 的 argument descriptors（例如 `&cmeta_type_int`）；
- provider construction ops。

validation 从 argument descriptor 读取现有 identity，调用 CMeta 既有 generic application validation。zero handle 不做 runtime registry lookup。

优点：所有 static initializer 都是 symbol address/整数常量；依赖方向正确；无全局状态；不复制 identity 实现；以后 CBind 只是 orchestration。

### 方案 B：header 生成第二个 `cmeta_type_identity APPLY`（拒绝）

除了形成第二个 application representation，还要求 header 能取得 atom identity 的常量地址。当前 identity 是 descriptor member 指向的内部 object，不能在严格 C 静态 initializer 中通过 `descriptor.identity` 取值。为满足该方案导出全部 atom identity 会扩大无必要 public surface。

### 方案 C：identity → descriptor runtime resolver / registry（拒绝）

字段只保存 logical APPLY，运行时通过 registry 解析 constructible storage/ops。

拒绝原因：需要 application canonicalization、registry 生命周期、并发和动态缓存；还会把本来编译期已知的字段类型变成运行时查找。

### 方案 D：Collector/CBind 各自保存 T/K/V（拒绝）

这会重新制造 semantic-data 阶段刚刚消除的多事实源。Collector 也会从“消费已绑定 output”退化为“理解每个具体容器的构造器”。

## CMeta declared type

新增公开只读结构，放在独立 `cmeta/declared_type.h`：

```c
typedef struct cmeta_declared_type {
    const cmeta_type_desc *storage_type;
    const cmeta_generic_desc *constructor;
    const cmeta_type_desc *const *arguments;
    size_t arity;
    const struct cmeta_container_construct_ops *construction;
} cmeta_declared_type;
```

语义：

- `storage_type` 描述字段的实际 C storage：size / align / kind。对 TurboSTL TYPE field，它是 provider 的 canonical handle-storage descriptor，identity 可以为 NULL，因为 generic application identity 不由 storage layout 决定。
- `constructor` 是 canonical generic constructor。
- `arguments` 是直接写入 runtime handle 的即时参数 descriptor。
- `arity` 必须被 constructor 接受。
- `construction` 为可选 capability；非 constructible generic type可以为 NULL。

提供：

```c
bool cmeta_declared_type_valid(const cmeta_declared_type *declared);
bool cmeta_declared_type_constructible(const cmeta_declared_type *declared);
const cmeta_type_desc *cmeta_declared_type_argument(
    const cmeta_declared_type *declared, size_t index);
```

`cmeta_declared_type_valid()`：

1. 验证 `storage_type`、`constructor`、arity。
2. 验证每个 argument 是合法 `cmeta_type_desc` 且存在合法 identity。
3. 在栈上收集 argument identity pointers。
4. 调用既有 `cmeta_type_application_valid(constructor, identities, arity)`。

不分配、不注册、不缓存 application object。

## 字段 TYPE DSL

`Struct(...)` 保持“字段 row 不需要 Field wrapper”的现有风格。generic 字段使用 type-position `TYPE(...)`：

```c
Struct(Payload,
    (int, id),
    (TYPE(Vec, int), values),
    (TYPE(Map, int, long), index)
);
```

其中：

- `TYPE(Vec, int)` 的实际 C storage 是 `vec_t`。
- `TYPE(Map, int, long)` 的实际 C storage 是 `map_t`。
- provider header（例如 `<turbostl/typed.h>`）注册 kind → storage C type / storage descriptor / generic constructor / construction ops 的编译期映射。
- 未注册 kind 在编译期失败，不 fallback 到字符串或 runtime lookup。
- 本阶段 TYPE argument 必须是 `CMETA_TYPEOF(...)` 能解析到合法 descriptor 的普通具体 C type；nested `TYPE(...)` argument 不在 C3 grammar 中。

`TYPE(...)` 是 CMeta schema/type-position spec，不是新的 C 语言类型，也不生成 `Vec_int`、`Map_int_long` 等用户可见 typedef。

### TYPE spec 的预处理器表示

CMeta 用 tagged parenthesized spec 保存 source tokens，例如概念上：

```c
#define TYPE(kind, ...) (CMETA_TYPE_SPEC_TAG, kind, __VA_ARGS__)
```

`Struct` replay 在 declaration pass 中把 spec 降为 provider storage C type；在 metadata pass 中生成 TU-local argument descriptor array 和 `cmeta_declared_type`。

普通 `int`、`double` 等非 TYPE 字段继续走现有 declaration 路径。preprocessor dispatch 只识别带 CMeta tag 的 tuple，不把任意 parenthesized C token 当 generic type。

### `cmeta_field_desc` 扩展

在现有字段 descriptor 末尾追加：

```c
const cmeta_type_desc *type;
const cmeta_declared_type *declared_type;
```

规则：

- 普通 `(int, id)`：`type == CMETA_TYPEOF(int)`，`declared_type == NULL`。
- `(TYPE(Vec, int), values)`：`type == declared_type->storage_type`，`declared_type != NULL`。
- `type_name` 保持展示用途；TYPE 字段允许保留 source spelling `TYPE(Vec, int)`，不参与 equality 或 binding。

TurboSTL provider 为每个 handle kind 暴露 canonical storage descriptor，例如概念上：

```c
extern const cmeta_type_desc stl_vec_storage_type;
```

它只描述 `vec_t` layout，不宣称 `Vec<int>` application identity，也不默认提供 whole-container ownership traits。

## Container construction extension

新增独立 versioned ops：

```c
typedef cmeta_status (*cmeta_container_bind_types_fn)(
    void *object,
    const cmeta_type_desc *const *arguments,
    size_t arity);

typedef struct cmeta_container_construct_ops {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_container_desc *descriptor;
    cmeta_container_bind_types_fn bind_types;
} cmeta_container_construct_ops;
```

并在 `cmeta_container_ext` **末尾**追加：

```c
const cmeta_container_construct_ops *construction;
```

`CMETA_CONTAINER_EXT_ABI_VERSION` 继续保持当前 append-safe 语义；旧 producer 只报告到 `.type` 或 `.data` 的 `struct_size` 时，新 construction accessor 返回 NULL。

提供 object-side 只读 accessor：

```c
const cmeta_container_construct_ops *
cmeta_container_construction(const void *object);
```

它只服务已绑定对象的 introspection。zero-handle bind **不得**通过这个 accessor 起步。

## 唯一 bind 入口

公开入口：

```c
cmeta_status cmeta_container_bind_types(
    void *object,
    const cmeta_declared_type *declared);
```

执行顺序：

```text
validate declared type
        ↓
require constructible + validate construction ops ABI
        ↓
obtain canonical runtime constructor from construction->descriptor.ext.type
        ↓
compare constructor stable identity + arity with declared
        ↓
validate every argument descriptor / identity
        ↓
call provider bind_types(object, arguments, arity)
```

CMeta 不读取 zero object 的 descriptor 来发现 provider。

成功 bind 后，runtime object 自己的 `.type` introspection 重新成为 authoritative runtime application；declared metadata 不需要生成另一份 runtime identity object。

## TurboSTL bind contract

TurboSTL 为所有 canonical generic kinds 提供：

1. canonical storage descriptor；
2. TYPE provider macro registration；
3. construction ops object。

unary/binary adapter 可共享实现宏，但最终每个 kind 都绑定自己的 canonical `cmeta_container_desc`。

### Unary

```text
Vec / Deque / List / Stack / Queue / Heap / Set / HashSet
arity = 1
argument[0] → element_type
```

### Binary

```text
HashMap / Map / MultiMap / BTree / BPlusTree
arity = 2
argument[0] → key_type
argument[1] → value_type
```

construction 与 semantic `.data` 独立，因此 Heap / MultiMap 即使 `.data == NULL`，仍然必须支持 declared type binding。

### 状态语义

`bind_types` 是 pre-construction 操作，状态优先级固定为：

1. `object == NULL`、declared/arity/argument 非法 → `CMETA_INVALID_ARGUMENT`。
2. live / initialized container → `CMETA_INVALID_ARGUMENT`，即使当前 binding 相同也不把它当成合法 construction 调用。
3. canonical unbound zero handle → 完成绑定，返回 `CMETA_OK`。
4. 已绑定但仍未初始化，descriptor 与全部 T/K/V 完全相同 → idempotent no-op，返回 `CMETA_OK`。
5. 已绑定但 constructor 或任一 T/K/V 不同 → `CMETA_TYPE_MISMATCH`。
6. descriptor/T/K/V 只写了一部分的非 canonical partial state → `CMETA_INVALID_ARGUMENT`；binder 不尝试修复未知半状态。

### Transactionality

所有失败条件必须在写 handle 前完成校验。成功提交阶段只写 declaration metadata：

```text
container descriptor
T 或 K/V pointers
```

不分配 storage、不调用 init、不改变 size/capacity/generation/root/impl。禁止产生 `descriptor = Vec` 但 `element_type = NULL` 的半绑定状态。

## Collector 边界

绑定成功后继续使用现有 descriptor collector factory；Collector API 本身不增加 declared-type 参数。

Vec 验收链：

```text
Payload.values == all-zero vec_t
        ↓
field.declared_type == TYPE(Vec,int)
        ↓
cmeta_container_bind_types(&payload.values, field.declared_type)
        ↓
values.cmeta.descriptor == &stl_vec_container_desc
values.element_type == &cmeta_type_int
        ↓
stl_vec_container_desc.collector(&values, limit)
        ↓
cmeta_collector_begin/accept/finish
```

Map-family 同理，在 collector begin 前 key/value 已存在。

## 依赖边界

```text
CMeta type descriptors / identities / Struct metadata
        ↓
cmeta_declared_type + generic construction protocol
        ↓
TurboSTL provider registrations + bind adapters
        ↓
existing TurboSTL Collector adapters
        ↓
future CBind
```

CMeta 不 include TurboSTL。TurboSTL include CMeta 并注册自己的 TYPE provider macros。CBind 未来只依赖 CMeta public protocols；它不 hard-code TurboSTL layout。

## ABI 与兼容性

必须保持：

- `cmeta_container_ext` 原有 prefix through `.type` 与 `.data` 的 offset/含义不变；construction 只追加在末尾。
- `cmeta_container_type_ops` 不修改。
- `cmeta_data_desc` 不修改。
- TurboSTL public handle 字段顺序、size/align 不因 construction binding 改变。
- 已存在 `Vec(int, name)` 等 declaration DSL 和 natural instance API 继续工作。
- 旧 synthetic extension 的 `struct_size` 不覆盖 construction 字段时，`cmeta_container_construction()` 安全返回 NULL。
- declared TYPE metadata 是 TU-local static metadata；其 constructor equality 依赖 existing stable generic ID，argument equality 依赖 existing descriptor identity semantics，不要求 metadata 地址相等。

`cmeta_field_desc` 只在末尾追加字段。旧 5-field aggregate initializer 的新增尾字段按 C/C++ aggregate 规则为零；Struct 生成器更新为填充新字段。

## 错误与所有权

- declared metadata、storage descriptor、construction ops 全部 immutable，不拥有 runtime object。
- bind 不分配内存，因此不存在 OOM 分支。
- bind 不取得 payload ownership，也不调用 element/key/value copy/move/destroy。
- argument descriptor 生命周期必须至少覆盖使用它的 bound handle；CMeta/TurboSTL 生成的字段 metadata 为 static storage duration。
- live container 的 destroy/init 语义仍由 TurboSTL 现有 API 管理。
- storage descriptor 不提供 shallow handle memcpy 作为 owning copy；后续 whole-container ownership 必须显式实现。

## TDD 设计

### RED 1：字段没有 declared TYPE

先增加 C11 test，声明：

```c
Struct(Payload,
    (TYPE(Vec, int), values)
);
```

并读取 `field->declared_type`。当前代码应在编译期失败，因为 `cmeta_field_desc`/`Struct` 尚无该 contract。

### RED 2：construction extension 缺失

测试访问 container construction capability，并调用：

```c
cmeta_container_bind_types(&payload.values, field->declared_type)
```

生产头不存在对应 contract 时保持 RED。

### GREEN 1：Vec static TYPE → zero bind → Collector

验证：

- 字段实际 storage 为 `vec_t`，offset/size/align 正确。
- declared constructor 为 canonical `stl_vec_generic_desc`，arity=1，argument=int。
- zero field bind 后 runtime generic introspection 返回 Vec<int>。
- Collector begin/accept/finish 成功，结果元素正确。

### GREEN 2：Map binary binding

使用 `TYPE(Map, int, long)`，验证 K/V、ordered-entry collector begin 和 put/finish 路径。

### GREEN 3：所有 TurboSTL generic kind construction mapping

用最小 unary/binary matrix 验证 13 个 kind 的 canonical descriptor、arity 与 binding slot，不把 semantic `data` 是否存在作为 construction 前提；因此 Heap/MultiMap 也必须能绑定。

### declared validation

CMeta test 使用 synthetic storage descriptor + generic descriptor 验证：

- arity 被 constructor 接受；
- NULL / malformed argument 被拒绝；
- argument identity 缺失或非法被拒绝；
- constructible helper 只接受合法 construction ops；
- validation 不依赖 runtime registry。

### 状态与错误测试

覆盖：

- NULL object / NULL declared / invalid arity / NULL argument。
- zero → bind 成功。
- bound-uninitialized same type → idempotent OK。
- bound-uninitialized different T/K/V → TYPE_MISMATCH。
- partial descriptor/type state → INVALID_ARGUMENT。
- live initialized container → INVALID_ARGUMENT，且 byte-for-byte 不变。
- failed bind 不修改 descriptor/T/K/V/generation/impl/storage。

### ABI regression

保留并扩展旧 prefix synthetic tests：

```text
old through .type  → data NULL, construction NULL
old through .data  → data valid, construction NULL
new full prefix    → construction valid
```

C++17 验证 standard-layout、field tail compatibility、TYPE Struct storage 以及 accessor。

## CI 门槛

每个 TDD 阶段至少运行相关 CMeta/TurboSTL target；最终 exact head 必须满足：

```text
Linux release
  fresh configure
  build
  selected CMeta/CFlow/TurboSTL tests

Windows release
  configure
  build
  test
```

若新 workflow 暴露与本改动无关的现有失败，必须区分 base failure 与 branch regression，不以重跑掩盖真实 RED/GREEN。

## 实施切片

本设计按单 PR 完成，但提交保持可审查边界：

1. RED：TYPE field + construction/bind contract tests。
2. CMeta declared-type core、TYPE spec preprocessor dispatch 与 Struct tail metadata。
3. append-only container construction protocol + validation/accessors。
4. TurboSTL storage descriptors / TYPE provider registration / unary construction binding。
5. TurboSTL binary construction binding。
6. Collector integration tests、negative-state tests、C++ ABI tests。
7. exact-head Linux + Windows CI，扫描 diff 确认没有 CBind/CSerde/parser/DataBind/TBE 改动。

## 后续边界

本 PR 结束时，future CBind 所需的最小 primitive 已完整存在：

```text
field metadata
  → declared TYPE
  → bind zero handle
  → existing Collector
```

下一 PR 才让 CBind 在遇到 container field 时调用上述 primitive。CBind 不获得新的 TurboSTL-specific switch。

若以后需要真正的 `Vec<Vec<int>>` / `Map<K, Vec<V>>` owning-container composition，则另开 ownership PR，为 container application value 提供正确的 whole-container copy/move/destroy traits，并决定 nested TYPE argument 的 descriptor/identity 生成方式；不得把 shallow handle memcpy 偷渡进本 construction PR。