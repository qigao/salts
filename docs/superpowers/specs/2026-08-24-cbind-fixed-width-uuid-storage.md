# CBind Fixed-Width Integer and UUID Storage Design

日期：2026-08-24

状态：Accepted for implementation

## 背景与目标

TurboUtils CMeta 已能用 `cmeta_data_integer_shape.bits` 描述 8、16、32、64 位整数，
但 CBind 的 scalar preflight 与 decode 仍只接受 `int`、`long`、`size_t` 三个 canonical
storage identity。该限制无法表达窄整数，也在 Windows LLP64 上把 schema 的精确 64 位
storage 与 `long` 错误绑定。TurboParser issue #5 需要通用 CBind kernel 支持 descriptor
声明的精确整数宽度，同时需要把 schema `uuid` 的 canonical string 安全地转换为现有
`turbo_uuid_t`。

本改动扩展通用 CBind/CMeta 能力，不引入 DataBind，不改变现有公开函数签名或 ABI
version，也不修改 struct field 的 offset/size/alignment 校验路径。

## 现状证据

- `cbind/src/scalar.c` 的 integer preflight 逐一比较 `cmeta_type_int`、
  `cmeta_type_long`、`cmeta_type_size`；合法的其他 integer identity 返回
  `CBIND_UNSUPPORTED`。
- 同一文件的 zero 检查、reset 和 decode 也通过 canonical identity 选择 native C 类型，
  因此仅放宽 preflight 会导致错误宽度读写。
- `cmeta/src/data.c` 已在 `cmeta_data_desc_valid` 中限制 integer bits 为
  `{8,16,32,64}`，并验证 storage type descriptor 的基本结构。
- `utils/include/turbo_uuid.h` 的 `turbo_uuid_t` 是无堆所有权的固定 16-byte value；
  现有 `turbo_uuid_parse` 接收 NUL-terminated text，不能直接安全消费 CSerde slice。

## 范围

本阶段支持：

- `CMETA_DATA_SINT` 对 `int8_t`、`int16_t`、`int32_t`、`int64_t` 宽度；
- `CMETA_DATA_UINT` 对 `uint8_t`、`uint16_t`、`uint32_t`、`uint64_t` 宽度；
- 现有 `int`、`long`、`size_t` descriptor 与 decode 行为保持不变；
- header-local fixed-width CMeta type/data descriptors；
- `turbo_uuid_t` 的 header-local type descriptor、owned STRING adapter 和 data descriptor。

不在范围内：修改 CMeta 全局 builtin registry、增加 token kind、修改 UUID public parse API、
容器/optional/default/union、DataBind adapter、JIT，以及 vendor 代码。

## 候选方案

### 方案 A：为每个 fixed-width identity 增加 canonical 分支

拒绝。CBind 会继续依赖一组中心注册 identity，第三方合法 descriptor 仍无法使用，且
`int32_t` 与 `int` 等 typedef 关系随平台变化。

### 方案 B：按 storage size 直接写整数

拒绝。只看 size 会接受错误 kind、错误 shape bits 或错误 alignment；直接把 `void *`
转换为窄整数指针还会引入未对齐访问与 aliasing 风险。

### 方案 C：descriptor-driven width validation + exact temporary

采用。preflight 验证 kind、合法 bits、`CHAR_BIT == 8`、精确 size 和对应 exact-width
类型的 alignment；decode 先在 64-bit token 域完成范围校验，再用 `intN_t/uintN_t`
temporary 和 `memcpy` 提交。该方案允许任意语义 identity，同时保留 storage ABI
约束和事务性 destination。

UUID 作为 `CMETA_DATA_STRING` 的 owned adapter：输入 slice 被同步解析并复制成 16-byte
value；descriptor 不借用输入，不分配，也不改变 CSerde 协议。

## Fixed-width integer 契约

integer descriptor admission 必须同时满足：

1. `cmeta_data_desc_valid(shape)` 成功；
2. kind 是 `CMETA_DATA_SINT` 或 `CMETA_DATA_UINT`；
3. 目标平台 `CHAR_BIT == 8`；
4. bits 是 8、16、32 或 64；
5. `storage_type->kind == CMETA_T_INTEGER`；
6. `storage_type->size == bits / CHAR_BIT`；
7. `storage_type->align` 等于对应 `intN_t` 或 `uintN_t` 的 alignment。

identity 不要求等于 `int/long/size_t`。错误 bits 在 CMeta descriptor validation 阶段返回
`CBIND_INVALID_SHAPE`；错误 size/alignment/kind 也在消费 token 前返回
`CBIND_INVALID_SHAPE`。旧 canonical descriptor 仍通过同一规则。

token conversion 规则保持原语义：

- signed storage 接受范围内的 `CSERDE_SINT` 和 `CSERDE_UINT`；
- unsigned storage 接受范围内的非负 signed token 和 unsigned token；
- float 必须 finite、integral 且在目标宽度范围内；
- 数值失败返回 `CBIND_VALUE_OUT_OF_RANGE`，类型不匹配返回
  `CBIND_TOKEN_MISMATCH`；
- 写入只在全部检查成功后发生，失败时 destination 保持 semantic zero。

zero 检查和 reset 同样按 descriptor bits 使用 exact-width temporary，不读写 storage
边界之外。时间复杂度 O(1)，空间复杂度 O(1)，无分配、无锁。

## Header-local metadata

`turbo_cmeta_data.h` 提供下列稳定命名：

- `turbo_int8_cmeta_type` / `turbo_int8_cmeta_data`，依次覆盖 16/32/64；
- `turbo_uint8_cmeta_type` / `turbo_uint8_cmeta_data`，依次覆盖 16/32/64；
- `turbo_uuid_cmeta_type`、`turbo_uuid_cmeta_buffer_ops`、
  `turbo_uuid_cmeta_data`。

每个 type descriptor 使用 `turbo.<ctype>` stable atom identity。descriptor 是
header-local immutable metadata；不同 translation unit 的地址不同，消费者必须使用
`cmeta_type_equal` 或字段语义比较，不能比较地址。编译期断言要求 `CHAR_BIT == 8`、
每个 exact-width typedef 的 size 符合其名称，并要求 `turbo_uuid_t` 恰为 16 bytes。

## UUID 所有权、生命周期与容量协议

| 项目 | 契约 |
|---|---|
| 数据单元 | canonical 36-byte `8-4-4-4-12` text 转换为固定 16-byte `turbo_uuid_t` |
| 事实源 | assign 成功后 destination 的 `bytes[16]`；输入只在调用期间借用 |
| 所有权 | adapter 标为 `CMETA_DATA_BUFFER_OWNED`，通过值复制拥有结果；无 heap allocation |
| 生命周期 | 输入 slice 在 assign 返回后不保留；destination 由调用方拥有 |
| 容量 | 输入必须恰好 36 bytes，且 `size <= max_bytes`；无增长结构 |
| 失败 | 非法参数/长度/hyphen/hex 返回 `CMETA_INVALID_ARGUMENT`；超限由 CMeta wrapper 返回 `CMETA_CAPACITY_EXCEEDED` |
| 回滚 | assign 在 local temporary 中解析，成功后一次 `memcpy`；任何失败后 wrapper 调用 restore，destination 为全零 |
| 并发 | metadata immutable；每个 destination 由调用方单 owner，函数不使用共享可变状态 |

解析函数按显式 `size` 索引固定 36 bytes，不调用 `strlen`，不观察 slice 末尾后的字节，
也不要求第 37 byte 为 NUL。uppercase/lowercase hex 都接受；hyphen 必须位于 8、13、18、
23。`restore_zero` 清除完整 16 bytes，重复调用幂等且 no-fail。

## UUID adapter provenance

Generic `cmeta_data_buffer_ops` 只承诺 buffer adapter 的结构、ownership 与 callback 完整性，
不能证明三个 callback 实现 canonical UUID 语义。UUID 因此额外提供 header-local、
size-prefixed `turbo_uuid_cmeta_buffer_ops_desc`。它把 generic
`cmeta_data_buffer_ops base` 置于 offset zero，并把 UUID ABI version 与 provider validation
callback 放在 tail；`base.struct_size` 覆盖完整 tail。candidate 通过自己的 `buffer_ops`
直接携带 provenance，消费方只需调用 `turbo_uuid_cmeta_data_valid(candidate)`，不需要带外
record 或改变既有 candidate-only admission API。

provider validation callback 由创建该组 UUID metadata/callbacks 的同一 TU 实例化。
consumer 只在验证 generic descriptor/ops prefix 与 extension size 后调用它，不把自己的
header-local descriptor、table 或 function 地址与 provider 比较。provider callback 在其
自身 TU 内验证 UUID stable identity、精确 storage ABI、owned shape、extension ABI/self
callback，以及三个 base callbacks。因此来自另一 TU 的 canonical candidate 与完整深复制
均可通过；复制 provenance 后单独替换 `is_zero`、`assign` 或 `restore_zero` 都被拒绝。

该 UUID 专用 additive tail 不修改 `cmeta_data_desc`、`cmeta_data_buffer_ops` layout、generic
ABI version 或 tstr/vstr 行为。既有 `turbo_uuid_cmeta_type` 和
`turbo_uuid_cmeta_data` 仍是相同类型的 header-local const objects。
`turbo_uuid_cmeta_buffer_ops` 为保持 `&name`、field access、`sizeof(name)` 和 const-lvalue
source 用法，成为指向 extension base member 的 macro lvalue facade；依赖
`decltype(turbo_uuid_cmeta_buffer_ops)` 精确 object-declaration 规则的 C++ metaprogram 需改为
`decltype((turbo_uuid_cmeta_buffer_ops))` 后移除 reference。C/C++ compile assertions 固化了
其 const base lvalue 与 address type；这是为实现 candidate discovery 的唯一 source-level
影响，没有 link ABI 影响。

## 架构、依赖与公开行为影响

依赖方向保持 `TurboUtils::Core -> TurboUtils::CMeta` 以及
`TurboUtils::CBind -> TurboUtils::CMeta + TurboUtils::CSerde`。UUID metadata 仅包含已安装的
`turbo_uuid.h`，不让 CBind 反向依赖 Core。TurboParser 可链接 Core 取得 UUID value API，
并把 header-local descriptor 交给 CBind。

公开 ABI records、enum values 和已有 symbol 不变。新增 header-local symbol 不需要 DLL
export；此前返回 unsupported 的合法 fixed-width descriptor 开始成功，是向后兼容的
能力扩展。malformed descriptor 继续 fail fast，且在输入消费前失败。

## 迁移与回滚

消费方可逐字段把自定义 integer descriptor 改为本头提供的 fixed-width descriptor；旧
`cmeta_data_int/long/size` 无需迁移。UUID 消费方应使用 `turbo_uuid_cmeta_data`，不要把
UUID 伪装为普通 `tstr`。

回滚仅需撤销新增 header-local metadata 与 CBind width dispatch；没有 wire format、持久化
数据或 ABI version 迁移。TurboParser 在依赖版本不足时应 configure-time fail fast，不能
退回旧 native-width 猜测。

## 验证

- CBind：每个 signed/unsigned width 的 min/max、cross-signed acceptance、negative unsigned、
  one-past-range、integral/fractional/nonfinite float、wrong bits/size/alignment、失败不改目标，
  以及 legacy `int/long/size_t`。
- CMeta/Core：fixed-width descriptor validity/semantic identity；UUID lower/uppercase、精确
  length/hyphen/hex、non-NUL slice、max buffer、occupied destination、失败归零、幂等 reset。
- UUID provenance：跨 C/C++ translation unit canonical candidate admission，三种 callback
  replacement rejection、intact deep-copy acceptance，以及 generic/extension prefix 边界前的
  size-gated rejection；fixture 只向 consumer 暴露 candidate pointer。
- C++：安装头可编译，descriptor const/type/size assumptions 成立。
- 构建：focused targets、完整 Release CTest、`verify_installed_package`、依赖闭包、
  `git diff --check`，并确认无 vendor 与 `.codegraph` 产物进入提交。
