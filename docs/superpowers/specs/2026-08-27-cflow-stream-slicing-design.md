# CFlow Stream `take` / `skip` 设计

## 背景与目标

Issue #125 要求为 CFlow/TurboSTL Stream 增加常用、语义可界定的
`take` 与 `skip`。二者依赖值到达操作节点时的 encounter order，因此属于
Graph/Run 语义，不能只在 collection terminal 截断结果，也不能把可变计数器
放入可复用 Graph 或 callable capture。

本阶段只实现 `take` / `skip`。Statechart XML/SCXML、Statechart Source、
Stream terminal、`distinct` 与 `sorted` 均不在本阶段范围内。

## 公开语义

- `skip(n)` 丢弃到达该节点的前 `n` 个值，之后保持 encounter order 转发。
- `take(n)` 最多转发到达该节点的前 `n` 个值；达到上限后正常结束上游，
  不把内部短路报告为用户取消。
- `take(0)` 不调用上游 Source 的 `resume`；Run 收到 demand 后取消上游并正常完成。
- `skip(0)` 是保持类型和顺序的恒等切片节点。
- 空输入始终产生空输出。
- 多个切片节点按 Graph 数据边顺序组合。计数对象是到达各自节点的值，
  不是原始 Source 项，也不是最终 terminal 项。
- 同一 Stream/Graph 的每次执行拥有独立计数器，重复执行不会继承位置。
- `take` 位于 `flatMap`/Relation 之后时会停止产生该节点之后不再需要的上游值；
  位于其之前时，已被 `take` 接受的值仍可完成其下游展开。

## API 与 IR

追加公开 opcode `CFLOW_OP_TAKE`、`CFLOW_OP_SKIP`，不改变既有枚举值。
`cflow_node` 尾部增加只读式 slice 参数元数据：

```c
typedef struct cflow_slice_parameter {
    bool present;
    size_t count;
} cflow_slice_parameter;
```

`op` 决定 `count` 表示 take limit 或 skip count；`present` 使合法的零值与损坏的
公开 concrete node 可区分。Graph 只拥有不可变参数，执行位置只属于 Run。

公开构造入口：

```c
bool cflow_graph_create_slice_node(cflow_graph *graph,
                                   cflow_subgraph_id subgraph,
                                   cflow_op op,
                                   const cmeta_type_desc *input_type,
                                   size_t count,
                                   cflow_node_id *out_node);
bool cflow_graph_take(cflow_graph *graph, size_t limit);
bool cflow_graph_skip(cflow_graph *graph, size_t count);

cflow_stream *cflow_stream_take(cflow_stream *stream, size_t limit);
cflow_stream *cflow_stream_skip(cflow_stream *stream, size_t count);
```

`cflow_stream` 尾部追加 `take`/`skip` explicit-self 方法指针。Graph/Stream 是公开
concrete struct，本次改动保持源码兼容，但消费端必须与新库一起重新编译。

## 状态、所有权与关闭协议

| 项目 | 契约 |
|---|---|
| 数据单元 | 与切片节点输入/输出类型相同的一个 CFlow value slot |
| 事实源 | Graph node 保存不可变 `count`；每个 Run 保存独立 `size_t` 位置 |
| 所有权 | Graph 不拥有元素；Run 按现有 COPY/MOVE/DESTROY traits 管理 value slot |
| 借用期 | Graph、Source backing storage、类型描述符保持有效直至 Run close |
| 线程拓扑 | 每个 Run 的 pump 是 slice position 的唯一写者；Graph 可被多个 Run 借用 |
| 顺序 | 保持 encounter order，不重排 |
| 容量 | 每节点一个固定 `size_t`，无可增长容器；达到上限后不再递增 |
| 背压 | 沿用 downstream demand；被 skip 丢弃的值不消费 demand |
| 错误 | Source、callable、value lifecycle、Sink 与 Scheduler 首错按现有 Run 契约传播 |
| 短路 | `take` 取消其上游 Source/continuation，保留下游已产生 continuation |
| 关闭 | Run close 继续负责 cancel Source、销毁 continuation、value slot 与计数数组 |

`take` 的内部短路是正常完成：`cflow_run_is_done()` 为真，
`cflow_run_is_cancelled()` 为假，Sink 收到一次 `on_done`。外部
`cflow_run_cancel()` 的现有取消语义不变。

## Graph 分析和执行支持

- 切片节点是 deterministic、total、no-alias 的有状态位置操作，不声明 idempotent。
- normalization、optimization、clone、validation 与 structural equality 必须保留参数。
- interpreted runtime 支持 trivial 与具备完整 lifecycle traits 的 managed values。
- direct compiled plan 本阶段显式不支持包含 TAKE/SKIP 的 Graph；
  `cflow_plan_graph_supported()` 返回 false，compile fail fast，不回退到 interpreter。
- `cflow_verify_pipeline()` 仍比较 Surface、normalized、optimized 的 interpreted 结果；
  仅在 plan 声明 supported 时才检查 compiled parity。

## 验证范围

- `skip(0)`、`skip(size)`、`skip(>size)`、`take(0)`、`take(size)` 与空输入。
- `filter -> skip -> take`、`take -> skip` 和多个切片节点的顺序。
- 同一 Stream 重复执行时计数器重置。
- 无限/未终止 Source 在 `take(n)` 后恰好停止上游，`take(0)` 零次 resume。
- 短路为 DONE 而非 CANCELLED，Source 错误在达到 limit 前仍稳定传播。
- Surface、normalized 与 optimized interpreted parity；compiled plan 明确拒绝。
- managed values 在丢弃、转发与短路路径中的 copy/move/destroy 平衡。
- C11/C++ public headers、TurboSTL facade 与 installed-package consumer 编译。
