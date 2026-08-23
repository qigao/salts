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
- 不在本 PR 引入“容器作为另一个容器的 owning element”所需的整容器 copy/move/destroy traits。`Vec<Vec<int>>` 的 declared identity 可以由后续递归 TYPE 扩展表达，但让外层 Vec 拷贝/销毁内层 Vec 是独立 ownership 能力，不属于 zero-handle binding。
- 不以字符串 `"Vec<int>"` 作为类型判断依据；字符串只用于展示。

## 已有约束

### `cmeta_type_identity` 已负责语义身份

`cmeta_type_identity` 已支持 `CMETA_TYPE_APPLY`，generic constructor 通过 `stable_id` 比较，参数递归按 identity 比较。`cmeta_type_equal()` 在 descriptor 带 identity 时使用该语义比较，而不是 descriptor 地址。

因此 declared type 不建立第二套 identity equality；它必须复用现有 application identity。

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

### 方案 A：declared application + 独立 construction ops（采用）

字段静态元数据拥有一个 `cmeta_declared_type`。它引用完整 application `cmeta_type_desc`、即时类型参数 descriptor，以及 construction ops。application descriptor 的 identity 使用现有 `CMETA_TYPE_APPLY`。

construction ops 由具体容器库提供；CMeta 只做通用校验和调度。zero handle 不做 runtime registry lookup。

优点：依赖方向正确、无全局状态、T/K/V 只有声明和实例这两个自然生命周期位置、可直接给 Collector 准备 output、以后 CBind 只是 orchestration。

### 方案 B：identity → descriptor runtime resolver / registry（拒绝）

字段只保存 `CMETA_TYPE_APPLY(Vec,int)`，运行时通过 registry 把 identity 解析成 constructible descriptor。

拒绝原因：需要 application canonicalization、registry 生命周期、并发和动态缓存；还会把本来编译期已知的字段类型变成运行时查找。

### 方案 C：Collector/CBind 各自保存 T/K/V（拒绝）

这会重新制造 semantic-data 阶段刚刚消除的多事实源。Collector 也会从“消费已绑定 output”退化为“理解每个具体容器的构造器”。

## CMeta declared type

新增公开只读结构，放在独立的 type/declaration 层，概念接口为：

```c
typedef struct cmeta_declared_type {
    const cmeta_type_desc *type;
    const cmeta_type_desc *const *arguments;
    size_t arity;
    const struct cmeta_container_construct_ops *construction;
} cmeta_declared_type;
```

语义：

- `type` 是完整声明类型的 descriptor。generic container 字段的 `type->identity` 必须是 `CMETA_TYPE_APPLY`。
- `arguments` 是直接写入 runtime handle 的即时参数 descriptor。它们的 identity 必须逐项等于 `type->identity->args`。
- `arity` 与 application identity arity 相同。
- `construction` 为可选 capability；非 constructible generic type 可以为 NULL。

提供：

```c
bool cmeta_declared_type_valid(const cmeta_declared_type *declared);
bool cmeta_declared_type_constructible(const cmeta_declared_type *declared);
const cmeta_type_desc *cmeta_declared_type_argument(
    const cmeta_declared_type *declared, size_t index);
```

validation 是 shallow capability validation + recursive identity validation，不分配内存。

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
- provider header（例如 `<turbostl/typed.h>`）注册 kind → storage / generic constructor / construction ops 的编译期映射。
- 未注册 kind 在编译期失败，不 fallback 到字符串或 runtime lookup。

`TYPE(...)` 是 CMeta schema/type-position spec，不是新的 C 语言类型，也不生成 `Vec_int`、`Map_int_long` 等用户可见 typedef。

### `cmeta_field_desc` 扩展

在现有字段 descriptor 末尾追加：

```c
const cmeta_type_desc *type;
const cmeta_declared_type *declared_type;
```

规则：

- 普通 `(int, id)`：`type == CMETA_TYPEOF(int)`，`declared_type == NULL`。
- `(TYPE(Vec, int), values)`：`type` 指向该字段 TU-local 的 application descriptor，`declared_type` 指向其 declared metadata。
- `type_name` 保持展示用途；TYPE 字段允许保留 source spelling，不参与 equality 或 binding。

TYPE 字段生成的 application descriptor 至少包含：

```text
name / sizeof(storage) / alignof(storage) / CMETA_T_OBJECT
identity = APPLY(canonical constructor, argument identities)
```

本阶段 application descriptor 不承诺 whole-container ownership traits，因此不能据此声称 `Vec<Vec<int>>` 已经可以作为 owning element 被外层容器复制。

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
validate construction ops ABI
        ↓
validate declared application constructor / arity
against construction->descriptor ext.type
        ↓
validate every argument descriptor and identity
        ↓
call provider bind_types(object, arguments, arity)
```

CMeta 不读取 zero object 的 descriptor 来发现 provider。

## TurboSTL bind contract

TurboSTL 为所有 canonical generic kinds 提供一个 construction ops object。unary/binary adapter 可共享实现宏，但最终每个 kind 都绑定自己的 canonical `cmeta_container_desc`。

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
CMeta type identity / Struct metadata
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
- TYPE application descriptor 是 TU-local static metadata；跨 TU equality 使用 existing identity semantics，不要求地址相等。

`cmeta_field_desc` 只在末尾追加字段。旧 5-field aggregate initializer 的新增尾字段按 C/C++ aggregate 规则为零；Struct 生成器更新为填充新字段。

## 错误与所有权

- declared metadata、application descriptor、construction ops 全部 immutable，不拥有 runtime object。
- bind 不分配内存，因此不存在 OOM 分支。
- bind 不取得 payload ownership，也不调用 element/key/value copy/move/destroy。
- argument descriptor 生命周期必须至少覆盖使用它的 bound handle；CMeta/TurboSTL 生成的字段 metadata 为 static storage duration。
- live container 的 destroy/init 语义仍由 TurboSTL 现有 API 管理。

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
- declared application constructor 为 canonical `stl_vec_generic_desc`，arity=1，argument=int。
- zero field bind 后 runtime generic introspection 返回 Vec<int>。
- Collector begin/accept/finish 成功，结果元素正确。

### GREEN 2：Map binary binding

使用 `TYPE(Map, int, long)`，验证 K/V、ordered-entry collector begin 和 put/finish 路径。

### GREEN 3：所有 TurboSTL generic kind construction mapping

用最小 unary/binary matrix 验证 13 个 kind 的 canonical descriptor、arity 与 binding slot，不把 semantic `data` 是否存在作为 construction 前提；因此 Heap/MultiMap 也必须能绑定。

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
2. CMeta declared-type core 与 Struct tail metadata。
3. append-only container construction protocol + validation/accessors。
4. TurboSTL unary construction binding。
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

若以后需要真正的 `Vec<Vec<int>>` / `Map<K, Vec<V>>` owning-container composition，则另开 ownership PR，为 container application descriptor 提供正确的 whole-container copy/move/destroy traits；不得把 shallow handle memcpy 偷渡进本 construction PR。