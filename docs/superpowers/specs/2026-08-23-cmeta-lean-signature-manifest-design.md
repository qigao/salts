# CMeta Lean 有限签名 Manifest 设计

## 背景

CMeta 已把 callable type universe 与 known type universe 分离，并提供三种
signature policy：relation、balanced 和 full。默认 relation policy 只展开显式
关系，但五个内建类型行和三类内建 relation 仍由 C 预处理器头手工维护。

现有 Lean calculus 能表达抽象类型和 callable judgement，却没有可枚举、可生成
C 头文件的 manifest。因此 Lean 目前能证明规则，不能证明实际 C relation 清单与
形式模型一致。

## 目标

1. 增加可枚举的 Lean `SignatureManifest`，显式拥有内建 C 类型行、unary、binary
   和 generator relations。
2. 对 manifest 执行 fail-fast 校验：空集合、重复行和 relation 引用未知类型均为
   错误。
3. 在 Lean 中证明内建 manifest 合法、binary relation 数量为 2，并严格小于五类
   型全积 `5³`。
4. 从同一 manifest 确定性生成 C 头，替代 `types.h` 与 `relations.h` 中对应的手写
   内建事实。
5. 提供 `--write`、`--check` 和 `--stdout` 构建期工具；CI 用 `--check` 拒绝
   manifest 与已提交生成物漂移。
6. 保持 CMeta runtime、`cmeta_sig` 排序、函数签名、用户扩展宏和普通 CMake 构建
   行为不变。

## 非目标

- 不实现通用 C++ 模板、任意 C 类型反射或按需实例化。
- 不实现类型级归一化语言或通用编译期值求值器。
- 不从任意 C callback 源码推断 PURE、ASSOCIATIVE 或真实 ABI。
- 不在普通 C/C++ configure、build 或运行时引入 Lean 依赖。
- 不改变 `CMETA_USER_TYPE_LIST`、`CMETA_USER_*_RELATION_LIST` 的扩展契约。
- 不为下游自定义 manifest 提供稳定工具 ABI；本阶段只建立仓库内建事实源。

## 架构

```text
BuiltinSignatures.lean                 唯一内建事实源
        │
        ├── validate / theorem         合法性与规模证明
        │
        └── SignatureHeader.render     确定性文本 lowering
                     │
                     ▼
cmeta/generated/builtin_signature_manifest.h
        │
        ├── types.h                    类型行与 built-in type list
        └── relations.h                三类 finite relation list
```

Lean 工具属于控制面。生成头作为源码产物提交并安装，普通 C 编译只消费该头。

## Manifest 模型

```lean
structure CTypeRow where
  token : String
  cType : String
  descriptor : String
  kind : String
  traits : String

structure UnaryRelation where
  input : String
  output : String

structure BinaryRelation where
  left : String
  right : String
  output : String

structure GeneratorRelation where
  input : String
  output : String

structure SignatureManifest where
  types : List CTypeRow
  unary : List UnaryRelation
  binary : List BinaryRelation
  generators : List GeneratorRelation
```

relation 使用 type token 引用 `types`。校验先验证每类列表非空和无重复，再验证所有
引用都存在；任何失败都不生成部分头文件，也不回退到 `N³` full profile。

## 生成的 C 契约

生成头定义：

```text
CMETA_ROW_B ... CMETA_ROW_D
CMETA_BUILTIN_TYPE_LIST
CMETA_BUILTIN_UNARY_RELATION_LIST
CMETA_BUILTIN_BINARY_RELATION_LIST
CMETA_BUILTIN_GENERATOR_RELATION_LIST
CMETA_BUILTIN_TYPE_COUNT
CMETA_BUILTIN_UNARY_RELATION_COUNT
CMETA_BUILTIN_BINARY_RELATION_COUNT
CMETA_BUILTIN_GENERATOR_RELATION_COUNT
```

前九个既有宏的展开顺序和 token 必须保持不变。四个 count 是新增的只读编译期常量，
用于验证和诊断，不参与 runtime 状态。

`types.h` 继续拥有 `CMETA_BOOL_TYPE` 的 C/C++ policy，然后包含生成头。
`relations.h` 继续拥有用户扩展和 built-in/user 合并逻辑。

## ABI 与兼容性

- 内建 type/relation 顺序不变，因此默认配置的 `cmeta_sig` 数值和 callable union
  surface 不变。
- 用户扩展宏名、拼接顺序与 source contract 不变。
- 生成头属于安装头的一部分；CMake 现有递归 `*.h` 安装规则自动包含它。
- 修改 manifest 的 relation 顺序或集合仍可能改变配置 ABI，必须经过现有 CMeta
  C/C++ 回归和下游一致配置验证。
- 本阶段不声称 count 或 manifest 能检测任意下游自定义宏造成的 ABI 不一致。

## 状态、所有权与错误

- `BuiltinSignatures.lean` 是内建事实唯一主源。
- 生成头是可重建派生数据，禁止反向手工维护。
- generator 不持有 runtime 状态，不分配 C runtime 资源。
- `--check` 遇到文件缺失或内容不一致返回非零。
- manifest 非法时返回明确 `ManifestError`，不写文件、不输出半成品、不 fallback。

## 构建与 CI

Lake 增加 `cmeta-signature-gen` executable：

```text
lake exe cmeta-signature-gen --stdout
lake exe cmeta-signature-gen --write <path>
lake exe cmeta-signature-gen --check <path>
```

formal CI 在 `lake build/test` 后运行 `--check`。workflow path filter 同时覆盖 Lean
manifest、生成头和消费它的两个 CMeta public headers。

普通 CMake 不增加 custom command，也不把 Lake/Lean 变成 configure 前置条件。

## 验证范围

1. Lean：合法内建 manifest、重复 relation、未知 token、空列表、规模证明、确定性
   header rendering。
2. Generator：`--check` 对当前生成物成功；内容漂移时非零。
3. C：生成 count 与默认 `CMETA_SIG_COUNT`、已注册 binary symbol 和 runtime
   descriptor 一致。
4. C++17：公共头继续编译。
5. 回归：全部 CMeta CTest 与 `lake test`。
6. Proof hygiene：formal 目录不存在 `sorry`、`admit` 或 `axiom`。

## 迁移与回滚

迁移仅把既有内建宏事实移动到生成头，不要求调用方修改源码。若 generator 或 CI
接入出现问题，可恢复 `types.h` / `relations.h` 的手写定义并删除生成工具；runtime
和数据格式没有迁移，因此回滚不需要状态转换。
