# CMeta–CFlow Calculus v1 Design

**状态：** Proposed

**分支：** `formal/cmeta-cflow-calculus-v1-v2`

**基线：** `master`（已包含 #48 execution foundation）

**日期：** 2026-08-22

## 1. 目的

本规范把 **CMeta–CFlow Calculus** 定义为 CMeta/CFlow/Execution Kernel 的理论规范层。

设计顺序必须固定为：

```text
Calculus
   ↓
Architecture validity proof
   ↓
Rewrite laws
   ↓
Efficiency-path derivation
   ↓
Graph / Plan / Kernel implementation
```

换言之：

> Calculus 是 specification；CMeta DSL、Graph IR、Plan、Execution Kernel 与 Stream 都是该 specification 的具体实现或 surface。

本 PR 的第一阶段只定义 Calculus 与证明目标，不修改 `cmeta/`、`cflow/`、`platform/`、`concurrency/` 的 C 实现。

---

## 2. 设计原则

1. **先语义，后实现。**
   不允许用当前 C 结构体布局反向定义 Calculus。

2. **CMeta 提供事实，不充当证明器。**
   类型、Trait、Effect、Property、Ownership、Contract 构成语义环境 `Γ`。

3. **CFlow Graph 是 term，不是 optimizer 本身。**
   Graph IR 应被解释为 CFlow 表达式的结构化编码。

4. **Execution Kernel 提供 operational semantics。**
   WAIT/Wake、Demand、Cancel、Executor boundary 等属于执行语义，而不是普通 Flow 等式。

5. **优化必须先证明等价，再比较代价。**
   “看起来更快”不是合法 rewrite 的充分条件。

6. **效率先使用 symbolic cost，再使用 benchmark 校准。**
   Calculus 证明结构性的成本下降；真实机器时间由 benchmark 评价。

7. **程序员声明是 trusted premise。**
   `PURE`、`ASSOCIATIVE`、`IDEMPOTENT` 等是优化前提，不由 CMeta 自动证明。

8. **Lean 证明规则，不进入运行时。**
   Lean 用来证明 calculus theorem 与 rewrite soundness；C optimizer 只应用已被接受的规则。

---

## 3. 四个核心域

### 3.1 CMeta semantic environment

记为：

```text
Γ
```

包含：

```text
Type identity
Traits
Callable signatures
Effects
Properties
Contracts
Ownership capabilities
Range properties
Collector properties
```

示例：

```text
Γ ⊢ normalize : User → User
    ! PURE
    [DETERMINISTIC, TOTAL]

Γ ⊢ sum : Int × Int → Int
    ! PURE
    [ASSOCIATIVE]

Γ ⊢ users : Range(User)
    [SIZED, ORDERED, REUSABLE]
```

### 3.2 Kernel capability environment

记为：

```text
K
```

包含执行环境可用能力，例如：

```text
SYNC_EXECUTION
WAITABLE
TIMER
PARALLEL_EXECUTOR
SPLITTABLE_SOURCE
BATCH_EXECUTION
CONTIGUOUS_ACCESS
PROCESS_SHARED_WAIT
```

`K` 描述“当前执行环境允许什么”，而不是“程序想做什么”。

### 3.3 Runtime resource state

记为：

```text
Σ
```

包含运行中的资源状态：

```text
value ownership
outstanding demand
wait registration
cancellation
terminal state
buffer state
continuation ownership
```

### 3.4 Cost domain

记为：

```text
C_K(E)
```

表示表达式 `E` 在执行能力 `K` 下的 symbolic execution cost。

---

## 4. CMeta Calculus

### 4.1 Ownership kind

v1 定义：

```text
O ::= borrowed | owned | moved
```

基本 judgement：

```text
Γ ⊢ v : T @ O
```

例如：

```text
Γ ⊢ v : User @ borrowed
Γ ⊢ w : User @ owned
```

### 4.2 Callable judgement

```text
Γ ⊢ f : A → B ! ε [P]
```

其中：

```text
ε = effects
P = properties/contracts
```

二元 callable：

```text
Γ ⊢ f : A × B → C ! ε [P]
```

### 4.3 Effect lattice

v1 只要求最小 effect 体系：

```text
PURE
READ_ONLY
STATEFUL
EXTERNAL
```

并满足：

```text
PURE ⊑ READ_ONLY ⊑ STATEFUL ⊑ EXTERNAL
```

该偏序表示可观察行为约束逐渐增大，而不是执行成本大小。

### 4.4 Property set

v1 至少使用：

```text
DETERMINISTIC
TOTAL
IDEMPOTENT
ASSOCIATIVE
COMMUTATIVE
STABLE
```

这些 property 是推导前提。

### 4.5 Type capabilities

类型可声明：

```text
COPY
MOVE
DESTROY
EQUAL
COMPARE
HASH
TRIVIAL_COPY
TRIVIAL_DESTROY
NO_ALIAS
```

---

## 5. Flow Calculus Syntax

v1 的 Flow term：

```text
F ::=
    Source(S)

  | Map(f, F)
  | Filter(p, F)
  | FlatMap(f, F)

  | Limit(n, F)
  | Skip(n, F)

  | Reduce(f, F)
  | Collect(c, F)
```

v1 **刻意不包含**：

```text
Relation
Zip
Distinct
Sorted
Window
Merge
Parallel
Reactive Publisher/Subscriber
```

这些在 v1 soundness 闭环完成后扩展。

### 5.1 Stream surface

例如：

```c
stream(&users, &s)
    ->filter(&s, active)
    ->map(&s, normalize)
    ->map(&s, name)
    ->to_list(&s, &names, 100u);
```

其 surface semantics 是：

```text
Collect(
  names,
  Limit(
    100,
    Map(
      name,
      Map(
        normalize,
        Filter(
          active,
          Source(users))))))
```

Stream API 不是理论核心；它只是 term construction notation。

---

## 6. Flow Typing Judgement

主 judgement：

```text
Γ ; K ⊢ F :
    A ⇒ B
    ! ε
    [P]
    {O}
    ⟨Card, Order, Exec⟩
```

含义：

- 输入元素类型 `A`
- 输出元素类型 `B`
- 可观察 effect `ε`
- 可推导 property `P`
- ownership 行为 `O`
- cardinality / encounter order / execution class

### 6.1 Source

若：

```text
Γ ⊢ S : Source(A)
```

则：

```text
Γ ; K ⊢ Source(S) : A ⇒ A
```

### 6.2 Map

若：

```text
Γ ; K ⊢ F : A ⇒ B
Γ ⊢ f : B → C ! εf [Pf]
```

则：

```text
Γ ; K ⊢ Map(f,F) : A ⇒ C
```

### 6.3 Filter

若：

```text
Γ ; K ⊢ F : A ⇒ B
Γ ⊢ p : B → Bool ! εp [Pp]
```

则：

```text
Γ ; K ⊢ Filter(p,F) : A ⇒ B
```

### 6.4 FlatMap

若：

```text
Γ ; K ⊢ F : A ⇒ B
Γ ⊢ f : B → Range(C) ! εf [Pf]
```

则：

```text
Γ ; K ⊢ FlatMap(f,F) : A ⇒ C
```

### 6.5 Reduce

若：

```text
Γ ; K ⊢ F : A ⇒ B
Γ ⊢ f : B × B → B ! εf [Pf]
```

则：

```text
Γ ; K ⊢ Reduce(f,F) : A ⇒ B
```

其 cardinality 为：

```text
0..1
```

### 6.6 Collect

若：

```text
Γ ; K ⊢ F : A ⇒ B
Γ ⊢ c : Collector(B,R)
```

则：

```text
Γ ; K ⊢ Collect(c,F) : A ⇒ R
```

Collect 是 terminal，不再产生 Stream element flow。

---

## 7. Ownership Calculus

### 7.1 Borrowed value scope

`borrowed` value 只允许存在于当前同步 reduction/resume step 的动态作用域。

合法：

```text
Source VALUE(v@borrowed)
    ↓
Filter reads v
    ↓
Map reads v
```

不合法：

```text
v@borrowed
   ↓
Buffer / WAIT / ExecutorBoundary / Continuation
```

### 7.2 Retain rule

若：

```text
Γ ⊢ v : T @ borrowed
Γ ⊢ T : COPY
```

则：

```text
Γ ⊢ retain(v) : T @ owned
```

### 7.3 Move rule

若：

```text
Γ ⊢ v : T @ owned
Γ ⊢ T : MOVE
```

则：

```text
Γ ⊢ move(v) : T @ moved
```

并且原 binding 不再可读。

### 7.4 Suspension rule

如果表达式可能跨 suspension point：

```text
MaySuspend(E)
```

那么其 live values 必须满足：

```text
∀v ∈ Live(E). ownership(v) = owned
```

因此：

```text
Γ ; K ⊬ Suspend(v@borrowed)
```

### 7.5 Fundamental ownership theorem target

目标 theorem：

```text
WellOwned(E)
∧ E →* WAIT
────────────────
NoBorrowedValueEscapes(E)
```

以及：

```text
WellOwned(E)
∧ E →* Terminal
────────────────
EveryOwnedValueDestroyedExactlyOnce
```

第二个定理在 v1 可先证明抽象资源模型，而不证明 C `malloc/free` 实现。

---

## 8. Execution Calculus

### 8.1 Source step

Source small-step：

```text
⟨S, Σ⟩ → VALUE(v, S', Σ')
⟨S, Σ⟩ → VALUE_AND_DONE(v, Σ')
⟨S, Σ⟩ → WAIT(w, S', Σ')
⟨S, Σ⟩ → DONE(Σ')
⟨S, Σ⟩ → ERROR(e, Σ')
```

### 8.2 WAIT/Wake

当：

```text
⟨S, Σ⟩ → WAIT(w,S',Σ')
```

Kernel：

```text
arm(w, wake_token)
```

进入：

```text
SUSPENDED(wake_token, S')
```

收到合法 wake：

```text
wake(wake_token)
```

转回：

```text
READY(S')
```

并重新执行 source step。

### 8.3 Lost-wakeup requirement

Calculus 不假设某一种 OS Event 实现，但要求 wait registration 满足：

```text
SignalBeforeArm
or
SignalConcurrentWithArm
```

都不能使已经成立的 readiness 永久丢失。

具体 Platform Event / Poller 负责证明它满足该 abstraction contract。

### 8.4 Demand

runtime state：

```text
demand(Σ) ∈ Nat
```

只有：

```text
demand(Σ) > 0
```

才能向 Sink 产生一个 downstream value。

规则：

```text
demand = n + 1
VALUE(v)
────────────────────
emit(v)
demand := n
```

如果：

```text
demand = 0
```

则 Kernel 不允许：

```text
Sink.value(...)
```

### 8.5 Terminal

终态：

```text
DONE
ERROR
CANCELLED
```

必须满足：

```text
Terminal(Σ)
────────────────────
NoFurtherValue
NoFurtherError
NoFurtherDone
```

即 terminal signal 之后不存在新的可观察 signal。

---

## 9. Observational Semantics

定义：

```text
Obs_K(E)
```

为表达式在执行环境 `K` 下的可观察 trace。

v1 trace 至少包含：

```text
Value(v)
Effect(e)
Error(e)
Done
Cancelled
```

其中 values 保留 encounter order。

不把以下内部事件作为普通用户观察：

```text
Graph node id
allocation address
executor task id
internal scratch buffer
internal wake token
```

### 9.1 Observational equivalence

定义：

```text
Γ ; K ⊢ E1 ≈obs E2
```

当且仅当在满足 Γ 前提的输入上：

```text
Obs_K(E1) = Obs_K(E2)
```

需要同时保持：

```text
value sequence
encounter order
observable effect order
error outcome
completion/cancellation semantics
ownership safety
```

因此：

```text
same final list
```

不自动意味着 observationally equivalent。

---

## 10. Architecture Validity Proof Obligations

实现链：

```text
Stream Surface
    ↓ lower
Graph
    ↓ normalize
Normalized Graph
    ↓ optimize
Optimized Graph
    ↓ compile
Plan
    ↓ execute
Execution Kernel
```

Calculus 要求建立以下 theorem family。

### 10.1 Surface soundness

```text
WellTyped(surface)
────────────────────────────
WellTyped(lower(surface))
```

且：

```text
Obs(surface) = Obs(lower(surface))
```

### 10.2 Normalize soundness

```text
Valid(G)
normalize(G)=N
────────────────
G ≈obs N
```

### 10.3 Rewrite soundness

对每条 rewrite rule `R`：

```text
PremisesΓ,K(R)
────────────────────
lhs(R) ≈obs rhs(R)
```

### 10.4 Optimize soundness

如果 optimizer 只应用 sound rules：

```text
optimize(G)=G'
────────────────
G ≈obs G'
```

### 10.5 Plan compilation soundness

```text
compile(G)=P
────────────────
G ≈obs P
```

### 10.6 Kernel execution soundness

```text
WellFormed(P)
Capabilities(K,P)
────────────────────────
Obs(P)=Obs(execute(K,P))
```

最终目标：

```text
Obs(Stream)
 =
Obs(Graph)
 =
Obs(NormalizedGraph)
 =
Obs(OptimizedGraph)
 =
Obs(Plan)
 =
Obs(KernelExecution)
```

---

## 11. Rewrite Calculus v1

Rewrite judgement：

```text
Γ ; K ⊢ E1 ⇒ E2
```

仅表示**合法等价 rewrite**。

性能 rewrite：

```text
Γ ; K ⊢ E1 ⇒ E2 ▷ ΔC
```

另外要求：

```text
C_K(E2) ≺ C_K(E1)
```

### R1 — Map identity

若：

```text
id : A → A
PURE(id)
TOTAL(id)
```

则：

```text
Map(id,X)
≈obs
X
```

### R2 — Map fusion

若：

```text
Γ ⊢ f : A → B ! PURE [TOTAL]
Γ ⊢ g : B → C ! PURE [TOTAL]
```

则：

```text
Map(g, Map(f,X))
≈obs
Map(g ∘ f, X)
```

### R3 — Filter true

若 predicate：

```text
p(x) = true
PURE(p)
TOTAL(p)
```

则：

```text
Filter(p,X)
≈obs
X
```

### R4 — Filter fusion

若：

```text
p,q : A → Bool
PURE(p), PURE(q)
TOTAL(p), TOTAL(q)
```

则：

```text
Filter(q, Filter(p,X))
≈obs
Filter(λx. p(x) && q(x), X)
```

组合 predicate 必须保持 `p` 后 `q` 的 evaluation order。

### R5 — Limit zero

```text
Limit(0,X)
≈obs
Empty
```

但如果 `X` construction 本身具有 surface-visible side effect，则必须保持 source construction semantics；该规则只消除 element traversal。

### R6 — Nested Limit

```text
Limit(a, Limit(b,X))
≈obs
Limit(min(a,b), X)
```

### R7 — Skip zero

```text
Skip(0,X)
≈obs
X
```

### R8 — Nested Skip

```text
Skip(a, Skip(b,X))
≈obs
Skip(sat_add(a,b), X)
```

`sat_add` 明确定义为不产生无符号溢出的饱和加法。

### R9 — Limit push through pure total Map

若：

```text
PURE(f)
TOTAL(f)
```

则：

```text
Limit(n, Map(f,X))
≈obs
Map(f, Limit(n,X))
```

该规则允许减少 callback 次数。

若 `f` 可失败或有 effect，则不成立。

### R10 — Reduce reassociation

若：

```text
Γ ⊢ f : A × A → A
PURE(f)
TOTAL(f)
ASSOCIATIVE(f)
```

则：

```text
LeftReduce(f,X)
≈obs
TreeReduce(f,X)
```

该规则不自动意味着 parallel execution。

### R11 — Parallel reduce eligibility

若同时：

```text
ASSOCIATIVE(f)
PURE(f)
TOTAL(f)
NO_ALIAS(A)

K ⊨ PARALLEL_EXECUTOR
K ⊨ SPLITTABLE_SOURCE
```

则允许 lowering：

```text
Reduce(f,X)
⇒exec
ParallelReduce(f,X)
```

如果 encounter order 本身对 reducer 可观察，则还必须满足对应 order independence premise；仅 ASSOCIATIVE 不自动授予任意 permutation 权限。

### R12 — Collect preallocation

若：

```text
Sized(X,n)
Collector(c) supports reserve
```

则：

```text
Collect(c,X)
```

可以 lowering 为：

```text
Reserve(c,n);
Collect(c,X)
```

该规则是 execution refinement，不改变 Flow observational semantics。

### R13 — No scheduler on synchronous closed segment

若 segment `E` 满足：

```text
NoWait(E)
NoExecutorBoundary(E)
NoAsyncBuffer(E)
```

则：

```text
K ⊢ E : SYNC_CLOSED
```

允许：

```text
execute(E) ⇒ DirectExecute(E)
```

并且 scheduler-hop symbolic cost 为 0。

### R14 — Batch eligibility

若：

```text
Contiguous(Source)
TrivialCopy(T)
NoWait(E)
K ⊨ BATCH_EXECUTION
```

则 linear segment 可 lowering 为 batch kernel。

### R15 — Async promotion

任何跨：

```text
WAIT
Buffer
Continuation
ExecutorBoundary
```

的 live borrowed value 必须先执行：

```text
retain
```

这不是性能 rewrite，而是 execution well-formedness rule。

---

## 12. 明确禁止的无条件 rewrite

以下规则 **不得** 无条件加入 optimizer：

```text
Map ↔ Filter reorder
Filter predicate reorder
Map across ExecutorBoundary
Buffer removal
WAIT removal
Parallelization
Reduce permutation
FlatMap reassociation with effects
```

例如：

```text
Map(f, Filter(p,X))
```

不能仅凭 `PURE(f)` 与 `PURE(p)` 就变成：

```text
Filter(p', Map(f,X))
```

除非还能证明 predicate transport、type relation 与 error/effect semantics。

---

## 13. Cost Calculus

### 13.1 Symbolic cost vector

v1：

```text
Cost(E) = ⟨
    allocations,
    allocated_bytes,
    copies,
    copied_bytes,
    callback_dispatches,
    atomic_ops,
    scheduler_hops,
    wakeups,
    syscalls,
    memory_passes
⟩
```

### 13.2 Dominance

定义：

```text
C2 ≺ C1
```

当：

```text
每个维度 C2 <= C1
并且至少一个维度严格 <
```

即 Pareto dominance。

因此 calculus 可以严格推出：

```text
per-item allocation
```

被：

```text
fixed scratch slot
```

支配，而无需声称具体快多少纳秒。

### 13.3 Profile-weighted extraction

真实 target 可以定义：

```text
W_K
```

从 benchmark 得到权重：

```text
MeasuredCost_K(E) = W_K · Cost(E)
```

权重只影响“在多个合法等价形式中选谁”，不影响 rewrite soundness。

### 13.4 Cost proof 与 benchmark 的边界

Calculus 能证明：

```text
allocations: N → 0
scheduler_hops: N → 0
memory_passes: 3 → 1
```

不能仅凭形式系统证明：

```text
运行时间一定减少 23.7%
```

真实 wall-clock/throughput/latency 必须通过 benchmark 验证。

---

## 14. Efficiency-Path Derivation

目标不是维护三套互不相关的 runtime，而是从 Calculus 推导 execution class。

### 14.1 Direct path

若：

```text
WellTyped(E)
NoWait(E)
NoExecutorBoundary(E)
NoAsyncBuffer(E)
Linear(E)
```

则：

```text
Γ ; K ⊢ E : DIRECT_ELIGIBLE
```

可以 lowering 为：

```text
tight native loop
```

不进入 scheduler。

### 14.2 Plan path

若：

```text
Static(E)
NoDynamicWait(E)
```

但不满足 simple Direct 条件，则：

```text
Γ ; K ⊢ E : PLAN_ELIGIBLE
```

lower 为 compact execution Plan。

### 14.3 Kernel path

若：

```text
MayWait(E)
∨ HasExecutorBoundary(E)
∨ HasAsyncBuffer(E)
```

则：

```text
Γ ; K ⊢ E : KERNEL_REQUIRED
```

由完整 Source/Sink/Run/Waitable/Waker 状态机执行。

### 14.4 Example derivation

输入：

```text
Collect(
  c,
  Limit(100,
    Map(g,
      Map(f,
        Filter(p,
          Source(xs))))))
```

若：

```text
PURE/TOTAL(p,f,g)
Contiguous(xs)
TrivialCopy(T)
NoWait
```

推导：

```text
Map(g,Map(f,X))
  ⇒ Map(g∘f,X)

Limit(100,Map(h,X))
  ⇒ Map(h,Limit(100,X))

SYNC_CLOSED
  ⇒ DirectExecute

CONTIGUOUS + TRIVIAL_COPY
  ⇒ BatchEligible
```

最终路径：

```text
Surface Graph
   ↓
fused linear term
   ↓
direct batch plan
   ↓
native data loop
```

这就是“效率路径由 Calculus 推导”，而不是 runtime 中分散的 heuristic。

---

## 15. FlatMap v1 边界

v1 保留 `FlatMap` syntax/type semantics，但暂不定义大规模重写规则。

原因：

```text
generator lifetime
inner range ownership
error timing
cancellation
encounter order
```

都会影响 observational equivalence。

v1 只要求证明：

```text
FlatMap typing soundness
FlatMap ownership soundness
FlatMap execution preservation
```

Monad-style associativity law 推迟到 v2。

---

## 16. Trusted Base

Calculus 的 trusted base 必须明确。

### 16.1 用户/库作者声明

例如：

```text
PURE(f)
ASSOCIATIVE(sum)
TOTAL(parse)
```

是 trusted premise。

Lean 证明：

```text
IF ASSOCIATIVE(sum)
THEN tree reduction preserves semantics
```

Lean 不自动证明任意 C callback 的 associativity。

### 16.2 Platform assumptions

Platform backend 必须实现并测试：

```text
wait/wake contract
clock monotonicity contract
executor submission contract
```

Calculus 对这些 backend 只引用 capability theorem。

### 16.3 C implementation

C implementation 必须通过 conformance test / model test 证明它符合 calculus abstraction。

---

## 17. Lean Formalization Target

后续 Lean 实现应独立于 C runtime 目录。

建议结构：

```text
formal/
  cmeta_cflow_calculus/
    CMeta/
      Types.lean
      Effects.lean
      Properties.lean
      Ownership.lean

    CFlow/
      Syntax.lean
      Typing.lean
      Observation.lean
      Execution.lean
      Cost.lean

    Proofs/
      Ownership.lean
      SurfaceSoundness.lean
      Rewrite.lean
      ExecutionSoundness.lean
      CostDerivation.lean
```

文件名最终服从仓库既有 Lean 工程结构；本规范定义职责而不是强制物理布局。

### 17.1 第一批 Lean theorem

优先级：

```text
1. ownership retain/suspend safety
2. map identity
3. map fusion
4. filter fusion
5. nested limit
6. nested skip
7. limit/map pushdown
8. reduce reassociation
9. demand never over-emits
10. terminal produces no later signals
11. wait/wake preserves suspended computation
12. sync-closed execution observational equivalence
```

---

## 18. 与现有 CMeta/CFlow 的关系

当前实现可以继续演进。

本 PR 不要求立即重写已有代码。

之后的 implementation/conformance PR 应遵循：

```text
现有 implementation
      ↓ map
Calculus terms/judgements
      ↓
发现不匹配
      ↓
修改 implementation
```

而不是：

```text
现有 implementation
      ↓
修改 Calculus 迁就实现
```

例外只有两类：

1. calculus 本身存在错误；
2. 现有 public semantic 是明确必须保留的兼容性合同。

---

## 19. 本 PR 与 execution-foundation 的隔离

本工作采用独立分支：

```text
formal/cmeta-cflow-calculus-v1-v2
```

base：

```text
master
```

允许修改：

```text
docs/superpowers/specs/
formal/ 或仓库既有 Lean proof 目录
formal-specific CI/config
```

v1 PR 不修改：

```text
cmeta/src
cmeta/include public API
cflow/src
cflow/include public API
platform/
concurrency/
turbostl/
utils/
```

因此该 PR 可以独立审查理论有效性，不和 execution-foundation 的 C 实现变更发生代码冲突。

---

## 20. 非目标

v1 不试图证明：

- 任意 C callback 的 purity；
- 任意 reducer 的 associativity；
- 实际 CPU cycle 数；
- 完整 Rx/Reactive Streams；
- Actor/Statechart/Workflow；
- Relation/Zip/Sorted/Distinct；
- 多核 scheduler 最优性；
- OS kernel correctness；
- memory allocator correctness；
- C compiler correctness。

---

## 21. v1 验收标准

Calculus v1 完成必须满足：

1. `Γ / K / Σ / Cost` 四个域被明确区分；
2. `Map/Filter/FlatMap/Limit/Skip/Reduce/Collect` 有 syntax；
3. Flow typing judgement 闭合；
4. ownership rules 能禁止 borrowed value 跨 suspension；
5. Source 的 `VALUE/WAIT/DONE/ERROR` 有 small-step semantics；
6. demand/cancel/terminal 具有可检查的不变量；
7. observational equivalence 被定义；
8. `Surface → Graph → Optimize → Plan → Kernel` 有明确 proof obligations；
9. 至少 R1–R15 的规则被分类为 semantic rewrite、execution refinement 或 well-formedness rule；
10. Cost 使用 symbolic vector，并和 benchmark 校准分离；
11. Direct/Plan/Kernel execution path 可以由 judgement 推导；
12. Lean proof implementation 可以完全独立于 C runtime 修改；
13. 本 PR 不改变现有程序行为。

---

## 22. 后续阶段

该规范批准后，按以下顺序继续：

```text
Phase A — Core Lean model
  Types / Effects / Ownership / Flow Syntax

Phase B — Execution semantics
  Source / Demand / WAIT / Terminal

Phase C — Architecture validity
  Surface / Graph / Plan / Kernel preservation

Phase D — Rewrite theorem catalogue
  R1–R15

Phase E — Cost derivation
  Direct / Plan / Kernel / Batch / Parallel Reduce

Phase F — C implementation conformance
  separate implementation PRs
```

其中 A–E 可以继续留在当前 formal PR 中；Phase F 必须拆成独立实现 PR。
