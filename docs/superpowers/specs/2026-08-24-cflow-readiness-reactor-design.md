# CFlow 有界 Readiness Reactor 设计

## 背景与证据

Issue #66 要求把操作系统 readiness 接入现有 `Source WAIT -> waker`
协议，同时保持 CFlow 语义 API 不暴露 epoll/kqueue/IOCP 句柄。

仓库现状提供了语义骨架，但还没有可称为生产后端的生命周期协议：

- `cflow/include/cflow/sources.h` 的通用 readiness Source 仅接收
  `read/arm/cancel/close` 回调；`arm` 只返回 `bool`，没有容量、代际或
  backend 错误。
- `cflow/src/sources.c` 把 driver waker 直接交给 Run；destroy 顺序是
  `unarm -> user close -> free`，但 `unarm` 的 `void` 契约不能证明回调已经
  quiescent。
- `cflow/src/runtime.c` 的 wake user data 是借用的 `cflow_run *`。Run close
  可以在取消 wait 后释放实现，因此 driver 必须保证 cancel 返回后旧回调不再
  调用 waker。
- `platform/` 当前只有 clock/thread 原语，没有 readiness 后端。
- Windows Release 基线在 2026-08-24 从 `origin/master@198ed52` 配置、构建并
  运行 125/125 CTest 通过。

因此本任务不是给现有 callback 再包一层名称，而是补齐“有界注册、一次性
arm、代际验证、quiescent close”的事实源。

## 决策

### 分层

采用两层实现，保持单向依赖：

```text
Platform salts_readiness_reactor
  owns: native fd wait, bounded slots, generation, backend errors, quiescence
                     |
                     v
CFlow reactor Source adapter
  owns: read/WOULD_BLOCK, WAIT arm, wake coalescing, Source cancel/destroy
```

Platform 在 `<salts/readiness.h>` 暴露 opaque reactor/registration、通用事件位和
精确 `int` 状态码。只有 Platform 的注册入口接收 native resource；
`<cflow/readiness.h>` 只接收一个已经注册的 opaque registration，并以 move
方式取得所有权。CFlow API 因而不出现 epoll fd、kqueue ident、IOCP port 或
OVERLAPPED。

`Salts::CFlow` 对 `Salts::Platform` 的链接从 PRIVATE 调整为 PUBLIC，
因为新公开头文件引用 Platform 的 opaque registration 类型。依赖方向仍是
`CFlow -> Platform`，不存在反向依赖。

### 首个生产后端

首个生产后端是 Linux epoll，并由显式 CMake option
`SALTS_ENABLE_EPOLL_READINESS` 控制。Linux Release user preset 和 Linux CI
显式打开它；Windows、macOS、Android 保持关闭。关闭或不支持时，native
reactor factory 返回 `SALTS_ENOTSUP`，不自动选择 poll/select、定时扫描或
thread-per-handle。

选择 epoll 的原因：

- `EPOLLONESHOT` 原生表达一次 arm 对应至多一次 wake，后续必须显式 rearm；
- level-triggered readiness 与 `read -> WOULD_BLOCK -> arm` 的 lost-wakeup
  规避顺序一致；
- `epoll_event.data.u64` 可以携带 slot generation，旧事件不会误投到复用 fd；
- `eventfd` 只用于 reactor shutdown/control 唤醒，数据 readiness 不经过轮询。

IOCP 是操作完成模型：handle 关联 completion port 后，通知对应已发起的
overlapped operation，而不是通用的“现在可重试 read”。它应在后续以
completion Source/operation adapter 进入，不能伪装成本期 readiness backend。

## 公共接口

Platform 新增下列概念；具体拼写在 RED header test 固定后实现：

```c
typedef struct salts_readiness_reactor { void *impl; }
    salts_readiness_reactor;
typedef struct salts_readiness_registration { void *impl; }
    salts_readiness_registration;

typedef uint32_t salts_readiness_events;

typedef struct salts_readiness_config {
    size_t registration_capacity;
    size_t event_batch_capacity;
} salts_readiness_config;

typedef struct salts_readiness_stats {
    size_t capacity;
    size_t registered_count;
    size_t armed_count;
    size_t callbacks_inflight;
    uint64_t rejected_full;
    uint64_t stale_events;
    uint64_t duplicate_events;
    uint64_t backend_errors;
} salts_readiness_stats;
```

控制面 API 使用 `0` 成功、稳定负错误码失败：

- reactor init/shutdown/destroy；
- register native resource；
- arm one-shot callback；
- additive callback-return continuation arm；
- unarm and wait for callback quiescence；
- close registration and release its slot；
- snapshot stats。

continuation 的最小用法如下；真实 callback 必须把 `blocked` 替换为一次有界、非阻塞的
I/O 尝试结果：

```c
#include <salts/readiness.h>

#include <stdbool.h>

static salts_readiness_callback_result continue_read(
    void *user, salts_readiness_events events, int status) {
  const bool blocked = status == SALTS_OK && events != 0u &&
                       *(const bool *)user;
  return (salts_readiness_callback_result){
      blocked ? SALTS_READINESS_REARM : SALTS_READINESS_COMPLETE,
      blocked ? SALTS_READINESS_EVENT_READ : 0u};
}

int arm_read(salts_readiness_registration *registration, bool *blocked) {
  return salts_readiness_arm_continuation(
      registration, SALTS_READINESS_EVENT_READ, continue_read, blocked);
}
```

错误映射使用已有 `SALTS_EINVAL`、`SALTS_ENOMEM`、`SALTS_ENOBUFS`、
`SALTS_EALREADY`、`SALTS_EBUSY`、`SALTS_ESHUTDOWN`、`SALTS_ENOTSUP`
以及负 errno。不会新增含义重叠的 CFlow 私有错误域。

CFlow 新增 `cflow_source_from_reactor_registration(...)`，返回精确 `int`。
成功时 move registration 并清空调用方 handle；失败时不转移所有权。配置仍复用
现有 `cflow_read_fn` 和 optional user close callback，因此值语义与现有 readiness
Source 相同，并继续只接收 trivial-copy/trivial-destroy 类型。

旧 `cflow_source_from_readiness(...)` 保留，避免破坏现有用户。文档明确它是
expert driver bridge：driver 自己负责 one-shot、stale suppression 和 cancel
quiescence；新代码优先使用 reactor registration adapter。

## 数据与生命周期协议

### 数据单元与事实源

- 注册事实源：reactor 预分配 slot table。
- readiness 事实源：epoll interest/ready list；CFlow 不镜像 OS readiness。
- 唤醒身份：`64-bit token = generation + slot index`，generation 为每次 slot
  复用递增的非零值。
- 值事实源：用户 `read` callback；readiness 事件本身不是一个 CFlow value。

### 所有权

- reactor 拥有 slot table、epoll fd、eventfd、event batch 和 reactor thread；
- registration 是 move-only handle，关闭前借用用户 native resource；
- registration 内含 per-handle admission word（closed bit + entrant count）；控制调用
  先取得 entrant，close 关闭 admission 后等待已进入调用，register 只在
  `CLOSED|0` 构造窗口内发布新 slot，避免 close/reuse ABA；
- CFlow Source 成功构造后拥有 registration；
- 用户 resource 和 `user` 由 Source 借用到 registration quiescent close；若提供
  close callback，Source 在 quiescence 后调用一次；
- Graph、scheduler 和 sink 所有权不变。

### 线程拓扑与顺序

- register/arm/unarm/close 可由多个控制线程调用；slot mutex/condition 保护状态；
- 一个 reactor 只有一个 epoll consumer thread，registration callback 串行执行；
- callback 在任何 Platform/CFlow mutex 外调用；
- 同一 arm generation 最多交付一次 callback；不同 registration 间不承诺
  readiness 顺序。

### 容量与分配

init 时检查：

```text
registration_capacity > 0
event_batch_capacity > 0
event_batch_capacity <= registration_capacity + 1
capacity * sizeof(slot) 不溢出
batch * sizeof(epoll_event) 不溢出
capacity <= UINT32_MAX - 1
```

slot table 和 event batch 只在 init 分配。register/arm/wake/unarm 不 realloc。
slot 满时 register 返回 `SALTS_ENOBUFS` 并增加 `rejected_full`；不阻塞、不扩容。

### 状态机

Registration slot 不再使用 `FREE/REGISTERED/ARMED/FIRING/CLOSING` 混合枚举。
同一个 slot 在 reactor mutex 下拥有五组正交事实：

```text
lifecycle: FREE | OPEN | CLOSING | RETIRED
interest:  IDLE | ARMING | ARMED | UNARMING
delivery:  IDLE | CALLBACK
terminal:  NONE | RESERVED | DELIVERING
control:   NONE | REGISTER | ARM | UNARM | CLOSE
```

`native_registered`、registration/arm generation、arm token、API borrow、arm
waiter 和 orphan cleanup 是附属资源/版本事实，不得反向推导 lifecycle 或
delivery。关键线性化点为：

- arm backend 成功后在锁内 `ARMING -> ARMED`；
- normal dispatch 在锁内同时执行 `ARMED -> IDLE` 与
  `delivery IDLE -> CALLBACK`，因此同一 arm 最多交付一次；
- callback epilogue 执行 `delivery CALLBACK -> IDLE` 并广播；
- terminal snapshot 先关闭公共控制 gate、排空 control，再把
  `OPEN + ARMED + NONE` 原子改为 `RESERVED`；
- close 先关闭 handle admission，native close 成功后等待 callback、API borrow
  和 arm waiter quiescent，最后清空 handle 并回收或 retire slot；
- generation 无可表示的后继时 retire 并返回 `SALTS_EOVERFLOW`，不回绕。

Reactor：

```text
OPEN --shutdown--> STOPPING --thread joined--> CLOSED --destroy--> EMPTY
```

在 reactor callback 内发起同 registration arm/unarm/close 或同 reactor shutdown
时不得阻塞等待自身，统一返回 `SALTS_EBUSY` 且不提交状态迁移。外部 arm 若在
`delivery CALLBACK` 期间到达则等待 epilogue，再完整重检 lifecycle、terminal、
reactor admission、generation、interest 和 control。CFlow adapter
持有 callback reference，Source destroy 只释放 owner reference，因此 shutdown
during callback 不会释放 callback 正在使用的 state/user resource。

### Lost wake、重复与资源复用

1. Source 先调用 user `read`。
2. 仅在 `CFLOW_READ_WOULD_BLOCK` 后 arm。
3. epoll 使用 level-triggered `EPOLLONESHOT`；若 readiness 已在 read 与 arm 之间
   成立，arm 后立即可见。
4. dispatch 在锁内验证 registration/arm generation、`OPEN + ARMED`、terminal
   和 control，原子消费 interest 并取得 `delivery CALLBACK` borrow 后才在锁外
   调用 adapter。
5. stale token 只增加 `stale_events`；同 generation 非 ARMED 事件只增加
   `duplicate_events`，均不得调用旧 waker。
6. close 先关闭 per-handle admission 并执行 `EPOLL_CTL_DEL`，再等待 callback、
   API borrow 与 waiter 为零，最后释放 slot。
   fd 被关闭并复用时，旧 epoll payload 仍因 generation 不匹配而被丢弃。

### Backend failure 与关闭

- `epoll_wait` 的 `EINTR` 重试；其他错误把 reactor 转为 STOPPING，并向每个
  ARMED registration 交付一次带负 errno 的 terminal backend event。
- `EPOLLERR/EPOLLHUP` 作为 readiness event 交给 user read 解释；read 可以先排空
  数据，再返回 DONE/ERROR。
- reactor shutdown 关闭 admission、唤醒 epoll thread、使所有 ARMED registration
  收到 `SALTS_ESHUTDOWN`、join thread，然后进入 CLOSED。
- 正常释放顺序是 `cflow_run_close -> source/registration quiescent close -> user
  resource close -> reactor shutdown/destroy`。

## CFlow adapter 行为

adapter 每次只保存一个 waker 和一个 arm generation。Platform callback 先把
adapter 从 ARMED 改为 READY/ERROR，再提取 waker，在锁外调用一次。

若 Platform arm 同步失败，adapter 保存精确错误并同步 wake，但向 Run 的
`cflow_waitable.arm` 返回成功；下一次 resume 产生稳定错误字符串。这样不改动
现有 `bool arm` ABI，同时避免 Run 只看到笼统的 `waitable arm failed`。

cancel/unarm 返回后保证旧 waker quiescent。destroy 可与 callback 竞态：owner
reference 和 callback reference 中最后一个负责 registration close、user close
和 state free，所有动作恰好一次。

## 验证范围

Lean 4 模型位于 `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Readiness.lean`，
证明位于 `CMetaCFlowCalculus/Proofs/Readiness.lean`。它证明 per-slot transition
及任意有限 trace 的不变量保持、one-shot/terminal 互斥、callback rearm/shutdown
判定、generation 不回绕，并且单独证明 close/register admission 中旧 entrant
会阻止 handle 复用。Slot、admission、generation 和 reactor gate 还没有组合成
一个联合 transition system，因此当前结论是协议的局部 safety lemmas，不是
handle pointer/generation ABA 协议或 fatal/shutdown snapshot 原子性的完整证明。
C/Lean 对应关系仍由共享 contract、native differential 与 TSan 验证，不得把形式化
模型本身当作 C 内存模型或 backend 实现证明。

共享 fake-driver contract suite 在所有 host 验证：

- zero/overflow capacity、full admission 和 exact status；
- register/arm/wake/rearm/unarm/close；
- duplicate/stale event、slot generation 和 simulated resource reuse；
- callback 中 close、外部 close 与 inflight callback 竞态；
- reactor shutdown、callback 内 shutdown 的 `SALTS_EBUSY`、backend error fan-out、
  统计计数和重复 shutdown。

Linux native suite 使用 nonblocking pipe/socketpair 验证：

- read-ready、hangup、WOULD_BLOCK rearm；
- close/unregister race 和 fd-number reuse；
- 多 registration 压力、cancel/rearm 循环和 shutdown during callback；
- native Source 与等价同步 array Source 的 value/done/error observation。

Windows/macOS/Android 验证公共头、fake suite、CFlow adapter 和明确 ENOTSUP；
不声称 native backend 完成。

## 兼容性、迁移与回滚

- 现有 Source、Graph、Run、scheduler ABI 和旧 readiness factory 保持不变；
- 新增 CFlow 对 Platform 的公开依赖，但两个目标已经属于同一安装包；
- Linux 启用 option 后增加一个 reactor thread 和 init-time bounded allocation；
  未创建 reactor 时没有运行时开销；
- 用户可逐个把 raw arm/cancel driver 迁移为 Platform registration，无需改变
  read callback 或值类型；
- 若 epoll backend 出现 host 回归，可关闭
  `SALTS_ENABLE_EPOLL_READINESS`，保留 fake contract 和 CFlow adapter，不回退到
  polling；若公共契约本身有缺陷，可完整回滚新增 header/source/tests，旧 API
  仍可工作。

`salts_readiness_arm_continuation()` 不改变旧 arm 的 callback 内 `SALTS_EBUSY` 判定。
continuation 只返回 `COMPLETE` 或 `REARM + interests`；Platform 在 callback 返回后与
close/unarm/external arm/shutdown 共用同一 control gate 提交后续 backend arm。terminal
callback 忽略返回值，backend rearm 失败则以 `events == 0` 和精确错误再投递一次 terminal
callback。当前 Lean `Readiness` 模型仍覆盖旧 one-shot/self-rearm 判定与共享 slot safety，
尚未刻画该新增 callback-return rearm commit；现有 C state-model 也只校验公共状态投影与
legacy callback presence，另以 callback-form XOR helper 校验两种具体函数指针，不表达
continuation transition。新增 commit、invalid result、backend rearm failure，以及与
close/unarm/shutdown 和阻塞 backend arm hook 的并发边界由 fake backend contract 覆盖，
epoll/kqueue native tests 继续验证真实 OS 集成。

## 一手资料

- Linux `epoll_ctl(2)` 与 `EPOLLONESHOT`：
  https://man7.org/linux/man-pages/man2/epoll_ctl.2.html
- Linux `epoll_wait(2)`：
  https://man7.org/linux/man-pages/man2/epoll_wait.2.html
- Windows IOCP completion 语义：
  https://learn.microsoft.com/en-us/windows/win32/fileio/createiocompletionport
- Windows `WSAEventSelect` readiness 及 nonblocking 副作用：
  https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsaeventselect
