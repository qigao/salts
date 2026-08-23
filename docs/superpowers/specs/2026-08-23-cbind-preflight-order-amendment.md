# CBind D2 Preflight Ordering Amendment

日期：2026-08-23
状态：Normative clarification
适用文档：`docs/superpowers/specs/2026-08-23-cbind-scalar-struct-decode-design.md`

## 1. 目的

原 D2 设计的 §10 将 semantic/storage graph preflight 列在 depth/scratch preflight 之前，而 §11.4 又要求在递归进入 child struct 前检查 context depth limit。两处对“超出 depth budget 的 nested descriptor 是否继续深入验证”存在错误优先级歧义。

本 amendment 只消除该歧义，不改变 D2 支持类型、public ABI、scratch 模型、transaction 语义或 no-consumption guarantee。

## 2. 固定优先级

Preflight 仍全部发生在第一次 `cserde_reader_next()` 之前，并保持：

```text
required arguments / error record
context record
root/current descriptor semantic + native-storage proof
nested-entry depth admission
admitted child descriptor recursive proof
complete scratch requirement
root destination empty-state
reader consumption
```

对当前已经进入的 descriptor，malformed semantic/storage facts 仍返回 `CBIND_INVALID_SHAPE`。

对一个即将进入的 nested `CMETA_DATA_STRUCT`，若：

```text
next_struct_depth > context->max_depth
```

则立即返回：

```text
CBIND_LIMIT_EXCEEDED
```

并且不继续验证该 child struct 的内部 layout/fields/descendants。

因此，若同一个 nested child 同时：

```text
beyond max_depth
and
internally malformed
```

observable result 固定为：

```text
CBIND_LIMIT_EXCEEDED
reader provider calls = 0
```

## 3. 原因

`max_depth` 不只是 runtime decode policy，也是递归 preflight 的资源边界。先做 nested-entry depth admission 可以阻止调用方通过一个极深、虽然尚未消费 input 的 descriptor graph 迫使 CBind 无界递归 C call stack。

这与原 §11.4 的约束一致：depth limit 在 recursive entry 前检查。

“完整 graph preflight”因此应理解为：

```text
完整验证所有被 context depth contract 接纳、且 D2 需要进入的 descriptor graph。
```

而不是在已经超出调用方 depth budget 后继续遍历 child internals。

## 4. Scratch 顺序不变

Depth-admitted semantic/storage graph 校验完成后，再计算 complete scratch requirement。Scratch overflow/insufficiency 仍返回 `CBIND_LIMIT_EXCEEDED`，并保持 zero reader consumption。

## 5. Regression contract

必须保留测试证明：

```text
root malformed shape                       -> CBIND_INVALID_SHAPE
nested malformed shape within depth budget -> CBIND_INVALID_SHAPE
nested struct beyond depth budget           -> CBIND_LIMIT_EXCEEDED
nested malformed struct beyond depth budget -> CBIND_LIMIT_EXCEEDED
all above                                   -> reader provider calls = 0
```

本 amendment 在上述错误优先级上覆盖原 §10 中可能被解读为“无条件先递归完整 graph、后检查 depth”的文字；其余 D2 设计保持不变。
