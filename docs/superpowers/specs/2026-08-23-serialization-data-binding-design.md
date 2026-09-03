# CMeta 驱动的序列化与数据绑定设计

日期：2026-08-23
状态：Accepted architecture / incremental implementation（以当前头文件、专项设计与测试为准）

本文件是总体架构记录。generic、semantic projection、container construction、
CSerde 与初始 CBind decode 已分阶段落地；具体 API 以同目录的专项设计、
公开头文件及测试为事实源。

## 1. 背景

Salts 已经具备三块可以直接成为数据绑定基础的能力：

- CMeta 提供类型身份、traits、Enum/Struct 元数据、Range、Collector 和 container descriptor；
- Container 已完成 self-describing instance API，标准容器通过 `cmeta_container_desc` 暴露 Range/Collector；
- CFlow 能消费 Range/Collector，但它是 execution 层，不应成为序列化核心依赖。

同时，独立仓库 `qigao/turbo-parser` 已经存在成熟的 parser/data-binding runtime：

- JSON、YAML、XML、CSV 等 parser 已有 DOM/SAX 或增量 SAX 能力；
- JSON raw SAX 能保留 exact number token，并明确 callback view lifetime；
- DataBind 2.5 已经覆盖 runtime schema、动态值树、existing-C-struct descriptor、generated typed binding、streaming、query/path、ownership/error/ABI；
- TBE 已经验证 enum、optional、union、collection、typed descriptor ABI、schema fingerprint 和 binary wire layout 等契约。

TurboParser 的既有依赖方向是：

```text
application -> TurboParser -> Salts
```

因此本设计不能让 Salts 反向依赖 TurboParser，也不应该在 Salts 内重新实现一套 JSON/YAML/XML/CSV parser 或第二套 DataBind。

当前真正的缺口是：CMeta 的 `Struct(...)` 只描述 C ABI layout——字段名、类型文本、offset、size、align——并不描述 wire/data 语义。`vstr` 就是直接反例：其 ABI 是 `{ const char *data; size_t len; }`，但数据语义应当是一个 borrowed string view，而不是一个包含 pointer 和 length 的 object。

因此本设计严格区分：

```text
C layout reflection != semantic data shape != canonical data events != native format syntax/wire
```

另有现存模块 `salts_serial`，含义是 serial-port/串口。新的 serialization 模块不得使用 `serial` / `salts_serial` 名称，避免概念冲突。

## 2. 目标

1. 建立 format-neutral semantic data model，使同一份 C 数据绑定可服务 JSON、YAML、XML、CSV、CBOR、MessagePack、TOML 等格式。
2. 建立 streaming canonical token reader/writer contract，避免 DOM 成为强制中间表示。
3. 建立 C object <-> token 的通用 binding engine。
4. 直接复用 Container 已有 `Range + Collector + cmeta_container_desc`，不为每种容器生成 serialize facade。
5. 支持 nested struct、enum、variant/union、owned/borrowed string、sequence、map、optional 和 custom adapter。
6. decode 失败不留下半初始化对象或半构造容器。
7. 默认严格处理 unknown field、duplicate field、missing required field、numeric overflow 和资源限制。
8. ownership、reader view lifetime、allocator/limits 必须成为显式 contract。
9. 复用 TurboParser 已有 parser 作为生产 codec adapter；Salts core 不拥有重复 parser implementation。
10. 保留 DataBind 的 runtime/dynamic-value 价值，但逐步把公共 binding semantics 下沉到 CMeta/CSerde/CBind。
11. 保留 TBE 的专用 binary wire/layout 能力，不把 TBE wire metadata 污染到通用 CMeta semantic descriptor。
12. 为后续 CFlow streaming adapter 留边界，但 v1 不让 CFlow 进入核心依赖图。

## 3. 非目标

v1 不做：

- mandatory DOM/value tree；
- 在 Salts 中重写 JSON/YAML/XML/CSV parser；
- 删除 DataBind 动态对象/runtime-host 能力；
- 把 TBE fixed-prefix/group/var-data/wire-offset 设计泛化成通用 data shape；
- runtime network schema registry；
- 裸 pointer 自动序列化；
- 根据 `const char *` 猜测 string ownership；
- 将 serialize/deserialize callback 塞进 `cmeta_type_traits`；
- 将 JSON rename/base64/date policy 塞进 `cmeta_type_desc`；
- 自动 protobuf/IDL 生成；
- 自动 migration graph；
- cyclic object graph/reference identity；
- CFlow graph execution 集成；
- 将现有 `salts_serial` 改造成 serialization 模块。

## 4. 采用架构

采用：**CMeta semantic shape + CSerde canonical token contract + CBind independent binding + TurboParser format adapters**。

核心依赖关系：

```text
                    CMeta semantic shape
                           |
                           |
Container --implements--> CMeta container contract
                           |
                           v
                         CBind <---------------- CSerde token contract
                           ^                         ^
                           |                         |
                           |                 format projection/adapters
                           |                         ^
                           |                         |
                           |                    TurboParser
                           |              JSON/YAML/XML/CSV/...
                           |
                      application
```

等价 target 依赖：

```text
Container -> CMeta
CSerde   -> minimal runtime/C runtime only
CBind    -> CMeta + CSerde
TurboParser parser adapters -> Salts::CSerde
TurboParser::DataBind       -> parser adapters + Salts::CBind
CFlow    -> future adapter only
```

关键边界：

- CSerde core 定义 canonical data event protocol，不拥有 JSON/YAML/XML parser；
- TurboParser 把已有 native parser SAX/event model 投影成 CSerde reader；
- writer 方向由 TurboParser serializer state machine 消费 CSerde writer events；
- CBind 永远只消费 `cserde_reader` / `cserde_writer`，不直接 include/link parser；
- DataBind 可以继续提供动态 `DataBindValue`/`DataBindObject` façade，但 dynamic tree 只是 CBind 的一个可选 sink/source，不是 mandatory core intermediate representation。

拒绝 DOM-first 作为核心，因为它强制完整中间树和额外 allocation/traversal。拒绝 `Type x Format` 代码生成，因为会重新形成 Container 刚删除的 generated facade 笛卡尔积。

## 5. 模块与 ownership

### 5.1 CMeta

新增 format-neutral semantic descriptor，建议：

```text
cmeta/include/cmeta/data.h
```

CMeta 不解析 JSON，不分配 parser DOM，不包含 format-specific callback。

### 5.2 CSerde

Salts 新模块：

```text
cserde/
  include/cserde/token.h
  include/cserde/reader.h
  include/cserde/writer.h
  src/...
```

CMake target：

```text
Salts::CSerde
```

职责仅为：

```text
canonical token types
reader/writer protocol
view lifetime capabilities
skip/value-state helpers
writer sink abstraction
format-neutral syntax/event errors
```

**CSerde core 不包含 `json.h`，不实现 JSON parser。**

### 5.3 CBind

Salts 新模块：

```text
cbind/
  include/cbind/cbind.h
  include/cbind/policy.h
  include/cbind/error.h
  include/cbind/context.h
  src/...
```

CMake target：

```text
Salts::CBind
```

依赖：

```text
CBind -> CMeta + CSerde
```

CBind 不直接 link Container。任何实现 CMeta container contract 的容器都可以参与 binding。

### 5.4 TurboParser

TurboParser 继续拥有：

```text
JSON parser / JSONPath
YAML parser / YPath
XML parser / XPath
CSV parser / DSV filter
TOML / INI / other parser modules
TBE schema + binary wire
DataBind public runtime
```

并新增/重构 adapter：

```text
native parser events -> format projection -> cserde_reader
cserde_writer        -> native serializer state machine / byte sink
```

这使 parser 的 lexical/syntax 能力保持单一事实源。

### 5.5 DataBind

DataBind 不删除。它逐步从“parser + schema + value tree + typed binder 全部集中实现”收敛成：

```text
TurboParser::DataBind
  ├── runtime schema host
  ├── dynamic DataBindValue/DataBindObject sink/source
  ├── compatibility API
  ├── query/path integration
  └── delegates generic native-object binding to CBind
```

动态对象仍适合 runtime schema、脚本、插件、RulesForge/TurboScript 等不知道最终 C type 的场景。

### 5.6 TBE

TBE 保留为 specialized schema/wire layer：

```text
cmeta_data_desc      -> logical data semantics
TBE semantic bridge -> schema name/alias/optional/union/etc.
tbe_wire_desc        -> wire kind/offset/fixed block/presence/endian/group/var-data
```

通用 CMeta semantic model 不吸收 TBE 的 fixed-prefix、group、var-data、wire-offset、endianness 约束。

### 5.7 CFlow

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
    CMETA_DATA_VARIANT,
    CMETA_DATA_OPTIONAL,
    CMETA_DATA_SEQUENCE,
    CMETA_DATA_MAP,
    CMETA_DATA_CUSTOM
} cmeta_data_kind;
```

`VARIANT` 是 core kind：现有 TBE/DataBind 已经支持 union/tagged value，新 semantic foundation 不能退化成只支持 struct/container。

concept descriptor：

```c
typedef struct cmeta_data_desc {
    size_t struct_size;
    uint32_t abi_version;
    const char *stable_id;
    const char *display_name;
    cmeta_data_kind kind;
    const cmeta_type_desc *storage_type;
    const void *shape;
    const struct cmeta_data_ops *ops;
} cmeta_data_desc;
```

`storage_type` 描述 C storage；`shape` 指向 kind-specific immutable descriptor；`ops` 只负责 generic lifecycle/access，不负责 JSON/CBOR/TBE wire。

公共 descriptor 从 v1 起采用 `struct_size + abi_version`，避免再次形成无版本跨模块 ABI。

### 6.1 scalar

整数 descriptor 必须记录 signedness 和 width。decode 进行精确范围检查，禁止 silent cast/truncation。

float descriptor 记录 storage width。non-finite/overflow 等转换由 CBind numeric policy 控制。

UUID、datetime、date、time、duration、money 等不全部升级成 core enum kind；在
其值能够无损映射到现有 string/bytes/integer token 时，可通过 logical/custom
scalar adapter 表达，避免无限扩大基础 kind。

Decimal/BigInt 不适用这一捷径：v1 CSerde 只有 `int64_t`、`uint64_t` 与
`double` 数值域，custom adapter 看到的也是 canonical token，而不是 parser
原始数字 lexeme。因此任意精度 Decimal/BigInt 在 exact-number token 扩展落地前
必须显式返回 unsupported；不得先转成 `double`、截断或舍入后再交给 adapter。

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

canonical semantic field name 属于 data shape；external rename/aliases/required/default/emit policy 属于 CBind binding policy 或 schema bridge，不放进 `cmeta_struct_desc`。

### 6.5 variant

variant shape 至少描述：

```text
tag semantic descriptor
alternatives[]
active-tag <-> alternative mapping
storage access/lifecycle
```

CMeta 只描述 tagged semantic value；JSON/TBE 等格式如何表现 discriminator 由 CBind policy / format adapter 决定。

v1 case-tag ABI 是 `int64_t`。tag descriptor 可以是 `UINT`，但 schema case tag
与 runtime discriminator 均只支持 `[0, INT64_MAX]`；更大的无符号值必须在
lookup 前 fail fast，禁止 cast/truncation。未来若需要完整 `uint64_t` tag 域，
必须新增带版本的 tag-value 表示，不能重新解释现有 `int64_t` 字段。

### 6.6 sequence / map

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

## 8. schema identity 与 descriptor identity

`stable_id` 解决逻辑名称/跨 TU identity，但不足以判断 schema compatibility：

```text
Order v1
Order v2
另一个不兼容的 Order
```

不能仅靠 `stable_id == "Order"` 判断相同 schema。

因此区分：

```text
stable_id              logical identity
semantic fingerprint   immutable semantic graph identity
format/wire fingerprint optional specialized schema/wire identity
```

要求：

- header-local descriptor 不要求跨 TU 地址相同；
- pointer equality 不作为跨 TU / 跨 module type identity；
- dynamic owning object 可以保存 semantic/schema fingerprint；
- serialize/replace/bridge 时发现不兼容 fingerprint 必须显式拒绝；
- fingerprint 算法/版本必须带 domain/version，不能依赖内存地址。

这继承现有 DataBindObject 的 schema fingerprint + mismatch rejection 语义，但把它推广成通用 identity contract。

## 9. CSerde canonical token model

CSerde 使用 pull reader + push writer，共享 format-neutral canonical data events：

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

v1 数值 token 的可表示域严格为：

```text
CSERDE_SINT  -> int64_t
CSERDE_UINT  -> uint64_t
CSERDE_FLOAT -> double
```

超出该域的 parser 数值必须 fail fast 为 unsupported/overflow。后续若支持
Decimal/BigInt，应新增带版本的 exact-number token（例如规范化的
sign/digits/exponent 视图或等价表示）并明确 view lifetime；不得改变既有三种
token 的含义。

不定义 JSON-specific OBJECT。struct/object 在 canonical token 层就是 string-key map：

```text
MAP_BEGIN
STRING("id")
SINT(7)
STRING("name")
STRING("alice")
MAP_END
```

这样 CBOR/MessagePack 可表达 arbitrary-key map；具体 format adapter 自己施加原生格式限制。

**这些 token 不是所有 parser 的 native AST/SAX event model。** 它们是供 binder 使用的 canonical data event model。

### 9.1 payload lifetime

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

### 9.2 reader / writer

concept：

```c
cserde_status cserde_reader_next(cserde_reader *, cserde_token *out);
cserde_status cserde_reader_skip_value(cserde_reader *);

cserde_status cserde_writer_write(cserde_writer *, const cserde_token *token);
cserde_status cserde_writer_finish(cserde_writer *);
```

`skip_value` 必须正确跨过 nested arrays/maps，是 ignore unknown field 的基础。

writer sink：

```c
typedef cserde_status (*cserde_write_fn)(void *ctx,
                                         const void *data,
                                         size_t size);
```

## 10. native format projection

JSON、YAML、XML、CSV 等 native event model 不完全同构。统一点发生在 **projection 后**，不是 parser lexer/SAX 层。

统一 pipeline：

```text
native parser events
        ↓
format projection
        ↓
CSerde canonical tokens
        ↓
CBind
```

### 10.1 JSON

JSON object/array/scalar 基本与 canonical map/array/scalar 一一对应。现有 raw
SAX number token 可先按 lexeme 判定：落在 v1 数值域内时精确分类成
SINT/UINT/FLOAT；超出该域时显式 unsupported，而不是先转 `double`。

### 10.2 YAML

YAML 原生具有：

```text
alias/tag/non-string key/duplicate mapping key/document boundaries
```

projection 必须明确哪些语义：

- 可无损映射为 canonical token；
- 需要 policy；
- 不可无损映射时返回 unsupported/type error。

不得先强转 JSON DOM 再声称无损绑定。

### 10.3 XML

XML 原生具有：

```text
element
attribute
text/CDATA
namespace
processing instruction
```

XML adapter 需要明确 schema/binding projection，例如 field 优先来自 child element、attribute 或显式 mapping。CSerde core 不增加 XML-specific token。

### 10.4 CSV

CSV 原生是 header/row/column。adapter 根据 header/schema projection 把一行投影为 map-like canonical record；nested dotted/indexed column 规则属于 CSV binding projection，不属于 CSerde core。

## 11. TurboParser reference adapters

JSON 是第一个 production reference adapter，但**不在 Salts 重写 JSON parser**。

现有 TurboParser JSON 已具备：

```text
DOM
one-shot SAX
incremental SAX
raw-number SAX
streamable JSONPath selected-subtree events
```

reader adapter 直接建立在现有 incremental raw SAX 上，并保持 callback/string/number view lifetime contract。

JSON adapter 要求：

- raw number 在 v1 数值域内分类为 SINT/UINT/FLOAT，超域显式 unsupported，禁止先 double 再恢复整数；
- escaped string 若需要 scratch，token capability 标记为 TRANSIENT；
- borrowed target 遇到 transient token 默认失败；
- JSON 无 native bytes，`CSERDE_BYTES` 默认 unsupported；base64/hex 必须显式 extension；
- syntax/depth/token-size limits 继续由 parser 自己先执行。

writer 方向优先把现有 TurboParser JSON serializer 的状态机抽成 token consumer，而不是新建第二个 serializer implementation：

```text
CBind -> cserde_writer -> TurboParser JSON writer state machine -> caller byte sink
```

YAML/XML/CSV adapters 在 JSON adapter + CBind contract 稳定后逐步迁移。

## 12. CBind engine

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
- variant -> policy-described tagged value；
- sequence -> default Range -> ARRAY；
- map -> entries Range -> MAP；
- optional empty -> NULL；
- custom -> format-neutral adapter。

decode：

- validate token kind；
- precise numeric conversion；
- enforce string ownership/lifetime；
- struct field dispatch；
- variant tag/alternative validation；
- sequence/map 通过 Collector 事务构造；
- failure destroy/reset 已构造字段并 abort container；
- success 后对象 fully initialized。

## 13. nested container instance binding

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

### 13.1 versioned construction extension

不把未来所有 construction hook 平铺追加到 `cmeta_container_desc`。定义 versioned extension：

```c
typedef struct cmeta_container_construct_ops {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_container_desc *descriptor;
    cmeta_status (*bind_types)(
        void *object,
        const cmeta_type_desc *const *args,
        size_t arity);
    void (*restore_zero)(void *object);
} cmeta_container_construct_ops;
```

`cmeta_container_desc` 只追加一次 `ext` pointer；construction 继续扩展该
versioned root：

```c
typedef struct cmeta_container_ext {
    size_t struct_size;
    uint32_t abi_version;
    const cmeta_container_type_ops *type;
    const cmeta_data_desc *data;
    const cmeta_container_construct_ops *construction;
} cmeta_container_ext;
```

未来若 construction contract 需要 `prepare`、`capacity_hint`、`move_commit` 等能力，优先扩展 versioned construct ops，而不是重复改变主 descriptor layout。

`bind_types` 语义：

- 只绑定 declaration-time type metadata；
- 不 malloc；
- 不改变 logical size；
- 对 live/initialized 或不兼容 handle 返回错误；
- unary container arity=1；associative arity=2；
- arity/type compatibility 由 container kind 验证。

nested sequence decode：

```text
cmeta_container_bind_types(field, reflected_field->declared_type)
  -> collector(field, limit)
  -> begin / accept... / finish
```

这不是 serialization 特例，而是补全 self-describing container 的 generic construction contract。

### 13.2 ABI 影响

当前实现没有再次增长 `cmeta_container_desc`；它在既有 versioned
`cmeta_container_ext` 尾部追加 optional `construction` pointer。

实现 PR 必须：

- 用 `struct_size` 做真实 prefix-size 检查；
- 只在 extension 尾部追加 `construction`；
- construction ops 自身从 v1 起 versioned；
- 更新所有仓库内 descriptor；
- 增加 C/C++ initializer/public-header regression；
- 保证旧 extension prefix 仍可读取，且缺少该 optional tail 时明确返回 unsupported。

## 14. Container container binding

### 14.1 sequence encode

CBind 通过 `cmeta_container_descriptor(object)` 获取 DEFAULT Range，逐元素按 element semantic shape encode。

适用于 Vec、Deque、List、Stack、Queue、Heap、Set、HashSet 等任何有 default range 的 unary container。

Set/HashSet 不保证 canonical order；v1 不承诺 canonical JSON。

### 14.2 map encode

选择 ENTRIES Range，读取 `cmeta_entry`，根据 key/value semantic descriptor 输出 pair。

JSON writer 要求 key 最终可表示为 STRING。非 string key 的 JSON map 默认 unsupported；array-of-pairs 只能作为显式 policy，禁止 silent fallback。

### 14.3 decode

CBind：

```text
cmeta_container_bind_types(object, declared_type)
  -> collector
  -> begin
  -> decode child temp
  -> accept
  -> finish
```

任何 child decode / accept 失败都 abort collector。capacity/ownership/rollback 继续由 container 自己负责。

## 15. transaction 与 destination contract

现有 DataBind typed parse 已有重要契约：parse 失败时 previous object remains unchanged。新 CBind 不应弱化这个行为。

v1 基础 `cbind_decode` 定义为“构造新值”：

```text
precondition: out 处于 shape 定义的 empty state
success:      out fully initialized
failure:      out 回到 empty state
```

不隐式覆盖 live owned object。

随后提供 `cbind_decode_replace` 时必须满足：

```text
decode into temporary
  -> full validation
  -> move/commit
  -> destroy previous value
```

失败时旧对象保持不变；只有 lifecycle/move contract 足够时开放 replace。

## 16. ownership / lifecycle

不能通过裸 layout 推断 ownership。

semantic data ops 至少覆盖 prepare-empty/reset/read access；replace API 才要求 move/commit。能复用 `cmeta_type_traits` copy/move/destroy 时直接复用，但 serialization callback 不加入 traits。

`tstr`：owned decode，失败必须 free。

`vstr`：默认禁止 borrowed decode；只有 `allow_borrowed=true` 且 token view lifetime 足够时允许 zero-copy。

裸 pointer：默认 unsupported，只有显式 custom adapter 可绑定。

动态 `DataBindValue`/`DataBindObject` 是 owning sink/source，生命周期保持由 DataBind API 管理，不把内部 allocation ownership 泄露给 CBind caller。

## 17. allocator 与资源限制

对不可信输入默认 bounded。allocator/pool policy 必须属于 **per-bind/per-context** contract，不能继承现有 DataBind process-global value-pool policy作为通用 CBind 行为。

concept：

```text
cbind_context
  allocator
  max_depth
  max_string_bytes
  max_container_elements
  max_map_entries
  max_total_input_bytes（reader 可提供时）
  unknown_field_policy
  duplicate_field_policy
  allow_borrowed
```

要求：

- allocator 的 alloc/free 必须成对属于同一 context；
- reusable library 不改变 process-global allocation policy；
- container collector limit 取 schema/static limit 与 runtime context limit 的较小值；
- 所有 count/size arithmetic 检查 overflow；
- native parser 自己也必须有 depth/token-size/input limits，不能完全依赖 binder。

DataBind 可以在 compatibility layer 内继续维护自己的 value pooling，但它不得成为 CBind core 的全局 allocator contract。

## 18. field policy 与 schema evolution

semantic shape 保存 canonical identity；CBind binding policy 管 external behavior。

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

v1 不自动注入 `_version`，不提供 migration graph。兼容演进依靠 additive optional field + aliases + explicit adapter，并通过 semantic/schema fingerprint 防止错误地把同名不兼容 shape 当成相同 schema。

现有 TBE `[name]` / `[alias]` / optional/default 语义应通过 schema bridge 映射到这套 policy，而不是复制一套不同规则。

## 19. enum policy

默认：

```text
encode -> text
decode -> text
```

可显式改为 symbol/integer/accept-text-or-symbol。numeric decode 默认要求 descriptor 中存在该值，除非 policy 明确允许 unknown numeric enum。

## 20. error model 与 path

CSerde error：canonical reader/writer/state/transport，例如 invalid token、unexpected EOF、writer state、sink failure、unsupported projection。

TurboParser native parser error：syntax/lexical/location，例如 escape/UTF-8/number syntax、YAML indentation/alias、XML syntax、CSV quoting；adapter 映射到 CSerde error，并保留原生 offset/line/column/location diagnostic。

CBind error：semantic conversion，例如 type mismatch、numeric overflow、missing/unknown/duplicate field、unsupported map key、ownership/lifetime、container bind/collector failure、schema fingerprint mismatch、resource limit、custom adapter failure。

CBind error 必须保存 logical path，例如：

```text
user.addresses[3].zip
settings["timeout"]
```

内部使用 path segment stack，不要求每层动态拼字符串。

## 21. custom adapter

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

adapter 只能看到 token reader/writer，不能 downcast 为 JSON/YAML/TBE implementation。UUID、timestamp、tstr、vstr 等可以跨 codec 复用。

## 22. DataBind / TBE descriptor migration

现有 `TbeTypedField` / `TbeTypedType` / `TbeTypedDescriptor` 已经包含 CBind 所需大量语义：

```text
field name
offset
logical kind
nested object
list/set/map
element type
optional bit
```

但它们同时包含 TBE wire metadata：

```text
wire_kind
element_wire_kind
wire_offset
wire_size
fixed_block_size
presence bitmap
wire_big_endian
group/var-data
```

迁移原则：

```text
TbeTyped semantic subset -> cmeta_data_desc / binding policy
dedicated TBE metadata   -> tbe_wire_desc
```

最终 generated/existing-C-struct DataBind typed binding 应复用通用 semantic descriptor，而 TBE binary encode/decode 再额外消费 `tbe_wire_desc`。

DataBind 动态 value tree 则成为：

```text
CSerde -> CBind -> DataBindValue sink
DataBindValue source -> CBind -> CSerde
```

它不再是 native-object binding 的强制中间树。

## 23. deterministic / canonical output

v1 保证 struct fields 按 semantic descriptor 声明顺序 encode。

不保证：

- HashMap/HashSet 迭代顺序；
- JSON canonical key sorting；
- canonical float rendering；
- cryptographic canonical serialization。

需要签名/hash 的 canonical wire format 后续单独设计。

## 24. translation-unit / module identity

与现有 CMeta 一致：header-local descriptors 不要求跨 TU 地址相同。binding 比较 stable id + versioned descriptor content/fingerprint，不用 pointer equality 作为跨 TU type identity。

公共 descriptor/API 必须同时被 C11 和 C++ 消费。

跨 shared-library/module 使用 descriptor 时先检查 `struct_size` / `abi_version`；跨 schema object/codec 使用时检查 semantic/schema fingerprint。

## 25. 安全测试边界

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
- Decimal/BigInt 超出 v1 token 数值域时显式 unsupported，且不得舍入；
- borrowed lifetime mismatch；
- malicious map key type；
- variant invalid discriminator；
- schema fingerprint mismatch；
- YAML alias/non-string-key projection failure；
- XML attribute/element projection ambiguity；
- CSV nested-column projection errors。

任何资源限制错误必须 deterministic，并保证 destination rollback。

## 26. 迁移与 TDD / 验证顺序

实现按小 PR 推进，不一次替换 DataBind。

### Phase 0：TurboParser 基线同步

`turbo-parser/main` 当前 `tbe_typed.h` 仍使用旧 Container public surface（例如旧 `salts_vec_t` / `salts_stl_status` / `salts_vec_*`）。在 DataBind/CMeta integration 前先迁移到最新 natural Container API：

```text
vec_t
stl_status
vec_* / natural instance API
```

此阶段只做 dependency/API baseline migration，不混 serialization architecture。

### Core PRs

1. CMeta semantic data descriptors + versioned ABI + VARIANT；
2. `cmeta_container_ext.construction` + `cmeta_container_bind_types(object, declared)`；
3. CSerde canonical token reader/writer + recording test codec；
4. CBind scalar/struct/variant engine；
5. CBind sequence/map through Range/Collector；
6. ownership adapters (`tstr`/`vstr`) + per-context allocator/limits；
7. schema identity/fingerprint + field policy/evolution/error-path hardening。

### TurboParser integration PRs

8. JSON incremental raw-SAX -> CSerde reader adapter；
9. JSON token writer by refactoring/reusing existing serializer state machine；
10. DataBind native typed JSON path delegates to CBind without mandatory `DataBindValue` tree；
11. DataBind dynamic `DataBindValue` sink/source adapter；
12. migrate `TbeTyped*` semantic subset to CMeta descriptors, keep TBE wire descriptor separate；
13. YAML/XML/CSV projections/adapters one format at a time；
14. fuzz/performance/full cross-repo CI；
15. 最后再决定 `DataStruct(...)` DSL sugar。

关键原则：先用 recording codec 证明 CBind 与任何 concrete format 无关；生产 JSON 验证复用 TurboParser 已有 parser，而不是在 Salts 重写 parser。

最终验证至少包含：

- C/C++ public-header tests；
- CMeta/CSerde/CBind unit tests；
- nested Container integration；
- TurboParser JSON adapter parity tests；
- DataBind existing behavioral/ABI regression tests；
- TBE binary wire regression tests；
- Linux + Windows fresh configure/build/test；
- sanitizer/fuzz target（环境允许时）。

## 27. 性能策略

核心 native-object 路径禁止 mandatory DOM/DataBindValue allocation。

benchmark：

```text
flat struct
nested struct + strings
large Vec
large Map
dynamic DataBindValue sink
```

分别测：

```text
native parser parse-only
parser -> CSerde projection
CBind decode direct-to-object
legacy/current DataBind path
CBind encode -> writer
```

记录 allocation count、bytes copied、throughput、peak retained bytes。borrowed stable-view mode单独测量，不能替代 owned mode 指标。

迁移的性能目标首先是：direct native-object path 不因新增抽象显著退化，并消除可避免的 mandatory intermediate tree allocation；不以未经 benchmark 的“zero allocation”宣传替代测量。

## 28. compatibility / migration policy

DataBind 2.5 已经有 public ABI、ownership、stream/query 语义，迁移不能借“新架构”无理由破坏它们。

必须保留或显式版本化的行为包括：

- typed parse failure 不修改已有目标对象；
- owning `DataBindValue` / `DataBindObject` / record view lifetime；
- schema mismatch rejection；
- format/query diagnostics；
- configured stream limits/cancel semantics；
- generated/existing-C-struct typed routes；
- TBE binary wire compatibility。

允许的内部替换：

```text
parser-specific binding logic -> CSerde adapter + CBind
mandatory DataBindValue tree   -> direct target binding where possible
TbeTyped duplicated semantic metadata -> CMeta semantic descriptor
```

不要求一次删除所有 compatibility entry；先把它们变成新 core 上的薄 façade，再单独决定长期 public API 收口。

## 29. Definition of Done

该架构只有在以下条件全部满足时才算实现完成：

- CMeta semantic shape 与 C layout reflection 分离；
- semantic model 覆盖 variant/union，不比现有 DataBind/TBE 表达力倒退；
- CSerde core 不依赖 CBind/Container/CFlow/TurboParser；
- v1 CSerde 数值域只包含 `int64_t`/`uint64_t`/`double`，超域 Decimal/BigInt 在 versioned exact-number token 落地前显式拒绝；
- Salts 不包含重复 JSON/YAML/XML/CSV parser implementation；
- TurboParser concrete parser 通过 format projection/adapter 实现 CSerde reader/writer；
- CBind 只依赖 CMeta + CSerde；
- nested Container container 不需要 CBind type switch/特判；
- container decode 走 versioned construction extension + Collector；
- container encode 走 Range；
- owned/borrowed string lifetime 有可测试 contract；
- allocator/limits 是 per-context contract，不需要 process-global binding policy；
- decode failure 回到 empty state，replace failure 保留旧对象；
- strict unknown/duplicate/missing/overflow/resource-limit behavior 有测试；
- semantic/schema fingerprint mismatch 有测试；
- DataBind dynamic value tree 是 optional sink/source，不是 native binding mandatory intermediate；
- TBE semantic metadata 与 binary wire/layout metadata 分层；
- DataBind 关键 public behavior/ABI contract 有回归验证；
- C/C++ public API 编译通过；
- Linux + Windows fresh CI 通过；
- cross-repo Salts -> TurboParser integration CI 通过；
- 没有 `Type x Format` generated facade；
- 没有 DOM/DataBindValue 作为 mandatory core intermediate representation。

## 30. 决策摘要

```text
CMeta：知道“数据是什么”、如何识别 semantic identity，以及容器 generic construction contract
CSerde：定义“binder 可消费/产出的 canonical data events”，不知道 JSON/YAML/XML parser
CBind：知道“C 对象如何映射 canonical events”，并管理 transaction/ownership/policy/context
Container：知道“容器如何存储、遍历、事务构造”
TurboParser：知道“具体格式如何 parse/emit，并把 native events 投影到 CSerde”
DataBind：保留 runtime schema/dynamic-value/compatibility host，逐步委托 generic binding 给 CBind
TBE：保留 specialized binary wire/layout，在 semantic CMeta 之上增加 wire descriptor
CFlow：以后只知道“这些数据如何流动”
```

这使 serialization/data binding 成为 CMeta + Container 能力的自然延伸，同时直接复用 TurboParser 已经成熟的 parser/DataBind/TBE 契约，而不是在 Salts 内建立第二套 generated facade、parser 或 DOM-first binding 系统。
