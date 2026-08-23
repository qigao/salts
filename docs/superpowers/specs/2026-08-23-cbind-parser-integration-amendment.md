# CBind Parser / CFlow Integration Amendment

日期：2026-08-23
状态：Design amendment / 已在对话中确认 ownership，待 written-spec review
适用文档：`docs/superpowers/specs/2026-08-23-cbind-scalar-struct-decode-design.md`

## 1. 目的

本 amendment 固化在讨论 TurboParser direct binding 与 CFlow stream-like API 后得到的边界，并撤销短暂提出的“CBind 迁入 TurboParser”方案。

D2 原设计关于 scalar/struct decode、context-first、caller scratch、preflight、rollback、numeric conversion、error attribution 与 reader position 的技术契约保持有效。

本 amendment 只修正/补充 ownership、跨 repo adapter 与 CFlow integration 边界。

## 2. Ownership 最终决策

CBind 与 CMeta/CSerde 同属 TurboUtils 基础数据语义栈：

```text
TurboUtils
├── CMeta   semantic/type truth
├── CSerde  canonical token truth
├── CBind   canonical semantic value <-> native C storage
├── CFlow   execution pipeline
└── STL     container implementation
```

公开身份保持：

```text
repo:    qigao/turbo-utils
module:  top-level cbind/
target:  TurboUtils::CBind
headers: <cbind/...>
C ABI:   cbind_*
```

依赖固定为：

```text
TurboUtils::CBind
    -> TurboUtils::CMeta
    -> TurboUtils::CSerde
```

CBind core 不依赖：

```text
TurboParser
parser/*
DataBind
TBE
TurboSTL
CFlow
JSON/YAML/XML/CSV concrete parser
```

repo ownership 由模块自身语义和依赖决定，而不是由未来 consumer 决定。Database row、network source、IPC source、synthetic source 等非-parser producer 也可以通过 CSerde 使用 CBind，因此 CBind 不属于 Parser product。

## 3. TurboParser 的职责

TurboParser 继续单向依赖 TurboUtils，并拥有格式语法、查询和 format-specific projection：

```text
TurboParser
├── parser/*
├── parser -> CSerde projection adapters
├── DataBind
├── TBE
└── optional Parser + CBind + CFlow composition
```

依赖方向：

```text
TurboParser format adapter
    -> concrete parser
    -> TurboUtils::CSerde
    -> TurboUtils::CBind
```

TurboUtils 不反向依赖 TurboParser。

DataBind/TbeTyped 可在后续 migration 中消费 `TurboUtils::CBind`，但它们不是 CBind ownership 的理由。

## 4. D2 public API 不变

首个 D2 public decode 仍为：

```c
cbind_status cbind_decode(
    const cbind_context *context,
    const cmeta_data_desc *shape,
    cserde_reader *reader,
    void *out,
    cbind_error *error);
```

D2 不增加：

```text
cbind_json_decode
cbind_yaml_decode
cbind_xml_decode
cbind_csv_decode
cbind_decoder_begin/accept/finish public ABI
CFlow source/stream API
```

这些属于后续独立设计阶段。

## 5. Pull reader 与 future push parser 的边界

CSerde v1 的 public decode substrate 是 pull `cserde_reader`。TurboParser 当前多个 parser 则具有 SAX/event push API。

此前讨论过内部 token-fed machine：

```text
parser SAX -> canonical token -> cbind machine
```

该结构可以是 CBind D2 的内部实现方式，但 **private machine 不能作为跨 repo contract**。TurboParser 不得 include TurboUtils private headers 或调用未导出的 CBind internal symbols。

因此 D2 只要求 `cbind_decode(reader)` 的可观察语义，不要求公开或稳定内部 machine ABI。

如果后续 direct adapter 要实现真正的：

```text
parser SAX
    -> canonical token
    -> CBind
```

且明确要求：

```text
no mandatory DOM
no unbounded token queue
bounded/streaming execution
```

则必须先单独设计一个 public incremental decoder contract，例如概念上的 begin/feed/finish family。最终名称、对象布局、backpressure、partial-value lifecycle、error ownership 与 ABI version 都不在 D2 中预设。

在该 contract 出现前，不得把 TurboParser 对 CBind private machine 的直接调用写入实现计划。

## 6. Direct format binding 的 ownership

未来 convenience API 可以让应用直接从格式输入绑定 native object，但 implementation/target 属于 TurboParser format adapter，而不是 `TurboUtils::CBind` core。

概念数据流：

```text
JSON/YAML/XML/CSV input
        |
        v
TurboParser concrete parser
        |
        v
format-specific canonical projection
        |
        v
TurboUtils::CBind
        |
        v
native C object
```

具体 API 名称（例如 `cbind_json_decode` 或其他命名）尚未锁定；不得把概念示例视为 D2 public ABI。

JSON/YAML 更接近 canonical MAP/ARRAY event model；XML/CSV 需要 format-specific projection policy。XML element/attribute/text、CSV header/row/column 语义不得进入 CMeta/CSerde/CBind core。

## 7. CFlow integration 最终边界

CFlow 与 CBind 都在 TurboUtils，但二者 core 仍不互相依赖。

禁止把 raw `cserde_token` 暴露成允许任意业务 operator 的普通 `Stream<T>`：

```text
Stream<cserde_token>
    -> filter/map
```

会允许删除 `MAP_END`、field key 或破坏 key/value pairing，因此不是合法业务组合面。

固定语义：

```text
CSerde token stream = structural transport
CBind               = semantic/native binding
CFlow Stream<T>      = complete semantic/native values
```

Parser/CBind/CFlow composition 应位于 complete value boundary：

```text
parser source
    -> complete semantic value
    -> CBind
    -> native object T
    -> CFlow Stream<T>
    -> filter/map/flatMap/distinct/sorted/limit/collect
```

适合未来形成 `Stream<T>` 的 source 包括：

```text
JSON root array items
JSONPath matches
YAML sequence items
YPath matches
CSV rows
XML/XPath matches
```

每一个 stream item 在进入业务 operator 前必须已经是一个完整 semantic/native value。

## 8. CFlow composition ownership

Parser-specific stream composition 属于 TurboParser integration layer，因为 source selection 与 format projection 依赖 parser/query 语义。

逻辑依赖：

```text
TurboParser parser/CBind/CFlow integration
    -> concrete parser/query
    -> TurboUtils::CBind
    -> TurboUtils::CFlow
```

`CBindFlow` 可作为讨论中的 design label，但 target/header/API 名称尚未批准，不属于 D2 ABI。

## 9. DataBind / TbeTyped migration

首个 CBind D2 PR 不修改 TurboParser DataBind/TbeTyped。

后续独立阶段：

```text
TbeTyped generic semantic metadata
    -> project/replace with CMeta semantic descriptors

DataBind native binding paths
    -> delegate generic binding to TurboUtils::CBind

TBE-only metadata
    -> retain wire offset / endian / presence / fixed block / group / var-data
```

目标是消除重复 generic semantic/binding truth，而不是把 CBind 的 repo ownership移到 consumer repo。

## 10. D2 implementation-plan consequence

D2 implementation plan 必须：

```text
keep target TurboUtils::CBind
keep production deps CMeta + CSerde only
keep public entry cbind_decode(reader)
not add parser adapters
not add CFlow dependency
not add public incremental decoder API
not modify TurboParser
```

内部是否采用 token-fed state machine 可以由 implementation plan 为可测试性/职责分离做出选择，但它必须保持 private，且不能被当作未来 TurboParser integration ABI。

实现应从 implementation 时的最新 TurboUtils master 创建 feature branch；本 design branch 的历史 base SHA 不要求成为未来 implementation branch base。

## 11. 后续设计顺序

推荐顺序：

```text
D2   TurboUtils::CBind scalar + struct pull decode
D2a  installed package / Linux + Windows conformance
P1   incremental CBind decoder contract（仅当 direct SAX zero-queue 需要）
P2   TurboParser JSON direct adapter
P3   TurboParser YAML direct adapter
P4   XML/CSV projection specs + adapters
F1   Parser + CBind + CFlow complete-value stream composition
M1   TbeTyped semantic metadata migration
M2   DataBind native binding delegation
```

P1 是否需要、以及具体 public ABI，在 P2 设计时由真实 parser integration 约束决定；D2 不提前扩张 API。

## 12. Final invariants

```text
TurboUtils owns CMeta + CSerde + CBind + CFlow.
TurboUtils does not depend on TurboParser.

CBind depends only on CMeta + CSerde.
CBind does not depend on CFlow or parser/*.

TurboParser adapters may depend on CBind.
TurboParser composition may depend on CBind + CFlow.
DataBind may later depend on CBind.

CSerde tokens remain structural transport.
CFlow business elements begin at complete semantic/native value boundaries.

CMeta  = semantic/type truth
CSerde = canonical token truth
CBind  = native binding truth
Parser = syntax/projection truth
TBE    = specialized binary wire/layout truth
CFlow  = execution truth
```
