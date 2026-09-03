# Serialization / Data Binding Parser + CFlow Amendment

日期：2026-08-23
状态：Design amendment / 待 written-spec review
适用文档：`docs/superpowers/specs/2026-08-23-serialization-data-binding-design.md`

## 1. Ownership clarification

原设计的基础 ownership 保持有效：

```text
Salts owns CMeta + CSerde + CBind + CFlow.
TurboParser owns concrete parsers, DataBind, TBE, format projection and parser-specific composition.
```

CBind 不迁入 TurboParser。

固定依赖：

```text
Salts::CBind -> Salts::CMeta + Salts::CSerde
```

Salts 不反向依赖 TurboParser。

## 2. Parser adapter direction

TurboParser concrete parser/event model 投影到 canonical data contract，再消费 `Salts::CBind`：

```text
TurboParser parser/*
    -> format projection adapter
    -> CSerde canonical tokens
    -> Salts::CBind
    -> native C object
```

JSON/YAML/XML/CSV-specific syntax、query、header、attribute、tag、alias 等 policy 留在 TurboParser adapter 层，不进入 CMeta/CSerde/CBind core。

## 3. Correction to the earlier CFlow placeholder

原设计中把未来 CFlow adapter 粗略写成：

```text
CSerde reader -> CFlow source
CFlow stream   -> CSerde writer sink
```

该表达过于宽泛，现由本 amendment 收紧。

raw `cserde_token` 是结构协议，不是业务独立元素；它不能作为允许普通 `filter/map` 的 public typed CFlow stream surface。

固定边界：

```text
CSerde token stream = structural transport
CBind               = semantic/native binding
CFlow typed stream   = complete semantic/native values
```

因此 parser + binding + flow 的组合应当是：

```text
parser source
    -> complete semantic value
    -> CBind
    -> native object T
    -> CFlow typed stream
```

而不是：

```text
cserde_token stream -> filter/map
```

## 4. Pull CSerde reader vs push parser

CSerde v1 的 CBind public substrate 是 pull `cserde_reader`。TurboParser 的部分格式 parser 已提供 SAX/event push API。

跨 repo direct push binding 若要做到 no-DOM / bounded / no-unbounded-token-queue，可能需要未来 public incremental CBind decoder contract。

该 contract 不属于当前 D2，也不能通过 TurboParser include/call CBind private internals 实现。

是否增加 incremental decoder，以及其 begin/feed/finish ABI、partial value state、backpressure 与 error semantics，必须在 parser adapter 独立设计阶段基于真实 integration constraint 决定。

## 5. Direct format convenience

TurboParser 可以在后续提供 direct JSON/YAML/XML/CSV -> native object API，但这些 API 属于 format adapter/composition，不属于 `Salts::CBind` core。

具体函数名和 target 名未在本 amendment 锁定。

## 6. DataBind / TBE

DataBind 仍是 runtime schema / dynamic value / compatibility / query host，并可逐步把 generic native binding 委托给 `Salts::CBind`。

TBE 继续独占 specialized wire/layout metadata：

```text
wire offset
endianness
presence bitmap
fixed block
group
variable data
```

这些不进入通用 CMeta semantic descriptor。

## 7. Final dependency truth

```text
Container -> CMeta
CSerde   -> minimal C runtime
CBind    -> CMeta + CSerde
CFlow    -> CMeta

TurboParser parser adapters -> concrete parser + CSerde + CBind as needed
TurboParser DataBind         -> parser/runtime layers + CBind in later migration
TurboParser flow composition -> parser adapters + CBind + CFlow
```

CBind 与 CFlow core 互不依赖。

## 8. Scope consequence

首个 CBind D2 implementation 不包含：

```text
TurboParser changes
parser adapters
CFlow integration
public incremental decoder
DataBind/TbeTyped migration
```

这些全部保持独立阶段，防止基础 binding kernel 的 ABI 与 format/product integration 同时膨胀。
