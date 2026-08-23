# CSerde canonical token protocol 设计

日期：2026-08-23
状态：Design / approved direction, implementation pending
基线：`master` @ `4e2d47971071bb75266a56844919a79e724c5c82`

## 1. 背景

当前 `master` 已完成 CBind 之前的三层基础：

```text
CMeta semantic data descriptor
        ↓
TurboSTL semantic projection
        ↓
Struct TYPE(...) + declared construction + zero-handle bind_types
```

其中 #42 已建立真实的 nested zero-container construction contract：字段元数据携带 `cmeta_declared_type`，真正的全零 handle 从 declaration side 调用 `cmeta_container_bind_types()`，成功后继续复用已有 runtime container descriptor 与 Collector。

仓库当前尚无 CSerde/CBind production module。既有 serialization/data-binding 总体设计已经确定依赖方向：

```text
TurboSTL -> CMeta
CSerde   -> C runtime only
CBind    -> CMeta + CSerde
TurboParser format adapters -> CSerde
TurboParser::DataBind       -> adapters + CBind
```

因此不能直接让 CBind 发明临时 token/callback 输入接口。本阶段先建立唯一的 format-neutral canonical token protocol，后续 CBind 和 TurboParser adapters 都消费这一协议。

本设计是总体 serialization/data-binding 设计中 CSerde 部分的窄化实现规格。若旧设计中的 construction 草案与当前代码冲突，以 #42 已合入的 `cmeta_container_ext.construction + cmeta_declared_type + cmeta_container_bind_types()` 为准；D1 不再设计任何 container construction API。

## 2. 目标

D1 只完成以下能力：

1. 新增独立 `cserde/` module 与 `TurboUtils::CSerde` target。
2. 定义 format-neutral canonical token model。
3. 定义 pull reader 与 push writer provider protocol。
4. reader/writer provider ops 从 v1 起使用 `struct_size + abi_version`，按 required-field prefix 验证。
5. STRING/BYTES 明确 borrowed view lifetime，CSerde core 不隐式复制、不取得 ownership。
6. 提供通用 `cserde_reader_skip_value(reader, max_depth)`，可正确跳过 nested ARRAY/MAP，并检测结构损坏与深度限制。
7. 提供 test-only recording reader/writer support，作为 D2 CBind 的 deterministic input/output harness。
8. C11 与 C++17 public-header conformance。
9. Linux/Windows 现有 release workflow 真正触发并执行 `cserde_*` tests。

## 3. 非目标

D1 不实现：

- CBind；
- JSON/YAML/XML/CSV/CBOR/MessagePack parser 或 serializer；
- TurboParser adapter；
- DOM/value tree；
- CMeta semantic descriptor 修改；
- TurboSTL、Range、Collector、construction 修改；
- schema/field policy；
- unknown/duplicate/missing field policy；
- owned string allocation；
- arbitrary precision decimal/bigint；
- process-global registry、allocator、limits 或 codec selection；
- CFlow integration。

CSerde 不是 parser library，也不是 binding engine。它只定义 canonical data events 与 reader/writer transport contract。

## 4. 模块与依赖

目录：

```text
cserde/
  CMakeLists.txt
  include/cserde/
    cserde.h
    status.h
    token.h
    reader.h
    writer.h
  src/
    token.c
    reader.c
    writer.c
  tests/
    CMakeLists.txt
    cserde_token_test.c
    cserde_reader_test.c
    cserde_writer_test.c
    cserde_header_cpp_test.cpp
    support/
      recording.c
      recording.h
```

CMake：

```text
turbo_cserde
alias: TurboUtils::CSerde
language: C11
public dependency: none beyond C runtime
```

`cserde` 不 include/link CMeta、CFlow、TurboSTL、TurboParser 或 `utils` Core。后续 CBind 单向依赖 `TurboUtils::CSerde` 与 `TurboUtils::CMeta`。

Top-level `CMakeLists.txt` 加入 `add_subdirectory(cserde)`。CSerde 本身不依赖 CMeta，因此目录顺序不构成语义依赖。

## 5. Status contract

公开状态：

```c
typedef enum cserde_status {
    CSERDE_OK = 0,
    CSERDE_DONE,
    CSERDE_INVALID_ARGUMENT,
    CSERDE_INVALID_STATE,
    CSERDE_INVALID_TOKEN,
    CSERDE_UNEXPECTED_END,
    CSERDE_VALUE_OUT_OF_RANGE,
    CSERDE_LIMIT_EXCEEDED,
    CSERDE_UNSUPPORTED,
    CSERDE_SOURCE_ERROR,
    CSERDE_SINK_ERROR,
    CSERDE_CALLBACK_ERROR
} cserde_status;
```

语义：

- `OK`：操作完成；reader `next` 产生一个 token。
- `DONE`：reader 正常到达 canonical stream 末尾，不是错误。
- `INVALID_ARGUMENT`：调用参数无效。
- `INVALID_STATE`：在不允许的 reader/writer state 上操作。
- `INVALID_TOKEN`：canonical token payload 或 ARRAY/MAP 结构非法。
- `UNEXPECTED_END`：正在要求一个完整 value 时输入提前 `DONE`。
- `VALUE_OUT_OF_RANGE`：native source value 无法进入 v1 canonical scalar representation。
- `LIMIT_EXCEEDED`：显式资源限制被触发，例如 `skip_value` 超过 `max_depth`。
- `UNSUPPORTED`：native/canonical 表达之间不存在本 adapter 支持的无损投影。
- `SOURCE_ERROR`：reader provider 的下层 input/parse/source 失败。
- `SINK_ERROR`：writer provider 的下层 output/sink 失败。
- `CALLBACK_ERROR`：provider 返回未知或违反 callback contract 的状态。

CSerde status 只表达 canonical protocol 层粗粒度结果。未来 TurboParser adapter 可在其 context 中保留 format-specific line/column/parser diagnostics；CSerde 不复制第二套 parser error model。

## 6. Canonical token model

### 6.1 token kinds

```c
typedef enum cserde_token_kind {
    CSERDE_NULL = 0,
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

不定义 JSON-specific `OBJECT`。canonical struct/object 是 string-key MAP；canonical MAP 本身允许任意 canonical key value，具体 format 或 CBind semantic shape 再施加 key 类型限制。

例：

```text
MAP_BEGIN
STRING("id")
SINT(7)
STRING("name")
STRING("alice")
MAP_END
```

### 6.2 scalar payload

```c
typedef struct cserde_token {
    cserde_token_kind kind;
    union {
        bool boolean;
        int64_t sint;
        uint64_t uint;
        double floating;
        cserde_slice slice;
    } value;
} cserde_token;
```

规则：

- `SINT` 使用 `int64_t`。
- `UINT` 使用 `uint64_t`。
- `FLOAT` 使用 binary64 `double` storage；canonical layer 允许 non-finite，具体 writer/format/CBind policy 可拒绝。
- native textual adapter 必须先精确区分 integer 与 floating syntax；整数禁止经 `double` round-trip 后再恢复。
- 超出 int64/uint64 的整数在 D1/D2 v1 返回 `VALUE_OUT_OF_RANGE`，不偷偷转 FLOAT。
- arbitrary precision decimal/bigint 不属于 D1；后续若需要，以明确 extension/custom semantic contract 增加，而不是改变既有整数含义。

### 6.3 STRING / BYTES view

```c
typedef enum cserde_view_lifetime {
    CSERDE_VIEW_TRANSIENT = 0,
    CSERDE_VIEW_STABLE
} cserde_view_lifetime;

typedef struct cserde_slice {
    const unsigned char *data;
    size_t size;
    cserde_view_lifetime lifetime;
} cserde_slice;
```

规则：

- CSerde token 永远不拥有 `data`。
- `size == 0` 时 `data == NULL` 合法；非空 slice 必须 `data != NULL`。
- slice 不保证 NUL terminator，`size` 不包含 terminator。
- `STRING` producer 保证 canonical text 为 UTF-8。core 只做 shallow contract validation，不重复 native parser 已完成的 Unicode validation。
- `BYTES` 是任意 bytes。
- `TRANSIENT`：最迟在下一次消费/推进 reader 的操作后失效，包括下一次 `cserde_reader_next()` 或 `cserde_reader_skip_value()`。
- `STABLE`：reader 推进不会使 view 失效；终止 lifetime 由 adapter 的 backing source owner contract 决定。报告 `STABLE` 的 adapter 必须在其 constructor/API 中明确 source owner 生命周期。
- `STABLE` 不等于 ownership transfer。未来 CBind 只有在 target 明确允许 borrow、调用方保持 source owner 存活且 token 报告 `STABLE` 时才能 zero-copy borrow。

## 7. Token validation

公开 helper：

```c
bool cserde_token_kind_valid(cserde_token_kind kind);
bool cserde_view_lifetime_valid(cserde_view_lifetime lifetime);
bool cserde_token_valid(const cserde_token *token);
```

`cserde_token_valid()` 只做 protocol-level shallow validation：kind 范围、STRING/BYTES slice 指针/长度/lifetime 组合。它不解析 UTF-8、不判断 MAP key policy、不判断 numeric target range，也不维护 ARRAY/MAP stack。

## 8. Pull reader protocol

### 8.1 versioned provider ops

```c
enum { CSERDE_READER_OPS_ABI_VERSION = 1u };

typedef cserde_status (*cserde_reader_next_fn)(
    void *context,
    cserde_token *out);

typedef struct cserde_reader_ops {
    size_t struct_size;
    uint32_t abi_version;
    cserde_reader_next_fn next;
} cserde_reader_ops;
```

v1 required prefix 到 `.next`。validation 使用 field-end prefix，禁止以 `struct_size >= sizeof(current_struct)` 判断兼容。未来 optional reader capability 只能尾部追加。

reader provider 的 `next` 允许返回：

```text
OK / DONE / VALUE_OUT_OF_RANGE / LIMIT_EXCEEDED / UNSUPPORTED / SOURCE_ERROR
```

其中 `OK` 必须填充合法 token。其它已定义 status（例如 `SINK_ERROR`、`INVALID_STATE`）若从 reader callback 返回，也视为 callback contract violation，facade 规范化为 `CALLBACK_ERROR`。

### 8.2 reader facade

```c
typedef enum cserde_reader_state {
    CSERDE_READER_ZERO = 0,
    CSERDE_READER_READY,
    CSERDE_READER_DONE,
    CSERDE_READER_FAILED
} cserde_reader_state;

typedef struct cserde_reader {
    const cserde_reader_ops *ops;
    void *context;
    cserde_reader_state state;
    cserde_status status;
} cserde_reader;
```

`cserde_reader` 借用 `ops` 与 `context`，不负责销毁 provider context。`context == NULL` 合法，只要 provider callback 能处理。facade 是同一 TurboUtils release 内的 stack value type；跨版本 provider extensibility 由 versioned ops 承担，不把 facade 当插件 ABI extension root。

公开入口：

```c
cserde_status cserde_reader_init(
    cserde_reader *reader,
    const cserde_reader_ops *ops,
    void *context);

cserde_status cserde_reader_next(
    cserde_reader *reader,
    cserde_token *out);

cserde_status cserde_reader_skip_value(
    cserde_reader *reader,
    size_t max_depth);
```

### 8.3 reader state semantics

`init`：

- caller 提供 zero-initialized reader；
- 验证 ops prefix/ABI/next；
- 成功后 state=`READY`, status=`OK`；
- 不调用 provider；
- 失败时 reader 保持 ZERO，不留下半初始化 facade。

`next`：

- `reader == NULL`、`out == NULL` 等 caller precondition error 直接返回 `INVALID_ARGUMENT`，不推进 provider、不改变已有 READY/DONE/FAILED state；
- `READY` 才调用 provider；
- callback 写入临时 token；只有 callback=`OK` 且 `cserde_token_valid()` 后才复制到 caller `out`，因此任何非 OK 返回都不污染 `out`；
- provider `DONE` -> state=`DONE`；
- provider 返回允许的 error -> state=`FAILED`，status sticky；
- provider 返回非法 status 或违反 callback contract -> `CALLBACK_ERROR` + `FAILED`；
- `DONE` 后再次 `next` 直接返回 `DONE`，不再次调用 provider；
- `FAILED` 后再次 `next` 返回 sticky error，禁止继续推进 source。

不提供 silent reset/recovery。需要重新读取时由 adapter 创建新的 context/reader。

## 9. `skip_value` grammar 与 depth contract

canonical value grammar：

```text
value := NULL | BOOL | SINT | UINT | FLOAT | STRING | BYTES
       | ARRAY_BEGIN value* ARRAY_END
       | MAP_BEGIN (value value)* MAP_END
```

`cserde_reader_skip_value(reader, max_depth)` 从 reader 的“下一个 token”开始，消费**恰好一个完整 value**：

- scalar：消费一个 token，返回 `OK`；
- ARRAY：递归消费元素直到匹配 `ARRAY_END`；
- MAP：按 key/value 成对递归消费直到匹配 `MAP_END`；canonical MAP key 可为任意 value；
- 在第一个 token 前即 `DONE` -> `UNEXPECTED_END`；
- mismatched/unexpected END -> `INVALID_TOKEN`；
- MAP 出现孤立 key -> `INVALID_TOKEN`；
- container 未闭合而 reader `DONE` -> `UNEXPECTED_END`；
- nesting 深度超过 `max_depth` -> `LIMIT_EXCEEDED`；
- `max_depth` 计数 opened ARRAY/MAP，root container 深度为 1；scalar 不消耗 depth budget。

实现允许使用受 `max_depth` 约束的 C call stack recursion，不做 heap allocation。任何 skip failure 都把 reader 置为 `FAILED` 并记录 sticky error；即使内部 `next()` 已先进入 DONE，helper 也把“不完整 value 的 DONE”提升为 `UNEXPECTED_END/FAILED`。stream 已可能部分消费，禁止 caller 猜测恢复点。

该 helper 是未来 CBind “ignore unknown field” 的唯一 generic skip primitive；CBind 不自己复制一套 token nesting walker。

## 10. Push writer protocol

### 10.1 versioned provider ops

```c
enum { CSERDE_WRITER_OPS_ABI_VERSION = 1u };

typedef cserde_status (*cserde_writer_write_token_fn)(
    void *context,
    const cserde_token *token);

typedef cserde_status (*cserde_writer_finish_fn)(void *context);

typedef struct cserde_writer_ops {
    size_t struct_size;
    uint32_t abi_version;
    cserde_writer_write_token_fn write;
    cserde_writer_finish_fn finish;
} cserde_writer_ops;
```

v1 required prefix 到 `.finish`，同样使用 field-end prefix validation。

writer `write`/`finish` callback 允许返回：

```text
OK / LIMIT_EXCEEDED / UNSUPPORTED / SINK_ERROR
```

`DONE`、reader-only status 或未知枚举值均是 callback contract violation，facade 转为 `CALLBACK_ERROR`。

### 10.2 writer facade

```c
typedef enum cserde_writer_state {
    CSERDE_WRITER_ZERO = 0,
    CSERDE_WRITER_READY,
    CSERDE_WRITER_FINISHED,
    CSERDE_WRITER_FAILED
} cserde_writer_state;

typedef struct cserde_writer {
    const cserde_writer_ops *ops;
    void *context;
    cserde_writer_state state;
    cserde_status status;
} cserde_writer;
```

公开入口：

```c
cserde_status cserde_writer_init(
    cserde_writer *writer,
    const cserde_writer_ops *ops,
    void *context);

cserde_status cserde_writer_write(
    cserde_writer *writer,
    const cserde_token *token);

cserde_status cserde_writer_finish(cserde_writer *writer);
```

writer 语义：

- init 需要 zero-initialized writer；失败保持 ZERO，无部分写入；`context == NULL` 合法；
- caller precondition error 不调用 provider，也不 poison 一个仍可用的 READY writer；
- `write` 只接受 READY + valid token；invalid token 返回 `INVALID_TOKEN`，provider 不被调用，writer 保持 READY；
- callback `OK` 保持 READY；允许的 callback error -> FAILED/sticky；非法 callback status -> CALLBACK_ERROR/FAILED；
- `finish` 只接受 READY；callback `OK` -> FINISHED；允许的 callback error -> FAILED/sticky；
- FINISHED 后再次 write/finish -> `INVALID_STATE`，不再次调用 provider；
- CSerde facade v1 不维护完整 output nesting stack。结构正确性由 token producer 与 concrete writer backend 共同承担；未来 CBind encoder 按 semantic recursion 产生完整 value，JSON/YAML 等 writer state machine 仍应拒绝非法序列。

不在 v1 添加 `abort`。对流式 sink，已经输出的 bytes 通常无法回滚；伪造 abort API 不会提供事务语义。CBind encode 的错误模型后续必须明确“已写前缀可能存在”。

## 11. Byte sink primitive

为 TurboParser format writer adapter 提供最小 byte sink callback type，但 CSerde core 不直接实现 serializer：

```c
typedef cserde_status (*cserde_byte_sink_fn)(
    void *context,
    const void *data,
    size_t size);
```

byte sink 的合法成功/失败结果是 `OK / LIMIT_EXCEEDED / SINK_ERROR`；其它 status 由具体 adapter 视为 callback contract violation。D1 不提供 buffering、file/socket sink、allocator 或 retry policy。

## 12. Threading 与 ownership

- reader/writer facade 与 provider context 默认 externally serialized，不宣称 thread-safe。
- CSerde 不分配 token payload，不释放 token payload。
- reader/writer 不拥有 provider context。
- 没有 process-global mutable registry。
- provider 若需要 scratch buffer，其 lifetime 必须通过 `TRANSIENT` view contract 暴露，不得伪装成 STABLE。

## 13. Recording conformance support

D1 提供仓库内 test-only support，不安装、不导出：

```text
cserde/tests/support/recording.{h,c}
```

用途：

- recording reader：从 caller-owned token array 顺序产生 token，正常结束返回 DONE；
- recording writer：把 token 写入 caller-provided fixed-capacity token buffer；容量不足返回 `LIMIT_EXCEEDED`；
- 不做 heap allocation；
- STRING/BYTES slice 默认继续 borrow caller payload；
- support target 仅在 `BUILD_TESTS` 下存在，并可被后续 `cbind/tests` 复用；
- 不成为 `TurboUtils::CSerde` public/install surface。

这使 D2 CBind 可以完全在 canonical protocol 上做 RED/GREEN，而不需要先连接真实 JSON parser。

## 14. C++17 public surface

所有 public header：

- 使用标准 C11-compatible data types；
- 在 C++ 下提供 `extern "C"` function linkage；
- 不暴露 `_Generic`、statement-expression、GNU extension；
- C++17 consumer 能 include `<cserde/cserde.h>` 并检查 token/ops/facade surface；
- 不要求异常；API 全部以 `cserde_status` 返回。

## 15. ABI 与兼容性

versioned provider structures：

```text
cserde_reader_ops
cserde_writer_ops
```

都使用：

```text
struct_size
abi_version
required-field prefix
```

规则与 CMeta 当前 append-safe extension 相同：

- v1 consumer 只要求已知 required prefix；
- future producer 可追加 tail；
- future consumer 遇到旧 producer 时，只能读取其 `struct_size` 已覆盖的字段；
- 禁止用 `sizeof(current_struct)` 判断兼容；
- unknown ABI version fail fast。

`cserde_token` 与 reader/writer facade 是 v1 same-release value ABI，不作为可尾部扩展的 provider object。未来若需要改变 token payload layout，应显式新增 protocol ABI/version，而不是静默改 union layout。

## 16. CI / build contract

现有 `.github/workflows/cmeta.yml` 目前没有 `cserde/**` path trigger，也只执行 `^(cmeta_|cflow_|turbostl_)` selected tests。D1 implementation 必须更新为：

```text
paths:
  + cserde/**

selected tests regex:
  ^(cmeta_|cflow_|turbostl_|cserde_)
```

Windows 的 combined configure/build/test 必须同样实际执行 cserde tests，不能只编译。

最终 exact-head gate：

```text
Linux fresh configure
Linux full build
Linux selected cmeta/cflow/turbostl/cserde tests
Windows configure + build + test
```

## 17. D1 acceptance contract

D1 只有同时证明以下行为才可结束：

1. token kind/payload shallow validation 正确；
2. empty/non-empty STRING/BYTES slice 与 lifetime 校验正确；
3. reader ops short prefix / bad ABI / missing next 被拒绝；
4. writer ops short prefix / bad ABI / missing write/finish 被拒绝；
5. reader init failure 无部分写入；
6. reader OK -> DONE 与 sticky FAILED 状态正确；
7. reader 非 OK 不污染 caller `out`；
8. invalid token callback 被 facade 转换为 INVALID_TOKEN；
9. reader/writer 非法 callback status 被规范化为 CALLBACK_ERROR；
10. `skip_value` 能跨 nested ARRAY/MAP；
11. `skip_value` 检测 initial/unexpected DONE、mismatched END、孤立 MAP key与 depth limit；
12. writer init failure 无部分写入，invalid input token 不调用 provider 且不 poison READY writer；
13. writer write/finish terminal state 正确；
14. recording reader/writer 可作为无 allocation deterministic test codec；
15. C++17 public include contract 通过；
16. CSerde target 不依赖 CMeta/CFlow/TurboSTL/utils；
17. 全仓没有 JSON/YAML/XML/CSV parser implementation 被加入 TurboUtils；
18. exact-head Linux + Windows release CI 通过。

## 18. 后续边界

D1 合并后进入 D2：CBind scalar + struct core。

D2 只允许依赖：

```text
CBind -> CMeta + CSerde
```

D2 使用 recording reader 证明 numeric conversion、struct field dispatch、strict field policy 与 empty-state transaction。D3 再通过 #42 已存在的：

```text
field->declared_type
    -> cmeta_container_bind_types(field_address, declared)
    -> runtime container descriptor
    -> existing Collector
```

接入 sequence/map decode。

D3 不允许出现 `Vec/Map/HashMap/...` type switch，不允许 CBind link TurboSTL，也不允许 CBind 保存第二份 T/K/V。
