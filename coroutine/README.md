# Coroutine

`TurboUtils::Coroutine` 是 `vendor/minicoro/minicoro.h` 的唯一编译封装与有界复用池。它是 NativeIO 之下的低层模块，不依赖 CFlow、NativeIO、CNet 或线程池。

模块只提供显式 coroutine 生命周期、cooperative yield/resume、bounded pool 和可选通用 scheduler。NativeIO 不采用通用 scheduler 或 TLS current context；它由 backend 的单 owner 在 terminal completion 到达后恢复 frame。正常终态复用 frame，协议错误则通过 pool abandon 销毁 suspended frame 并归还有界 slot，避免遗留无法 drain 的活动 ownership。

依赖方向固定为：

```text
CFlow / CNet
     |
     v
  NativeIO -> Coroutine -> vendor/minicoro
```

`cflow/minicoro` 适配目标已删除。CFlow 的 Graph/Resumable 不拥有 coroutine frame；需要异步 I/O 的 Actor、Reactive 或 CNet 层通过 NativeIO 的 operation/completion 或 coroutine owner API 工作。
