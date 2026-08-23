# CBind ownership supersession

日期：2026-08-23
状态：Supersession notice

`docs/superpowers/specs/2026-08-23-serialization-data-binding-design.md` 仍然是 CMeta/CSerde semantic/canonical protocol 的历史架构依据，但其中“TurboUtils owns CBind / TurboUtils::CBind”这一 ownership 已被后续设计取代。

新的 ownership truth 位于 `qigao/turbo-parser`：

```text
branch: design/cbind-parser-flow
spec:   docs/superpowers/specs/2026-08-23-cbind-parser-cflow-design.md
repo:   qigao/turbo-parser
module: top-level cbind/
target: TurboParser::CBind
header: <cbind/...>
C ABI:  cbind_*
```

仍然保持的基础边界：

```text
TurboUtils owns CMeta + CSerde.
TurboUtils does not depend on TurboParser.
TurboParser::CBind depends only on TurboUtils::CMeta + TurboUtils::CSerde.
CBind core is format-neutral.
TBE wire/layout remains specialized in TurboParser.
DataBind remains a runtime/dynamic compatibility layer and may later delegate native binding to CBind.
```

新增的后续组合边界：

```text
parser/* adapter -> canonical CSerde projection -> CBind -> native object
native object -> optional CFlow Stream<T> through TurboParser CBindFlow composition
```

raw `cserde_token` 不作为允许任意 `filter/map` 的业务 Stream 元素；CFlow 从完整 semantic/native value 边界开始。
