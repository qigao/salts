# CFlow Stream `take` / `skip` 设计

## 背景与目标

Issue #125 要求为 TurboSTL Stream 增加常用、语义明确的切片操作。`take` 与
`skip` 依赖值在当前操作位置上的到达次序，因此它们属于 CFlow Graph
语义，不能只在 TurboSTL terminal 外层截断结果，也不能把可变计数器放进
Graph 持有的 callable capture。

本阶段只实现 `take` / `skip`。`count`、匹配、查找和副作用 terminal 后续
分别设计，避免把 intermediate operation 与 terminal ownership 混成一个接口。

## 语义

- `skip(n)` 丢弃到达该节点的前 `n` 个值，之后按 encounter order 转发。
- `take(n)` 最多转发到达该节点的前 `n` 个值；第 `n` 个值进入下游后，运行时
  取消并停止继续拉取上游。
- `take(0)` 产生空流，并且不调用上游 Source 的 `resume`。
- 空输入始终产生空输出。
- 多个切片节点按 Graph 顺序组合；计数对象是到达各自节点的值，而不是原始
  Source 项，也不是最终 terminal 项。
- 解释执行支持 managed COPY/MOVE/DESTROY 值；direct compiled plan 继续只接收
  trivial storage，并对不支持的值类型 fail fast。

## 状态与所有权协议

| 项目 | 契约 |
|---|---|
| 数据单元 | 与节点输入类型相同的一个 CFlow value slot |
| 事实源 | Graph 节点保存不可变 `limit`；每个 Run 保存独立计数器 |
| 所有权 | Graph 不拥有元素；Run 的 value slot 按现有 copy/move/destroy trait 管理 |
| 生命周期 | Graph、Source backing storage 和类型描述符保持到 Run close；计数器随 Run 销毁 |
| 拓扑 | 每个 Run 单 owner 执行；同一 Graph 可创建多个互不共享计数的 Run |
| 顺序 | 保持 encounter order，不重排 |
| 容量 | 仅固定 `size_t` 计数，无可增长存储；比较后递增，禁止溢出 |
| 背压 | 沿用 Run downstream demand；丢弃值不消费 downstream demand |
| 失败 | Source/callback/value construction 错误按现有 Run 首错稳定传播 |
| 关闭 | `take` 短路时 cancel Source；Run close 最终 destroy Source 和 managed slots |

compiled plan 将不可变 `limit` 复制进指令；每次 eval 使用局部 materialized
vector，因此没有跨执行可变状态。当前只准入 slice-only trivial-value Graph；
切片与 callable node 混合时显式拒绝，因为 eager materialization 无法保持
`take` 的 lazy short-circuit/error 语义。包含切片的 plan 不进入 filter/map
fused value path，也不进入 ordered parallel reduce。

## API 与兼容性

- `cflow_stream_take(stream, limit)` / `stream.take(stream, limit)`
- `cflow_stream_skip(stream, count)` / `stream.skip(stream, count)`
- `cflow_graph_take(graph, limit)` / `cflow_graph_skip(graph, count)`
- advanced Graph builder 可创建带不可变 `size_t` 参数的切片节点。

现有 enum 值保持不变，新 opcode 只追加。函数调用源码兼容。`cflow_node`、
`cflow_stream` 是公开 concrete struct，新增尾部字段会改变二进制布局；消费端必须
与新库一起重编译。本变更不改变已有操作、错误文本或序列化格式。

## 验证范围

- `skip(0)`、`skip(size)`、`skip(>size)`、`take(0)`、`take(size)`、组合顺序。
- Surface、normalized、optimized、interpreted 与 compiled plan 结果一致。
- `take` 对 Source 的短路：`take(0)` 零次 resume，`take(n)` 恰好读取 n 个到达值。
- 同一 Stream 重复执行时计数器重置。
- managed values 在保留与丢弃路径均平衡 copy/move/destroy。
- C++ aggregate header 与安装消费端编译。
