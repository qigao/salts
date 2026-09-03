# CMeta Finite Compute Design

**状态：** Accepted for implementation

**分支：** `feat/cmeta-finite-compute`

**基线：** `origin/master` at `62ddcd8`

**日期：** 2026-08-23

## 1. 目的

在不引入 C++ 模板语法、不修改 C 语法且不增加运行时分配的前提下，为 CMeta 增加有限编译期计算：

- 一至三元类型函数；
- 一至三元整数常量函数；
- 一元 predicate 与编译期约束；
- 现有单列表达式 `Schema` 的 count/all/any 聚合；
- Lean 对有限关系函数性、域覆盖与输出闭包的可复用定义和证明。

该能力是 C11 头文件表面。`Salts::CMeta` 静态库继续拥有运行时描述符与协议，不参与编译期求值。

## 2. 公开语义

类型函数把有限输入 token 映射到 C 类型：

```c
TypeFunction(CommonArithmetic,
    (int, int, int),
    (int, double, double),
    (double, int, double),
    (double, double, double)
);

typedef TypeEval(CommonArithmetic, int, double) result_type;
```

值函数把有限输入 token 映射到整数常量表达式：

```c
ValueFunction(TypeRank,
    (int, 1),
    (double, 2)
);

enum { rank = ValueEval(TypeRank, double) };
```

Predicate 是布尔值函数的语义别名：

```c
Predicate(Hashable,
    (int, 1),
    (opaque, 0)
);

Require(Hashable, int);
```

缺失映射必须在 C 编译阶段因未知生成标识符而失败；冲突映射必须因生成 typedef 或枚举常量冲突而失败。没有 fallback。

## 3. 输入约束

- 函数名和输入 key 必须是单个预处理标识符。
- 带空格、逗号或声明符语法的 C 类型必须先定义稳定 typedef，再把 typedef 名作为 key。
- 输出类型可以是宏 mapper 能放入 `typedef` 声明的类型；复杂声明符仍应先 typedef。
- 每个声明接受 1–16 行，与 `CMETA_PP_FOR_EACH` 的现有合同一致；更大的关系由同名函数的多个声明分片组成。
- 值输出必须是 C/C++ 可接受的整数常量表达式。

这些约束让查询仅做 token 拼接，不扫描关系，也不引入 N²/N³ 的查询时展开。

## 4. Schema 聚合

`SchemaCount(schema)` 适用于任意非空 row schema，因为它只计算行数。

`SchemaAll(schema)` 与 `SchemaAny(schema)` 只接受每行恰好一个整数常量表达式的 schema：

```c
#define FeatureChecks(M) Schema(M, (1), (1), (0))

enum {
    feature_count = SchemaCount(FeatureChecks),
    every_feature = SchemaAll(FeatureChecks),
    some_feature = SchemaAny(FeatureChecks)
};
```

Predicate 投影由调用者在 schema 事实源中显式表达，例如 `(Satisfies(Hashable, int))`。v1 不改变 `Replay(schema, M)` 的 mapper 签名，也不引入隐藏 predicate 上下文。

## 5. 实现分层

```text
cmeta/compute.h
  ├─ TypeFunction + TypeEval
  ├─ ValueFunction + ValueEval
  ├─ Predicate / Satisfies / Require
  └─ SchemaCount / SchemaAll / SchemaAny

formal/CMeta/FiniteCompute.lean
  ├─ FiniteRelation
  ├─ Functional
  ├─ TotalOn
  └─ ClosedOver
```

`meta.h` 聚合公开 `compute.h`。实现不修改 CMeta ABI，不增加静态库对象，不依赖 re2c。

## 6. Lean 边界

Lean 对显式给出的有限 row list 证明：

- 同一输入最多有一个输出；
- 声明域中的每个输入都有结果；
- 所有结果属于允许输出集合；
- lookup 的成功结果属于原关系。

Lean 不证明 C 预处理器或编译器正确，也不自动证明任意用户在 C 文件里声明的 `TypeFunction*`。内建关系以后可从同一 Lean manifest 生成，再增加 drift check；v1 先建立可复用证明语义。

## 7. 兼容性与影响

- **公开接口：** 仅新增，不改变现有宏、结构布局、枚举值或符号。
- **运行时行为：** 无变化；所有计算产物都是 typedef、enum 或常量表达式。
- **编译性能：** 声明成本与关系行数线性；单次查询是固定 token 拼接。实际编译时间只通过前后测量报告，不从复杂度直接推断百分比。
- **多 TU：** 产物是头文件局部声明，不依赖地址身份或全局自动注册。
- **迁移成本：** 现有调用者无需修改；采用者逐步把手写类型/值选择表迁移为有限函数。

## 8. 风险

- `HIGH`：不得为缺失映射增加静默默认类型，否则会掩盖关系未闭合。
- `MED`：生成标识符可能因不受约束的 key 拼接冲突，因此 key 限制为稳定单 token，并测试不同 arity 隔离。
- `MED`：公共宏必须同时通过严格 C11、MSVC 预处理器与 C++17。
- `LOW`：错误文本仍由 C/C++ 编译器产生，v1 只能通过稳定生成名称提高可定位性。

## 9. 验证

- C11 TinyTest/compile surface：1/2/3 元类型和值函数、predicate、require、schema 聚合、分片声明。
- C++17 public-header test：类型和值求值、`static_assert` 兼容。
- negative compile tests：缺失映射、冲突映射、失败 `Require`。
- Lean focused test、`lake test`、`lake build` 和 proof-escape 扫描。
- CMeta 相关 CTest、全仓库 build/test、`git diff --check`。
