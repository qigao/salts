# CFlow SCXML CMeta late binding 设计

## 背景与范围

本设计为 `datamodel="cmeta"` 增加 W3C SCXML `binding="late"`。现有
`binding="early"` 行为保持不变；`<data src>` 仍不在本阶段范围内。

CBind 负责外部 token/格式到 C 结构的绑定。当前 `<data expr>` 已经是
CMeta location/expression 到既有 C 对象的赋值，因此直接复用
`cflow_scxml_cmeta_assign_*`。只有以后实现 `<data src>` 的格式读取与对象绑定时，
CBind 才进入该适配边界。

## 状态与所有权

- CMeta session state 是业务数据唯一事实源。
- 每个含至少一个 `<data>` 的 `scxml`、`state` 或 `parallel` 拥有一个
  session-local first-entry 标志，状态为 `NEVER`、`PENDING`、`DONE`。
- 编译程序拥有不可变 assignment program；session 只拥有定长标志数组。
- 调用方的 initial object 仅在 session 初始化时借用并复制。

在包含状态第一次进入前，对其声明位置的读取返回调用方 initial object 中的值。
实现不会合成零值或静默默认值。第一次进入成功后，`<data expr>` 按文档顺序覆盖
对应位置。

## 执行与事务

每个非空 late datamodel 降低为一个内部 entry executable，排序在用户
`onentry` 与 invoke-entry 之前。原生 Statechart 已保证父状态 entry action 先于
子状态，并保证 entry action 先于 initial/history executable，因此无需另建调度器。

内部 executable 执行时：

1. `DONE` 直接跳过，保证重入和 history 恢复不重复初始化。
2. `NEVER` 改为 `PENDING`；该 microstep 的第一个 initializer 向原生 effect
   journal stage 一个不可失败的 commit/discard ticket，后续 initializer 共享它。
3. 在 staged CMeta state 上顺序执行该 datamodel 的全部 assignment。
4. microstep 发布后 ticket 将标志改为 `DONE`；任意失败、取消或回滚将其恢复为
   `NEVER`。

initializer 求值失败是终止当前 microstep 的 fatal action failure，不转成普通
`error.execution` block-abort。这样先前声明写入、并行分支写入、配置变化与
first-entry 标志统一回滚，避免出现“数据未发布但已标记初始化”的分裂状态。

## 容量、错误与兼容性

编译期为每个非空 late datamodel 计入一个 executable、block、step 与 state
action，并执行 checked arithmetic 和既有限额检查。session 初始化时分配精确大小
的标志数组，并要求 `effect_capacity` 非零。同一 microstep 的全部 late initializer
共享一个 ticket；send/invoke 等其他副作用仍按其既有协议消耗额外 journal row。

包含 late 声明的程序发布
`CFLOW_SCXML_REQUIREMENT_LATE_BINDING`，因此不能通过不具备 session 状态的原生
program-level executable binding 入口执行。未知 `binding` 是结构错误；late 用于
非 CMeta profile 是不支持特性；`<data src>` 报告缺少外部 loader。

公开兼容性只有一个追加 requirement bit。early 初始化顺序、赋值语义、错误码和
调用方 initial object 不变。

## 验证范围

focused tests 覆盖：late admission、未知 binding、外部 src、root/nested 顺序、
onentry 与 initial 顺序、初始化前读取、重入、parallel 文档顺序、history、journal
容量、求值失败与调用方对象不变。随后运行全部 SCXML tests，覆盖 early、managed
CMeta 生命周期、foreach、null profile 与 W3C fixture 回归。
