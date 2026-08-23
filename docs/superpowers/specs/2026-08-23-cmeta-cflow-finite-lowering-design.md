# CMeta/CFlow 有限关系 Lowering 设计

## 背景

第一阶段已让 Lean manifest 拥有 CMeta 的五个内建类型和 `8/2/1` 三类
callable relations，并生成 C 头文件。CFlow 的 filter、map、transform、flatMap、
reduce 和 zip policy 仍在 `cflow/operator_policy.h` 重复手写具体签名。

同时，CMeta 的部分 runtime lowering 使用 `CMETA_ALL_SIGNATURES` 生成与当前
protocol 无关的分支：value invoke 会生成 generator 拒绝分支，generator dispatch
也会生成 unary/binary 拒绝分支。协议检查已在 switch 前完成，这些分支不承载
额外语义。

## 目标

1. 在 Lean 中表达 CFlow 六类 operator 的有限内建签名 policy。
2. 复用 CMeta 内建 relation 常量，不重复书写类型元组。
3. 验证每个 CFlow 签名均已注册，并验证 CMeta 内建 relation 全部被至少一个
   CFlow operator 使用。
4. 从 Lean 确定性生成 CFlow built-in policy 头；用户扩展宏继续由应用维护。
5. 增加 `CMETA_VALUE_SIGNATURES(U, B)` 分组，并用它裁剪 protocol 已排除的展开。
6. 保持 `cmeta_sig` 编号、CFlow admission、错误返回和普通 CMake 构建行为不变。
7. 用同一 MSVC `/O2` 编译单元比较改动前后的预处理字节和目标文件大小。

## 非目标

- 不让 CMeta 依赖 CFlow；CMeta registry 仍是下层独立事实源。
- 不删除 balanced/full profile，也不改变自定义 signature policy。
- 不依据有限性推导 PURE、ASSOCIATIVE、IDEMPOTENT 等语义性质。
- 不把 CFlow operator policy 变成运行时可变状态。
- 不优化 storage、并发、range 生命周期或 plan execution 算法。

## 架构

```text
CMeta/BuiltinSignatures.lean
        │ named relation constants
        ├──────────────────────────────┐
        ▼                              ▼
CMeta builtin manifest          CFlow/BuiltinOperatorPolicy.lean
        │                              │ validate closure + coverage
        ▼                              ▼
cmeta/generated/...             cflow/generated/builtin_operator_policy.h
                                       │
                                       ▼
                              cflow/operator_policy.h + user hooks
```

CFlow 层依赖 CMeta relation 值；CMeta 不读取 CFlow policy。`coverage` 定理只证明
当前 CMeta 内建关系没有闲置项，不把 CFlow 当作 CMeta registry 的生成输入。

## Lean 数据模型

```lean
structure OperatorPolicyManifest where
  filter : List UnaryRelation
  map : List UnaryRelation
  transform : List UnaryRelation
  flatMap : List GeneratorRelation
  reduce : List BinaryRelation
  zip : List BinaryRelation

inductive OperatorPolicyError where
  | invalidRegistry
  | emptyOperator
  | duplicateOperatorSignature
  | unregisteredSignature
  | invalidOperatorShape
  | registryNotCovered
```

`validate registry policy` 依次执行：六类非空、各列表内部无重复、policy 到 registry
的包含关系、filter 返回 bool、reduce 满足 `T(T,T)`、registry 到所有 policy 列表
并集的覆盖关系。列表规模固定为
`1/7/1/1/1/1`，结构扫描最坏 O(n²)，只发生在 Lean 构建控制面。

CMeta 内建 relation 改为具名 Lean 常量；CMeta manifest 与 CFlow policy 都引用这些
值，从而不重复书写 `(I,L)` 等类型事实。

## 生成的 CFlow 契约

生成头定义：

```text
CFLOW_BUILTIN_FILTER_SIGNATURE_LIST
CFLOW_BUILTIN_MAP_SIGNATURE_LIST
CFLOW_BUILTIN_TRANSFORM_SIGNATURE_LIST
CFLOW_BUILTIN_FLAT_MAP_SIGNATURE_LIST
CFLOW_BUILTIN_REDUCE_SIGNATURE_LIST
CFLOW_BUILTIN_ZIP_SIGNATURE_LIST
CFLOW_BUILTIN_FILTER_SIGNATURE_COUNT
CFLOW_BUILTIN_MAP_SIGNATURE_COUNT
CFLOW_BUILTIN_TRANSFORM_SIGNATURE_COUNT
CFLOW_BUILTIN_FLAT_MAP_SIGNATURE_COUNT
CFLOW_BUILTIN_REDUCE_SIGNATURE_COUNT
CFLOW_BUILTIN_ZIP_SIGNATURE_COUNT
```

前六个宏保持现有 token、顺序和惰性展开形式。后六个 count 是新增只读常量。
`operator_policy.h` 保留 relation 构造宏、所有 `CFLOW_USER_*` hook、built-in/user
拼接和小写 facade alias。

## Protocol-specific lowering

`signatures.h` 新增：

```c
#define CMETA_VALUE_SIGNATURES(U, B) \
    CMETA_UNARY_SIGNATURES(U) \
    CMETA_BINARY_SIGNATURES(B)
```

`CMETA_ALL_SIGNATURES` 复用该分组。以下位置改用精确 family：

- value typed invoker 只定义 unary/binary adapter；generator `_Generic` association
  统一映射到现有 unsupported adapter。
- `cmeta_fn_invoke` 只展开 unary/binary case，default 保持 `false`。
- `cmeta_fn_generate` 只展开 generator case，default 保持 `CMETA_GEN_ERROR`。

签名 descriptor、符号表、target validation、target equality、maker 和 ABI union 仍需
完整 signature universe，不做裁剪。

## 状态、ABI 与错误语义

- Lean manifests 是事实源；两个生成头都是可重建派生数据。
- CFlow 用户 policy 仍由共享配置头提供，所有 translation unit 必须一致。
- 内建列表顺序不变，因此 `cmeta_sig` 数值和 `CMETA_SIG_COUNT` 不变。
- `cmeta_fn_invoke(generator)` 继续返回 `false`；
  `cmeta_fn_generate(value)` 继续返回 `CMETA_GEN_ERROR`。
- manifest 非法或生成物漂移时 fail fast，不写部分输出，不切换到 full profile。

## 性能基线与验证

第一阶段提交 `5df5ba2` 的 MSVC `/O2` 单编译单元基线：

| profile | `cmeta.c` 预处理字节 | `cmeta.c.obj` 字节 |
|---|---:|---:|
| relation | 183064 | 39880 |
| balanced | 257555 | 74481 |
| full | 392069 | 121791 |

本阶段只声明编译产物裁剪收益，不宣称运行时吞吐改善。相同脚本和工具链复测；功能
验证覆盖 Lean、CMeta、CFlow、C++ public headers、生成物漂移与 proof hygiene。

相同环境复测结果：

| profile | 新预处理字节 | 变化 | 新 object 字节 | 变化 |
|---|---:|---:|---:|---:|
| relation | 182238 | -826 (-0.45%) | 39566 | -314 (-0.79%) |
| balanced | 250235 | -7320 (-2.84%) | 74250 | -231 (-0.31%) |
| full | 379949 | -12120 (-3.09%) | 121460 | -331 (-0.27%) |

编译器原本已消除部分不可达 runtime case，因此 object 收益有限；本改动的主要收益
是减少预处理展开并让 protocol 边界在源代码中精确表达。

## 兼容性风险

- `MED`：生成 policy token 或顺序漂移会改变 CFlow admission；用逐字节 drift check
  和六类运行时 admission 测试拦截。
- `MED`：误删仍需完整 universe 的展开会破坏 ABI 或 `_Generic`；只裁剪 protocol
  已提前排除的 invoke/generate 分支。
- `LOW`：新增 count 与 grouping 宏扩展 public header surface，但不改变既有宏展开。

## 回滚

可恢复 `operator_policy.h` 的六个手写 built-in 列表并删除 CFlow generator；CMeta
protocol refactor 可独立恢复为 `CMETA_ALL_SIGNATURES`。没有数据迁移或运行时状态，
两个回滚方向互不依赖。
