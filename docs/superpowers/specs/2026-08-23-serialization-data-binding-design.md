# CMeta 驱动的序列化与数据绑定设计

日期：2026-08-23
状态：Design / Draft

## 1. 背景

TurboUtils 已经具备三块可以直接成为数据绑定基础的能力：

- CMeta 提供类型身份、traits、Enum/Struct 元数据、Range、Collector 和 container descriptor；
- TurboSTL 已完成 self-describing instance API，标准容器通过 `cmeta_container_desc` 暴露 Range/Collector；
- CFlow 能消费 Range/Collector，但它是 execution 层，不应成为序列化核心依赖。

与此同时，当前 CMeta 的 `Struct(...)` 只描述 C ABI layout：字段名、字段类型文本、offset、size、align。它并不知道字段的“数据语义”。例如 `vstr` 的 ABI 是 `{ const char *data; size_t len; }`，但它的 wire 语义应当是一个 borrowed string view，而不是一个包含 pointer 和 length 的 object。类似地，`tstr`、TurboSTL 容器、optional/handle 等类型也不能仅靠 C layout 推导出正确的序列化和 ownership 行为。

因此本设计明确区分：

```text
C layout reflection != semantic data shape != wire format
```

序列化系统必须建立在 format-neutral semantic shape 上，而不能把 JSON、字段别名或 owned/borrowed 行为塞进现有 `cmeta_type_traits` 或 `Struct(...)` layout descriptor。

另有现存模块 `turbo_serial`，其含义是 serial-port/串口。新的 serialization 模块不得使用 `serial`/`turbo_serial` 名称，避免概念和目标名称冲突。

## 2. 目标

1. 建立 format-neutral 的 semantic data model，使同一份 C 数据绑定可以服务 JSON、CBOR、MessagePack、TOML 等 codec。
2. 建立 streaming token reader/writer contract，避免 DOM 作为强制中间表示。
3. 建立 object <-> token 的通用 data-binding engine。
4. 直接复用 TurboSTL 已有 `Range + Collector + cmeta_container_desc`，不为每种容器重新生成序列化 facade。
5. 支持 nested struct、enum、owned/borrowed string、sequence、map、optional/custom adapter。
6. decode 具备事务式错误语义：失败不得留下半初始化对象或半构造容器。
7. 默认严格处理 unknown field、duplicate field、missing required field、number overflow 和资源限制。
8. 将 ownership、reader view lifetime、allocator/limits 作为显式 contract，而不是隐式约定。
9. JSON 作为第一个 reference codec，但核心设计不得依赖 JSON 语法或 JSON object model。
10. 为后续 CFlow streaming adapter 留出边界，但 v1 不让 CFlow 进入核心依赖图。

## 3. 非目标

v1 不做以下事情：

- 不实现完整 DOM/value tree 作为公共核心抽象；
- 不提供 runtime schema discovery/network registry；
- 不自动序列化任意裸 pointer；
- 不根据 `const char *` 猜测 string ownership；
- 不把 serialize/deserialize callback 加进 `cmeta_type_traits`；
- 不把 JSON 字段名、base64、日期格式等 format policy 加进 `cmeta_type_desc`；
- 不自动生成 protobuf/IDL；
- 不承诺跨版本自动 migration；
- 不实现 cyclic object graph/reference identity；
- 不在 v1 进行 CFlow graph execution 集成；
- 不把现有 `turbo_serial` 改造成 serialization 模块。

## 4. 方案比较

### 4.1 方案 A：semantic shape + streaming token + independent binding（采用）

```text
CMeta semantic shape
        |
        +------------------+
        |                  |
      CSerde            TurboSTL
   token reader/writer   Range/Collector
        |                  |
        +--------+---------+
                 |
               CBind
                 |
         JSON / binary / ...
```

CMeta 只描述数据语义；CSerde 只描述 token I/O；CBind 负责 C object 与 token 之间的递归绑定。TurboSTL 通过已经存在的 container descriptor 接入。

优点：format-neutral、streaming、可复用现有容器协议、无生成 facade 膨胀、依赖方向清晰。

代价：需要先补 semantic shape 和 nested-container instance binding 两个基础层。

### 4.2 方案 B：DOM-first

JSON 先解析成 `Value/Object/Array` 树，再绑定 C object。

优点是实现和调试简单；缺点是强制完整中间树、额外 allocation 和 traversal，且以后 binary/streaming codec 仍需要再拆一层 token API。DOM 可以以后作为 CSerde 的一个可选 consumer/provider，但不作为核心架构。

### 4.3 方案 C：每个 Type x Format 生成 encode/decode

例如为 `User x JSON`、`User x CBOR` 分别生成函数。

虽然可以获得很强的 static specialization，但会快速形成 `type x format x container` 笛卡尔积，重新制造 TurboSTL 刚删除的 generated facade surface，因此拒绝作为主架构。

## 5. 模块边界与目标名称

### 5.1 CMeta

CMeta 增加 format-neutral semantic data descriptor，建议公共头：

```text
cmeta/include/cmeta/data.h
```

它不解析 JSON，不分配 JSON DOM，不包含 codec callback。

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

职责只有 lexical/syntactic codec 和 token stream。

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

CBind 不直接依赖 TurboSTL。只要一个容器实现 CMeta container contract，它就可以参与 binding。

### 5.4 TurboSTL

TurboSTL 保持容器算法 ownership。它不增加 `vec_to_json()`、`map_from_json()` 一类 API。

必要的唯一基础扩展是：container descriptor 提供“绑定实例类型参数但不分配”的 prepare hook，使 nested container field 可以被通用 binder 构造。

### 5.5 CFlow

未来可提供：

```text
CSerde reader -> CFlow source
CFlow stream   -> CSerde writer sink
```

但这是 adapter 层。CSerde/CBind 不依赖 CFlow。

## 6. CMeta semantic data model

### 6.1 data kind

建议最小公共 kind：

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

它描述 semantic shape，而不是 C compiler type kind。

### 6.2 descriptor

概念接口：

```c
typedef struct cmeta_data_desc cmeta_data_desc;

typedef struct cmeta_data_desc {
    const char *stable_id;
    const char *display_name;
    cmeta_data_kind kind;
    const cmeta_type_desc *storage_type;
    const void *shape;
    const struct cmeta_data_ops *ops;
} cmeta_data_desc;
```

`storage_type` 说明 C storage 类型；`shape` 指向 kind-specific immutable descriptor；`ops` 只负责 generic data lifecycle/access，不负责 JSON/CBOR。

### 6.3 scalar

整数 descriptor 必须记录 signedness/width，decode 时做精确范围检查。禁止通过 `memcpy` 或 C cast 静默截断。

float descriptor 记录 storage width；JSON number -> float 的具体可表示性策略由 CBind numeric policy 决定，默认拒绝明显 overflow/non-finite mismatch。

### 6.4 string / bytes

string/bytes 不是 pointer reflection，而是独立 semantic scalar。

至少区分：

```text
owned
borrowed
fixed-buffer/custom
```

`tstr` 应绑定为 owned string adapter；`vstr` 应绑定为 borrowed string-view adapter。`vstr` 的 C layout 仍由 `Struct(vstr, ...)` 描述 ABI，但其 semantic data descriptor 是 STRING，而不是 STRUCT。

### 6.5 enum

semantic enum shape 引用现有 `cmeta_enum_desc`。CBind policy 决定外部 representation：text/symbol/integer。

默认人类可读 binding 建议使用 enum `text`；必须保留显式 numeric representation 选项供 compact/binary codec 使用。

### 6.6 struct

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

这里的 `name` 是 canonical semantic field name。rename aliases、required/default 等 binding policy 不放进 layout descriptor。

### 6.7 sequence / map

sequence shape 记录 element semantic descriptor；map shape 记录 key/value semantic descriptor。

它们不硬编码 `vec_t`/`map_t`。运行时 container instance 仍由 `cmeta_container_desc` 提供 Range/Collector。

## 7. C layout 与 semantic declaration DSL

低层 descriptor API 是第一事实源，宏只是 sugar。

对新数据模型，目标 DSL 可以是 single-source：

```c
DataStruct(User,
    (int,   id,   Int),
    (tstr,  name, StringOwned),
    (vec_t, tags, Sequence(Int))
);
```

它可以同时生成：

- C struct layout；
- `cmeta_struct_desc`；
- `cmeta_data_desc`；
- field semantic descriptors。

但 v1 实现不应先从复杂宏开始。先建立低层 descriptors + tests，确认语义后再决定最终 sugar。

对已有 `Struct(...)` 类型，允许 sidecar semantic binding；对 `vstr` 这类 layout 与 wire shape 不同的类型，允许 whole-type scalar/custom adapter 覆盖 field recursion。

原则：

```text
reflection describes memory;
binding describes meaning.
```

## 8. CSerde token model

CSerde 使用 pull reader + push writer，共享同一组 format-neutral token：

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

不单独定义 JSON OBJECT。struct/object 在 token 层就是 string-key map：

```text
MAP_BEGIN
STRING("id")
SINT(7)
STRING("name")
STRING("alice")
MAP_END
```

这使 CBOR/MessagePack 的 arbitrary-key map 能保持通用表达；JSON codec 只需要施加“map key 必须可表示为 string”的自身限制。

### 8.1 token payload lifetime

string/bytes token 使用 view：

```c
typedef struct cserde_slice {
    const unsigned char *data;
    size_t size;
} cserde_slice;
```

reader 必须公布 capability：

```text
TRANSIENT_VIEWS: payload 最迟在下一次 next() 后失效
STABLE_VIEWS: payload 在 reader backing buffer 生命周期内稳定
```

CBind 只有在 target 是 borrowed binding 且 reader 声明 STABLE_VIEWS 时，才允许 zero-copy borrow。

### 8.2 reader

concept：

```c
cserde_status cserde_reader_next(cserde_reader *, cserde_token *out);
cserde_status cserde_reader_skip_value(cserde_reader *);
```

`skip_value` 是 ignore unknown fields 的基础能力，必须保证正确跨过 nested array/map。

### 8.3 writer

concept：

```c
cserde_status cserde_writer_write(cserde_writer *, const cserde_token *token);
cserde_status cserde_writer_finish(cserde_writer *);
```

writer 自己负责 JSON comma/escaping、binary framing 等 syntax state。

## 9. JSON reference codec

JSON 是第一个 reference codec，只验证架构，不定义核心数据模型。

### 9.1 reader

v1 reader 可以直接在 immutable input buffer 上解析，并返回 stable string slices；escaped string 必须进入 scratch/owned storage，因此该 token 可能只能是 transient view。capability 必须反映真实 lifetime，不能因为多数 token zero-copy 就整体宣称 stable。

更安全的设计是 capability 可按 token 标记 lifetime，或 reader 提供 `pin/copy`。v1 若不实现 pin，则 borrowed decode 遇到 transient string 必须失败，而不是静默保存 dangling pointer。

### 9.2 writer

writer 通过 caller sink callback 输出：

```c
typedef cserde_status (*cserde_write_fn)(void *ctx,
                                         const void *data,
                                         size_t size);
```

这样 JSON writer 不强依赖 `tstr`/`turbo_buffer`。

### 9.3 JSON bytes

JSON 没有 native bytes。v1 core JSON codec 对 `CSERDE_BYTES` 返回 unsupported；base64/hex mapping 留给显式 extension policy，不在 core 中隐式猜测。

### 9.4 JSON number

reader 必须区分 SINT / UINT / FLOAT，至少保存足够信息让 CBind 做 overflow-safe narrowing。禁止先无条件解析成 double 再还原整数。

## 10. CBind binding engine

公共入口概念：

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

### 10.1 encode

- scalar -> primitive token；
- enum -> policy-selected scalar token；
- struct -> MAP_BEGIN + canonical field name/value pairs；
- sequence -> container default Range -> ARRAY；
- map -> entries Range -> MAP；
- optional null -> NULL；
- custom -> format-neutral custom binding adapter。

### 10.2 decode

- validate token kind；
- precise numeric conversion；
- string ownership contract；
- struct 按 field lookup dispatch；
- sequence/map 通过 Collector 事务构造；
- failure 必须调用已构造字段/container 的 destroy/abort；
- success 后对象进入 fully initialized state。

## 11. nested container instance binding

这是本设计必须补齐的 CMeta container contract。

现有自然 API：

```c
Vec(int, values);
```

在变量声明时已经把 descriptor + element type 写入 handle。但结构体字段通常只是：

```c
vec_t values;
```

zero-init 后没有 element binding，因此 generic CBind 不能直接调用 collector。

解决方案：`cmeta_container_desc` 增加一个不分配、不初始化 storage 的 instance type bind hook：

```c
typedef cmeta_status (*cmeta_container_bind_types_fn)(
    void *object,
    const cmeta_type_desc *const *args,
    size_t arity);
```

descriptor 增加：

```c
bind_types
```

语义：

- 只写 declaration-time type metadata；
- 不 malloc；
- 不改变 logical size；
- 对已初始化/不兼容 handle 返回错误；
- arity 由 container kind 决定；
- unary container: element；
- associative container: key + value。

于是 nested sequence decode 可以：

```text
bind_types(field, [int], 1)
  -> collector(field, limit)
  -> begin / accept... / finish
```

这不是 serialization 特例；它补全了 self-describing container 的 generic construction contract。

## 12. TurboSTL container binding

### 12.1 encode sequence

CBind 获取 `cmeta_container_descriptor(object)`，选择 DEFAULT Range，然后逐元素按 semantic element shape encode。

支持：Vec、Deque、List、Stack、Queue、Heap、Set、HashSet 等任何暴露 default range 的 unary container。

注意：Set/HashSet 的顺序语义不保证 canonical JSON 输出。v1 不承诺 canonicalization。

### 12.2 encode map

选择 ENTRIES Range，获取 `cmeta_entry`，分别根据 key/value semantic descriptor 输出 map pair。

JSON writer 要求 key 最终为 STRING token。非 string key 的 JSON map 默认返回 unsupported；以后可以显式配置 array-of-pairs mapping，但不是隐式 fallback。

### 12.3 decode

CBind 首先 `bind_types`，然后获取 collector。

sequence：每个 child value decode 到临时 element，再 `collector_accept`。

map：decode key/value 临时值，构造 `cmeta_entry`，再 accept。

任何 child decode 或 accept 失败都 abort collector。

这样 container capacity/ownership/rollback 仍由 container 自己实现。

## 13. transaction 与 destination contract

v1 的基础 decode API 定义为“构造新值”：

```text
precondition: out 处于 descriptor 定义的 empty state
success:      out fully initialized
failure:      out 回到 empty state
```

它不隐式覆盖 live owned object。

后续可提供：

```c
cbind_decode_replace(...)
```

实现为 temporary decode + move/destroy commit；只有目标 type 具备可证明的 move/destroy lifecycle 时才允许。

禁止 decode failure 后留下已分配的 string、部分 struct field 或半填充 container。

## 14. ownership 与 lifecycle

semantic descriptor 的 lifecycle 不能通过裸 layout 推断。

至少需要：

```text
prepare_empty / reset
move-or-commit（replace API 才需要）
view/read access
```

现有 `cmeta_type_traits` 的 copy/move/destroy 能复用时直接复用，但 serialization callback 不加入 traits。

### 14.1 owned string

`tstr` adapter：decode 分配/拥有数据；destroy 使用 `tstr_free`。

### 14.2 borrowed string

`vstr` adapter：

- 默认 decode 禁止 borrow；
- 只有 options 显式 `allow_borrowed = true`；
- 当前 token 必须具有足够长的 stable lifetime；
- 否则返回 lifetime/ownership error。

### 14.3 pointer

裸 pointer semantic shape 默认 unsupported。只有显式 custom adapter 才能参与 binding。

## 15. allocator 与资源限制

对不可信输入，binding 必须默认 bounded。

`cbind_options` 至少包含：

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

container collector 的 `limit` 由 schema static limit 与 runtime global limit 的较小值决定。

所有 size/count 计算必须检查 overflow。

JSON reader 自身也应有 depth/token size 限制，不能完全依赖 binder。

## 16. field binding policy 与 schema evolution

format-neutral semantic shape 保留 canonical field identity；CBind policy 处理 external binding behavior。

v1 field policy：

```text
external_name          optional rename
aliases[]              decode-only historical names
required               missing -> error
optional/default       missing -> keep/reset default
emit                   encode 是否输出
```

默认策略：

```text
unknown field    -> error
duplicate field  -> error
missing required -> error
```

可以通过 runtime options 放宽 unknown field 为 ignore。

v1 不自动注入 `_version` 字段，也不提供 migration graph。兼容演进依靠 additive optional field + aliases + explicit custom adapter。真正 versioned migration 后续单独设计。

## 17. enum binding policy

默认：

```text
encode -> text
decode -> text
```

可选：

```text
symbol
integer
accept_text_or_symbol
```

numeric enum decode 必须校验该数值是否存在于 descriptor，除非 policy 显式允许 unknown numeric enum。

## 18. error model 与 path

CSerde error 负责 syntax/transport：

```text
invalid token
unexpected eof
invalid escape
invalid UTF-8
number syntax
writer state
I/O sink failure
```

CBind error 负责 semantic conversion：

```text
type mismatch
numeric overflow
missing required
unknown field
duplicate field
unsupported map key
ownership/lifetime
container bind/collector failure
resource limit
custom adapter failure
```

错误必须携带 logical path，例如：

```text
user.addresses[3].zip
settings["timeout"]
```

内部表示使用 path segment stack，不要求为每次错误动态拼字符串。字符串 rendering 是诊断 helper。

CSerde JSON error 可附加 byte offset/line/column，CBind error 可以嵌入 underlying codec error。

## 19. custom adapter

CBind 提供 format-neutral custom adapter：

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

adapter 只能看到 token reader/writer，不得 downcast 成 JSON implementation。这样 UUID、timestamp、tstr、vstr 等 adapter 可跨 codec 复用。

若某个类型确实需要 format-specific behavior，应该通过 policy/codec extension 显式注入，而不是污染全局 type descriptor。

## 20. deterministic 与 canonical output

v1 保证 struct fields 按 semantic descriptor 声明顺序 encode。

不保证：

- HashMap/HashSet 迭代顺序；
- JSON canonical key sorting；
- canonical float rendering 跨 codec；
- cryptographic canonical serialization。

如需要签名/hash 用 canonical wire format，应单独设计 canonical codec mode，而不是假设普通 JSON 输出稳定。

## 21. C/C++ 与 translation-unit identity

与现有 CMeta 一致：header-local descriptors 不要求不同 TU 地址相同。binding 比较 semantic stable id/type identity/content，不用 descriptor pointer equality 作为跨 TU identity。

公共 API 必须可被 C11 和 C++ 消费。

## 22. 安全边界

必须显式测试：

- 深层 nested input；
- 巨大 string length；
- 巨大 array/map；
- count/size multiplication overflow；
- invalid UTF-8；
- integer signed/unsigned overflow；
- duplicate field；
- unknown field skip 跨 nested value；
- truncated input；
- container collector abort；
- custom adapter rollback；
- borrowed view lifetime mismatch；
- malicious map key type。

任何资源限制错误必须 deterministic，并保证 destination 回滚。

## 23. 测试策略

### Phase 1: CMeta data shape

- scalar descriptors；
- struct field semantic lookup；
- layout != semantic shape（`vstr` regression）；
- nested sequence/map descriptors；
- cross-TU semantic equality/content。

### Phase 2: CSerde token contract

先实现 recording reader/writer test double，而不是先写 JSON。证明 token stream 自身可表达 nested arrays/maps 和 error state。

### Phase 3: CBind primitive + struct

用 recording codec 做 round-trip，证明 binder 与 JSON 无关。

### Phase 4: container bind

- `bind_types` no-allocation contract；
- nested Vec/Map field；
- Range encode；
- Collector decode；
- accept failure -> abort；
- capacity limit propagation。

### Phase 5: ownership

`tstr` owned round-trip；`vstr` borrowed stable-view success + transient-view rejection。

### Phase 6: JSON reference codec

- escaping / UTF-8；
- exact integer parse；
- nested map/array；
- unknown skip；
- malformed input diagnostics；
- sink failure propagation。

### Phase 7: full integration

Linux + Windows fresh configure/build/test；C/C++ public-header tests；TurboSTL regressions；sanitizer/fuzz target when available。

## 24. 性能策略

核心路径禁止强制 DOM allocation。

应建立至少三类 benchmark：

```text
scalar/flat struct
nested struct + strings
large Vec/Map
```

比较：

```text
parse only
bind decode
bind encode
```

记录 allocations、bytes copied、throughput。borrowed stable-view mode单独测量，但不得用它掩盖 owned mode 的真实成本。

## 25. 实施顺序

实现 PR 不应一次性完成所有层。建议顺序：

1. CMeta semantic data descriptors；
2. container `bind_types` contract；
3. CSerde token reader/writer + recording test codec；
4. CBind scalar/struct engine；
5. CBind sequence/map through Range/Collector；
6. ownership adapters (`tstr`/`vstr`)；
7. JSON reference codec；
8. field policy/evolution/error path hardening；
9. fuzz/performance/full CI；
10. 最后再决定 `DataStruct(...)` 等 DSL sugar。

每一步都必须能独立 review 和回滚。

## 26. Definition of Done

该架构只有满足以下条件才算实现完成：

- CMeta semantic shape 与 C layout reflection 明确分离；
- CSerde 不依赖 CBind/TurboSTL/CFlow；
- CBind 只依赖 CMeta+CSerde；
- JSON 不是 CBind 的硬编码分支；
- nested TurboSTL container 不需要 CBind 类型特判；
- container decode 通过 `bind_types + Collector`；
- container encode 通过 Range；
- owned/borrowed string lifetime 有可测试 contract；
- decode failure 回滚到 empty state；
- strict unknown/duplicate/missing/overflow/resource-limit behavior 有测试；
- C/C++ public API 编译通过；
- Linux + Windows fresh CI 通过；
- 没有 `Type x Format` generated facade；
- 没有 DOM 作为 mandatory core intermediate representation。

## 27. 决策摘要

本设计采用：

```text
CMeta semantic shape
        |
        +-------------------+
        |                   |
      CSerde             CMeta containers
   token reader/writer   Range / Collector / bind_types
        |                   |
        +---------+---------+
                  |
                CBind
                  |
        JSON reference codec first
```

最重要的边界是：

```text
CMeta 知道“数据是什么”；
CSerde 知道“格式如何读写”；
CBind 知道“C 对象如何映射到数据”；
TurboSTL 知道“容器如何存储和事务构造”；
CFlow 以后只负责“数据如何流动”。
```

这让序列化成为 CMeta/TurboSTL 能力的自然延伸，而不是新的生成式 facade 系统。
