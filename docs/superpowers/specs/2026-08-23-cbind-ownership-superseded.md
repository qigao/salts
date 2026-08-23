# CBind ownership supersession

日期：2026-08-23
状态：Supersession notice

`docs/superpowers/specs/2026-08-23-cbind-scalar-struct-decode-design.md` 中的 D2 scalar/struct decode、context-first、caller scratch、preflight、rollback 与 numeric conversion reasoning 仍可作为历史技术设计依据，但其中以下 ownership 已失效：

```text
repo:   qigao/turbo-utils
target: TurboUtils::CBind
module: turbo-utils/cbind/
```

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

新的 CBind kernel 仍保持 format-neutral，production dependency 仅为：

```text
TurboParser::CBind
    -> TurboUtils::CMeta
    -> TurboUtils::CSerde
```

具体 parser direct binding 与 CFlow stream-like composition 属于 TurboParser adapter/composition layers；CBind core 不依赖 parser/* 或 CFlow。

首个 CBind D2 PR 不修改 TurboParser 现有 DataBind/TbeTyped；迁移另开阶段。
