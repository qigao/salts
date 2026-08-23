# CMeta 驱动的序列化与数据绑定设计

日期：2026-08-23
状态：Design / Draft

## 1. 背景

TurboUtils 已经具备三块可以直接成为数据绑定基础的能力：

- CMeta 提供类型身份、traits、Enum/Struct 元数据、Range、Collector 和 container descriptor；
- TurboSTL 已完成 self-describing instance API，标准容器通过 `cmeta_container_desc` 暴露 Range/Collector；
- CFlow 能消费 Range/Collector，但它是 execution 层，不应成为序列化核心依赖。

当前缺口在于：CMeta 的 `Struct(...)` 只描述 C ABI layout——字段名、类型文本、offset、size、align——并不描述 wire/data 语义。`vstr` 就是直接反例：其 ABI 是 `{ const char *data; size_t len; }`，但数据语义应当是一个 borrowed string view，而不是一个包含 pointer 和 length 的 object。

因此本设计严格区分：

```text
C layout reflection != semantic data shape != wire format
```

另有现存模块 `turbo_serial`，含义是 serial-port/串口。新的 serialization 模块不得使用 `serial` / `turbo_serial` 名称，避免概念冲突。

## 2. 目标

1. 建立 format-neutral semantic data model，使同一份 C 数据绑定可服务 JSON、CBOR、MessagePack、TOML 等 codec。
2. 建立 streaming token reader/writer contract，避免 DOM 成为强制中间表示。
3. 建立 C object <-> token 的通用 binding engine。
4. 直接复用 TurboSTL 已有 `Range + Collector + cmeta_container_desc`，不为每种容器生成 serialize facade。
5. 支持 nested struct、enum、owned/borrowed string、sequence、map、optional 和 custom adapter。
6. decode 失败不留下半初始化对象或半构造容器。
7. 默认严格处理 unknown field、duplicate field、missing required field、numeric overflow 和资源限制。
8. ownership、reader view lifetime、allocator/limits 必须成为显式 contract。
9. JSON 作为第一个 reference codec，但核心设计不依赖 JSON object model。
10. 为后续 CFlow streaming adapter 留边界，但 v1 不让 CFlow 进入核心依赖图。

## 3. 非目标

v1 不做：

- mandatory DOM/value tree；
- runtime network schema registry；
- 裸 pointer 自动序列化；
- 根据 `const char *` 猜测 string ownership；
- 将 serialize/deserialize callback 塞进 `cmeta_type_traits`；
- 将 JSON rename/base64/date policy 塞进 `cmeta_type_desc`；
- 自动 protobuf/IDL 生成；
- 自动 migration graph；
- cyclic object graph/reference identity；
- CFlow graph execution 集成；
- 将现有 `turbo_serial` 改造成 serialization 模块。

## 4. 采用架构

采用：**CMeta semantic shape + CSerde streaming token + CBind independent binding**。

正确依赖关系是：

```text
                    CMeta semantic shape
                           |
                           |
TurboSTL --implements--> CMeta container contract
                           |
                           v
                         CBind <---------------- CSerde token contract
                           ^                         ^
                           |                         |
                           |                 +-------+--------+
                           |                 |                |
                           |             JSON codec       future codecs
                           |                              CBOR/MsgPack/...
                           |
                      application
```

等价的 target 依赖：

```text
TurboSTL -> CMeta
CSerde   -> minimal runtime/C runtime only
CBind    -> CMeta + CSerde
JSON     -> CSerde implementation
CFlow    -> future adapter only
```

JSON/CBOR 是 **CSerde 的 concrete codec**，不是 CBind 的下游模块。CBind 永远只消费 `cserde_reader` / `cserde_writer`。

拒绝 DOM-first 作为核心，因为它强制完整中间树和额外 allocation/traversal。拒绝 `Type x Format` 代码生成，因为会重新形成 TurboSTL 刚删除的 generated facade 笛卡尔积。

## 5. 模块与目标

### 5.1 CMeta

新增 format-neutral semantic descriptor，建议：

```text
cmeta/include/cmeta/data.h
```

CMeta 不解析 JSON，不分配 JSON DOM，不包含 codec callback。

### 5.2 CSerde

新模块：

```text
cserde/
  include/cserde/token.h
  include/cserde/reader.h
  include/cserde/writer.h
  include/cserde/json.h
  src/...
```

CMake target：

```text
TurboUtils::CSerde
```

职责仅为 token I/O、syntax state 和 concrete codec。

### 5.3 CBind

新模块：

```text
cbind/
  include/cbind/cbind.h
  include/cbind/policy.h
  include/cbind/error.h
  src/...
```

CMake target：

```text
TurboUtils::CBind
```

依赖：

```text
CBind -> CMeta + CSerde
```

CBind 不直接 link TurboSTL。任何实现 CMeta container contract 的容器都可以参与 binding。

### 5.4 CFlow

未来仅提供 adapter：

```text
CSerde reader -> CFlow source
CFlow stream   -> CSerde writer sink
```

CSerde/CBind 不依赖 CFlow。

## 6. CMeta semantic data model

最小 semantic kind：

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
    CMETA_DATA_OPTIONAL,
    CMETA_DATA_SEQUENCE,
    CMETA_DATA_MAP,
    CMETA_DATA_CUSTOM
} cmeta_data_kind;
```

concept descriptor：

```c
typedef struct cmeta_data_desc {
    const char *stable_id;
    const char *display_name;
    cmeta_data_kind kind;
    const cmeta_type_desc *storage_type;
    const void *shape;
    const struct cmeta_data_ops *ops;
} cmeta_data_desc;
```

`storage_type` 描述 C storage；`shape` 指向 kind-specific immutable descriptor；`ops` 只负责 generic lifecycle/access，不负责 JSON/CBOR。

### 6.1 scalar

整数 descriptor 必须记录 signedness 和 width。decode 进行精确范围检查，禁止 silent cast/truncation。

float descriptor 记录 storage width。non-finite/overflow 等转换由 CBind numeric policy 控制。

### 6.2 string / bytes

string/bytes 是 semantic scalar，而不是 pointer reflection。至少区分：

```text
owned
borrowed
custom/fixed-buffer
```

`tstr` -> owned string adapter。

`vstr` -> borrowed string-view adapter；它可以同时保留现有 `Struct(vstr, ...)` ABI reflection，但 semantic data shape 必须是 STRING，不是 STRUCT。

### 6.3 enum

semantic enum shape 引用 `cmeta_enum_desc`。representation 由 CBind policy 选择 text/symbol/integer；默认 human-readable binding 使用 `text`。

### 6.4 struct

concept：

```c
typedef struct cmeta_data_field_desc {
    const char *stable_id;
    const char *name;
    size_t offset;
    const cmeta_data_desc *value;
} cmeta_data_field_desc;

typedef struct cmeta_data_struct_desc {
    const cmeta_struct_desc *layout;
    const cmeta_data_field_desc *fields;
    size_t field_count;
} cmeta_data_struct_desc;
```

canonical semantic field name 属于 data shape；rename aliases、required/default 属于 CBind policy，不放进 `cmeta_struct_desc`。

### 6.5 sequence / map

sequence shape 引用 element semantic descriptor；map shape 引用 key/value semantic descriptor。它们不硬编码 `vec_t` / `map_t`。

## 7. declaration DSL

低层 descriptor API 是第一事实源，宏只是 sugar。

未来新类型可以提供 single-source DSL，例如：

```c
DataStruct(User,
    (int,   id,   Int),
    (tstr,  name, StringOwned),
    (vec_t, tags, Sequence(Int))
);
```

目标是同时生成 C layout、`cmeta_struct_desc` 和 `cmeta_data_desc`。

但 implementation v1 不先从复杂宏开始；先以显式 descriptor + tests 锁定语义，再决定最终 DSL。已有 `Struct(...)` 可以附加 sidecar semantic binding；whole-type custom/scalar adapter 可以覆盖 field recursion。

原则：

```text
reflection describes memory;
binding describes meaning.
```

## 8. CSerde token model

CSerde 使用 pull reader + push writer，共享 format-neutral token：

```c
typedef enum cserde_token_kind {
    CSERDE_NULL,
    CSERDE_BOOL,
    CSERDE_SINT,
    CSERDE_UINT,
    CSERDE_FLOAT,
    CSERDE_STRING,
    CSERDE_BYTES,
    CSERDE_ARRAY_BEGIN,
    CSERDE_ARRAY_END,
    CSERDE_MAP_BEGIN,
    CSERDE_MAP_END
} cserde_token_kind;
```

不定义 JSON-specific OBJECT。struct/object 在 token 层就是 string-key map：

```text
MAP_BEGIN
STRING("id")
SINT(7)
STRING("name")
STRING("alice")
MAP_END
```

这样 CBOR/MessagePack 可表达 arbitrary-key map；JSON codec 自己施加 key 必须可表示为 string 的限制。

### 8.1 payload lifetime

string/bytes token 使用 view：

```c
typedef struct cserde_slice {
    const unsigned char *data;
    size_t size;
} cserde_slice;
```

每个 token 或 reader capability 必须明确 view lifetime：

```text
TRANSIENT: 最迟下一次 next() 后失效
STABLE:    backing input 生命周期内稳定
```

CBind 只有在 target 是 borrowed binding 且 token lifetime 足够长时才允许 zero-copy borrow。

### 8.2 reader / writer

concept：

```c
cserde_status cserde_reader_next(cserde_reader *, cserde_token *out);
cserde_status cserde_reader_skip_value(cserde_reader *);

cserde_status cserde_writer_write(cserde_writer *, const cserde_token *token);
cserde_status cserde_writer_finish(cserde_writer *);
```

`skip_value` 必须正确跨过 nested arrays/maps，是 ignore unknown field 的基础。

## 9. JSON reference codec

JSON 是 CSerde 的第一个 concrete codec，只用于证明架构。

- reader 对 unescaped string 可直接返回 input slice；escaped string 若使用 scratch，则必须标记 transient；
- borrowed target 遇到 transient token 默认失败，不能保存 dangling pointer；
- writer 通过 caller sink callback 输出，不强依赖 `tstr`/`turbo_buffer`；
- JSON 无 native bytes，v1 对 `CSERDE_BYTES` 返回 unsupported；base64/hex 必须显式 extension；
- number parser 必须区分 SINT/UINT/FLOAT，不能全部先转 double。

writer sink concept：

```c
typedef cserde_status (*cserde_write_fn)(void *ctx,
                                         const void *data,
                                         size_t size);
```

## 10. CBind engine

公共入口 concept：

```c
cbind_status cbind_encode(const cmeta_data_desc *shape,
                          const void *object,
                          cserde_writer *writer,
                          const cbind_options *options,
                          cbind_error *error);

cbind_status cbind_decode(const cmeta_data_desc *shape,
                          cserde_reader *reader,
                          void *out,
                          const cbind_options *options,
                          cbind_error *error);
```

encode：

- scalar -> primitive token；
- enum -> policy-selected scalar；
- struct -> string-key MAP；
- sequence -> default Range -> ARRAY；
- map -> entries Range -> MAP；
- optional empty -> NULL；
- custom -> format-neutral adapter。

decode：

- validate token kind；
- precise numeric conversion；
- enforce string ownership/lifetime；
- struct field dispatch；
- sequence/map 通过 Collector 事务构造；
- failure destroy/reset 已构造字段并 abort container；
- success 后对象 fully initialized。

## 11. nested container instance binding

这是本设计必须补齐的 CMeta container contract。

顶层：

```c
Vec(int, values);
```

已经在声明时把 container descriptor + element type 放入 handle。但结构体字段通常只是：

```c
vec_t values;
```

zero-init 后没有 declaration-time element binding，generic CBind 无法直接调用 collector。

因此 `cmeta_container_desc` 需要 optional hook：

```c
typedef cmeta_status (*cmeta_container_bind_types_fn)(
    void *object,
    const cmeta_type_desc *const *args,
    size_t arity);
```

建议把 `bind_types` **追加在 descriptor 结构尾部**，避免改变现有字段顺序。现有 positional initializer 少写尾部成员在 C 中保持 zero-init；仓库内 descriptor 全部更新为显式完整初始化并增加 public-header regression。

语义：

- 只绑定 declaration-time type metadata；
- 不 malloc；
- 不改变 logical size；
- 对 live/initialized 或不兼容 handle 返回错误；
- unary container arity=1；associative arity=2；
- arity/type compatibility 由 container kind 验证。

nested sequence decode：

```text
bind_types(field, [int], 1)
  -> collector(field, limit)
  -> begin / accept... / finish
```

这不是 serialization 特例，而是补全 self-describing container 的 generic construction contract。

### 11.1 ABI 影响

给 public `cmeta_container_desc` 追加字段会改变 `sizeof(cmeta_container_desc)`，因此不是跨版本 binary ABI-neutral 变更。实现 PR 必须：

- 明确该版本的 ABI 变化；
- 只在结构尾部追加；
- 更新所有仓库内 descriptor；
- 增加 C/C++ initializer tests；
- 不声称旧 binary 与新 library 可混链。

如果项目在实现前要求冻结 `cmeta_container_desc` binary ABI，则应改为 companion extension descriptor；本设计当前选择尾部追加，因为结构仍处于主动演进期，且这是最小、最直接的 generic construction contract。

## 12. TurboSTL container binding

### 12.1 sequence encode

CBind 通过 `cmeta_container_descriptor(object)` 获取 DEFAULT Range，逐元素按 element semantic shape encode。

适用于 Vec、Deque、List、Stack、Queue、Heap、Set、HashSet 等任何有 default range 的 unary container。

Set/HashSet 不保证 canonical order；v1 不承诺 canonical JSON。

### 12.2 map encode

选择 ENTRIES Range，读取 `cmeta_entry`，根据 key/value semantic descriptor 输出 pair。

JSON writer 要求 key 最终是 STRING。非 string key 的 JSON map 默认 unsupported；array-of-pairs 只能作为显式 policy，禁止 silent fallback。

### 12.3 decode

CBind：

```text
bind_types
  -> collector
  -> begin
  -> decode child temp
  -> accept
  -> finish
```

任何 child decode / accept 失败都 abort collector。capacity/ownership/rollback 继续由 container 自己负责。

## 13. transaction 与 destination contract

v1 `cbind_decode` 定义为“构造新值”：

```text
precondition: out 处于 shape 定义的 empty state
success:      out fully initialized
failure:      out 回到 empty state
```

不隐式覆盖 live owned object。

后续可增加 `cbind_decode_replace`：temporary decode + move/destroy commit；只有 lifecycle 足够时开放。

## 14. ownership / lifecycle

不能通过裸 layout 推断 ownership。

semantic data ops 至少覆盖 prepare-empty/reset/read access；replace API 才要求 move/commit。能复用 `cmeta_type_traits` copy/move/destroy 时直接复用，但 serialization callback 不加入 traits。

`tstr`：owned decode，失败必须 free。

`vstr`：默认禁止 borrowed decode；只有 `allow_borrowed=true` 且 token view lifetime 足够时允许 zero-copy。

裸 pointer：默认 unsupported，只有显式 custom adapter 可绑定。

## 15. allocator 与资源限制

对不可信输入默认 bounded。`cbind_options` 至少包含：

```text
max_depth
max_string_bytes
max_container_elements
max_map_entries
max_total_input_bytes（codec 可提供时）
unknown_field_policy
duplicate_field_policy
allow_borrowed
```

container collector limit 取 schema/static limit 与 runtime global limit 的较小值。所有 count/size arithmetic 检查 overflow。JSON reader 自身也必须有 depth/token-size 限制，不能完全依赖 binder。

## 16. field policy 与 schema evolution

semantic shape 保存 canonical identity；CBind policy 管 external behavior。

v1 field policy：

```text
external_name   optional rename
aliases[]       decode-only historical names
required        missing -> error
optional/default
emit            encode 是否输出
```

默认：

```text
unknown field    -> error
duplicate field  -> error
missing required -> error
```

runtime options 可以放宽 unknown field 为 ignore。

v1 不自动注入 `_version`，不提供 migration graph。兼容演进依靠 additive optional field + aliases + explicit adapter。

## 17. enum policy

默认：

```text
encode -> text
decode -> text
```

可显式改为 symbol/integer/accept-text-or-symbol。numeric decode 默认要求 descriptor 中存在该值，除非 policy 明确允许 unknown numeric enum。

## 18. error model 与 path

CSerde error：syntax/transport，例如 invalid token、unexpected EOF、escape/UTF-8/number syntax、writer state、sink failure。

CBind error：semantic conversion，例如 type mismatch、numeric overflow、missing/unknown/duplicate field、unsupported map key、ownership/lifetime、container bind/collector failure、resource limit、custom adapter failure。

CBind error 必须保存 logical path，例如：

```text
user.addresses[3].zip
settings["timeout"]
```

内部使用 path segment stack，不要求每层动态拼字符串。JSON error 可额外携带 byte offset/line/column。

## 19. custom adapter

format-neutral adapter concept：

```c
typedef struct cbind_adapter {
    cbind_status (*encode)(const void *value,
                           cserde_writer *writer,
                           const cbind_context *ctx);
    cbind_status (*decode)(cserde_reader *reader,
                           void *out,
                           const cbind_context *ctx);
    void (*reset)(void *value);
} cbind_adapter;
```

adapter 只能看到 token reader/writer，不能 downcast 为 JSON implementation。UUID、timestamp、tstr、vstr 等可以跨 codec 复用。

## 20. deterministic / canonical output

v1 保证 struct fields 按 semantic descriptor 声明顺序 encode。

不保证：

- HashMap/HashSet 迭代顺序；
- JSON canonical key sorting；
- canonical float rendering；
- cryptographic canonical serialization。

需要签名/hash 的 canonical wire format 后续单独设计。

## 21. translation-unit identity

与现有 CMeta 一致：header-local descriptors 不要求跨 TU 地址相同。binding 比较 stable id/type identity/descriptor content，不用 pointer equality 作为跨 TU type identity。

公共 API 必须同时被 C11 和 C++ 消费。

## 22. 安全测试边界

必须覆盖：

- 深层 nested input；
- 巨大 string/array/map；
- size/count overflow；
- invalid UTF-8；
- signed/unsigned overflow；
- duplicate/unknown/missing field；
- unknown-field skip 跨 nested value；
- truncated input；
- collector abort；
- custom adapter rollback；
- borrowed lifetime mismatch；
- malicious map key type。

任何资源限制错误必须 deterministic，并保证 destination rollback。

## 23. TDD / 验证顺序

实现时按以下独立 PR/阶段推进：

1. CMeta semantic data descriptors；
2. `cmeta_container_desc.bind_types` contract；
3. CSerde token reader/writer + recording test codec；
4. CBind scalar/struct engine；
5. CBind sequence/map through Range/Collector；
6. ownership adapters (`tstr`/`vstr`)；
7. JSON reference codec；
8. field policy/evolution/error-path hardening；
9. fuzz/performance/full CI；
10. 最后再决定 `DataStruct(...)` DSL sugar。

关键原则：先用 recording codec 证明 CBind 与 JSON 无关，再实现 JSON。

最终验证至少包含：

- C/C++ public-header tests；
- CMeta/CSerde/CBind unit tests；
- nested TurboSTL integration；
- Linux + Windows fresh configure/build/test；
- sanitizer/fuzz target（环境允许时）。

## 24. 性能策略

核心路径禁止 mandatory DOM allocation。

benchmark：

```text
flat struct
nested struct + strings
large Vec
large Map
```

分别测 parse-only、bind decode、bind encode，并记录 allocation count、bytes copied、throughput。borrowed stable-view mode单独测量，不能替代 owned mode 指标。

## 25. Definition of Done

该架构只有在以下条件全部满足时才算实现完成：

- CMeta semantic shape 与 C layout reflection 分离；
- CSerde 不依赖 CBind/TurboSTL/CFlow；
- JSON 是 CSerde concrete codec，不是 CBind hard-coded branch；
- CBind 只依赖 CMeta + CSerde；
- nested TurboSTL container 不需要 CBind type switch/特判；
- container decode 走 `bind_types + Collector`；
- container encode 走 Range；
- owned/borrowed string lifetime 有可测试 contract；
- decode failure 回到 empty state；
- strict unknown/duplicate/missing/overflow/resource-limit behavior 有测试；
- C/C++ public API 编译通过；
- Linux + Windows fresh CI 通过；
- 没有 `Type x Format` generated facade；
- 没有 DOM 作为 mandatory core intermediate representation。

## 26. 决策摘要

```text
CMeta：知道“数据是什么”以及容器 generic construction contract
CSerde：知道“格式如何变成 token / token 如何写回格式”
CBind：知道“C 对象如何映射 token”
TurboSTL：知道“容器如何存储、遍历、事务构造”
CFlow：以后只知道“这些数据如何流动”
```

这使 serialization/data binding 成为 CMeta + TurboSTL 能力的自然延伸，而不是新的 generated facade 系统。
