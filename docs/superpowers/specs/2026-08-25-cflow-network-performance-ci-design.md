# CFlow TCP/UDP Performance CI Design

## 目标与测量边界

新增真实 loopback TCP/UDP benchmark，走 `cflow_io_actor`、本机 native backend 和
Executor；结果用于 PR/主分支证据与 artifact，不作为共享 runner 的硬性能门禁。

每次 logical exchange 包含一次 native send 与一次 native recv，server peer 回显。
应用字节口径每 exchange 只计 payload 一次；另报 `wire_bytes = 2 * application_bytes`。

## 指标与事实源

- TinyTest `benchmark_io`：ops/s 与 MiB/s 展示；其 operation 数等于真实 exchange 数。
- 独立固定 latency 数组：nearest-rank P50/P95/P99，`index = ceil(p*n)-1`。
- wall time：`turbo_hrtime()`；进程 CPU：Windows `GetProcessTimes`，POSIX `getrusage`；
  `process_cpu_pct = process_cpu_ns / wall_ns * 100`。它是进程跨线程消耗的 core-equivalent，
  多个 worker/server 线程并行时允许超过 100%。同时报告
  `application_mib_per_second = application_bytes / MiB / wall_seconds`、
  `cpu_core_equivalents = process_cpu_pct / 100`，以及
  `application_mib_per_cpu_second = application_bytes / MiB / process_cpu_seconds`，用于同一
  runner 内比较单位 CPU 时间完成的应用数据量。
- peak RSS：Windows `PeakWorkingSetSize` bytes；Linux/macOS `ru_maxrss` 按平台单位归一化。
- Actor/native stats 是 accepted、错误、full/closed/lease/executor rejection 和 stale 的
  唯一事实源。错误率 = errors / attempted；拒绝率 = rejected / attempted。

机器消费格式是单行 `CFLOW_BENCHMARK_JSON { ... }`，schema 为
`cflow-network-benchmark/v1`。TinyTest 文本只供人读，不作为 CI parser 协议。

## 负载、容量与资源

- latency-64：64 B，200 samples × 128 exchanges；
- throughput-16384：16 KiB，50 samples × 128 exchanges；
- TCP 与 UDP 各运行，两种 profile 各重复 `BENCHMARK_RUNS` 次。

`CFLOW_NETWORK_PROTOCOL` 仅接受 `tcp|udp`，`CFLOW_NETWORK_PROFILE` 仅接受
`latency|throughput`，`CFLOW_NETWORK_BACKEND` 仅接受
`epoll|kqueue|iocp|io_uring`；未设置时选本机默认值，设置为空、未知值或选择本机未编译
backend 时 fail fast。samples、exchanges、payload 的环境值同样要求正整数且不超过硬上限。

`CFLOW_NETWORK_WAIT_MODE` 仅接受 `blocking|busy`，默认 `blocking`。`busy` 保留主动调用
Actor/Executor 并 yield 的基线；`blocking` 使用 Actor 已有 advisory wake 驱动条件变量 edge
latch，在每次休眠前持锁复查 latch，避免 native completion 与 wait 之间丢唤醒。两种模式
都由 benchmark 主线程串行驱动 Actor 和 Manual Executor，不改变生产 API、请求所有权或
完成 acknowledge 协议。

所有 samples、ops、payload 和 latency storage 使用 checked multiplication 与硬上限。
server/client 使用 `127.0.0.1:0`，无外网、固定端口或持久化状态。TCP 必须循环处理
短读短写；UDP 一 datagram 对应一次 exchange，长度不符即失败。

## CI 报告与兼容性

扩展现有 release benchmark workflow：构建/正确性测试新 target，对两种 wait mode 逐场景保存 raw 输出，
验证唯一 JSON，汇总 JSONL 和 Markdown 到现有 `benchmark-results/network/`，并上传 30 天。
job summary 显示同 host 重复运行的中位数/范围，同时明确 shared-runner evidence 非 gate。

不新增 preset、依赖或跨 host 基线。性能退化阈值只作为后续固定 runner 方案的候选；
当前 CI 只在 benchmark 退出失败、schema 无效、功能错误/拒绝非零时失败。

回滚只移除 benchmark/test/workflow 条目，不影响 runtime API 或生产数据。
