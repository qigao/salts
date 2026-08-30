# CFlow SCXML `invoke/@idlocation` 稳定事务设计

## 背景与目标

Issue #177 要求 CMeta profile 支持 SCXML `invoke/@idlocation`：只有 invocation 在稳定配置中被实际评估时，才生成新的 `stateid.platformid`，将其写入 Machine 扩展状态，并以同一 ID 启动 adapter。瞬时进入又退出的状态不得写 ID 或启动服务。

当前 `on_stable` hook 只能读取 published const state；直接修改该指针会破坏 Machine 的状态所有权，另存 session 状态镜像又会产生第二事实源。因此需要一个版本化、事务性的稳定边界。

规范依据为 [W3C SCXML 1.0 invoke](https://www.w3.org/TR/scxml/#invoke) 和 [解释算法](https://www.w3.org/TR/scxml/#AlgorithmforSCXMLInterpretation)。

## 现状证据

- native runtime 已用双缓冲事务提交 configuration、history、completion 和 managed extended state；普通 action 还可事务性 stage internal Events 与 move-only effect tickets。
- SCXML invoke entry/exit action 已将声明置为 `PENDING` 或取消；现有 stable hook 仅启动仍为 `PENDING` 且 owner active 的声明，已具备 transient-state 过滤。
- invoke adapter v1/v2 的 `prepare_start` 返回 commit/discard ticket；prepare 只预留资源，真正副作用必须在 ticket commit 执行。
- `cflow_scxml_cmeta_location_compile` 与 owned-string helper 已能拒绝不存在、非 string、borrowed/custom、只读及 `_` 系统位置。
- 当前 cancel、autoforward、returned Event metadata 都从静态 descriptor 读取 ID；动态 ID 必须改为活动 row 的不可变快照。

## 架构决策

新增 `CFLOW_STATECHART_INSTANCE_HOOKS_ABI_V3`，在 hooks 尾部追加 `on_stable_transaction`。旧 v1/v2 hook 表继续按固定前缀尺寸读取，不能因 C struct 变大而要求旧调用方提供新尾部。

稳定事务 context 提供：

- `published_state`：只读、当前已发布状态；
- `staged_state`：runtime 从 published state copy-construct 的独立可变副本；
- 当前 configuration version 和 call-scoped active-state query；
- 复用 action transaction 的 bounded `raise_internal`；
- 复用 action effect journal 的 `stage_effect`。

回调返回三态：

- `NOOP`：没有 state/Event/effect 改动；runtime 销毁副本；
- `COMMIT`：runtime 原子发布整个副本与 staged internal Events，解锁后按序 commit tickets；
- `FATAL`：runtime 丢弃 Events/tickets、销毁副本并 latch 明确错误。

`NOOP` 若已经 stage Event/effect 属于 contract violation。v3 中 `on_stable` 与 `on_stable_transaction` 互斥。无 idlocation 的旧 SCXML program 继续安装 v2 hooks，避免无意义的 state copy/version 变化。

不采用隐藏内部 Event：它会制造额外微步、消耗 microstep limit、改变统计与观察结果，并污染用户 Event IR。禁止 const-cast 与 session 状态镜像。

## ABI 与事务协议

hooks 固定尺寸：

- v1 prefix：`offsetof(cflow_statechart_instance_hooks, on_event)`；兼容既有短表与旧 full-size 表；
- v2 prefix：`offsetof(cflow_statechart_instance_hooks, on_stable_transaction)`；
- v3：至少为新结构完整尺寸。

runtime 只能读取调用方 `struct_size` 覆盖的字段。稳定事务顺序为：

1. 在 SerialExecutor 上确认 macrostep 已排空 eventless/internal/completion work；
2. 将 published configuration/history/completion/state 复制到另一 buffer；
3. 清空本次 staged Event/effect counters；
4. 调用 v3 callback；
5. `NOOP` 时销毁副本；`FATAL` 时整体 rollback；
6. `COMMIT` 时在 instance lock 下重新检查 cancel/terminal winner，提交 internal FIFO、切换 `published` 并递增 publication version；
7. 解锁后按 document order commit effect tickets；
8. 若产生 internal work 则继续 drain，否则 settle macrostep。

copy、赋值、journal 容量、无效 ticket、cancel winner 或 callback contract 失败时，published state/version 不变，所有已接受 ticket 恰好 discard 一次。

## SCXML admission 与编译产物

`scxml_invocation_descriptor` 增加 `has_id_location` 和 compiled CMeta location，同时保留编译期 declaration key/done numeric Event。admission 顺序：

1. `id` 与 `idlocation` 同时出现先返回 `CFLOW_SCXML_INVALID_STRUCTURE`；
2. `idlocation` 仅允许 `datamodel='cmeta'`；
3. 空路径、路径语法/深度、missing、非 string、borrowed/custom、无 restore/assign ops、系统位置全部编译期拒绝；
4. checked 预算 `stateid + '.' + 20 decimal digits` 以及 `done.invoke.` 前缀，超过固定 metadata 上限返回 `LIMIT_EXCEEDED`，绝不截断。

## ID、row 与所有权

ID 格式固定为 `<owner-state-id>.<unsigned-decimal-token>`。session 的单调 `uint64_t` allocator 从 1 开始；`UINT64_MAX` 可使用一次，随后变为 exhausted sentinel 0，绝不回绕或复用。transient state 不领取 token；adapter recoverable rejection 仍消耗 token，因为 invoke 已被实际评估。

每个预分配 invocation row 增加 `id_size` 与固定容量 active-ID buffer。该 buffer 不是 Machine 状态镜像，而是活动外部 invocation 的不可变路由身份：CMeta location 后续可被文档覆盖，但 cancel、forward、returned metadata 和 done 仍使用启动时快照。

row 状态协议：

```
INACTIVE --committed entry--> PENDING
PENDING --stable reservation accepted--> START_RESERVED
PENDING --recoverable rejection--> FAIL_RESERVED
PENDING --owner exits before stable--> INACTIVE
START_RESERVED --runtime transaction commit--> ACTIVE
START_RESERVED --rollback/cancel winner--> PENDING + ticket discard
FAIL_RESERVED --runtime transaction commit--> FAILED + error Event
FAIL_RESERVED --rollback--> PENDING
ACTIVE --committed owner exit--> INACTIVE + cancel(exact row ID)
ACTIVE --matching done return--> INACTIVE without cancel
FAILED --exit/re-entry--> INACTIVE/PENDING
```

row 转换在 `registry_lock` 下完成；adapter prepare/commit/discard 不持锁。成功 start effect 的 commit 必须发生在 Machine state 发布之后。shutdown 继续 close adapter exactly once，并在 quiescent 后释放 row storage。

## stable callback 行为

一次回调按 invocation document order处理仍 `PENDING` 且 owner active 的 descriptor：

1. 生成候选 ID；
2. 在 staged CMeta state 写 owned string；
3. 从同一 staged state 求值 type/src/payload；
4. 调 adapter v1/v2 `prepare_start`；
5. accepted ticket 包装成 SCXML row-transition ticket并 stage 到 runtime journal；
6. recoverable rejection stage `error.execution` 并在 commit 后将 row 置 `FAILED`；
7. fatal contract/journal/copy failure整体 rollback。

并行 pending invocation 的 state writes、internal error Events 与 start tickets作为一个 stable transaction 提交；任何 fatal 都不能留下部分 ID、ACTIVE row 或已启动 adapter。

## 动态 `done.invoke`

native Event ID 仍是编译期有限集合。descriptor 保留内部 numeric `done_event`，不在运行期扩张 Event table。新增 additive API：

```c
cflow_mailbox_status cflow_scxml_session_report_invoke_done(
    cflow_scxml_session *session, uint64_t token);
```

API 在 `registry_lock` 下将 live ACTIVE token 映射到 descriptor numeric done Event，再走 tagged external admission；zero/stale/cancelled token 返回 `INVALID_ARGUMENT`。`observe_scxml_event` 在 token 与 numeric done Event 同时匹配时，有界合成 `done.invoke.<active-row-id>`，并令 `_event.invokeid` 等于该 row ID。preprocess 仍以 token + numeric ID 完成 finalize/row completion，不发送 cancel。

当前 profile 只支持 exact named Event，不宣称通用 W3C prefix/wildcard descriptor。动态 done 的 API、`_event.name` 和 `_event.invokeid` 是本 issue 的准确范围；通用 descriptor matching 另行实现。

## 失败语义

- 参数/type/src/payload/ID assignment 失败：不调用 adapter；transactionally raise `error.execution`；不发布半写。
- recoverable adapter rejection：不 commit start ticket；发布已生成 ID，row 置 `FAILED`，raise 一个对应 error Event；runtime 不 fatal。
- accepted 但 ticket 无效、effect journal full、state copy/move failure或 hook contract violation：stable transaction fatal；全部 ticket discard，所有 ID write/Event rollback。
- committed owner exit：cancel 使用 row 的 exact token/ID；exit microstep rollback 时 row 和 adapter 保持 ACTIVE。
- session close/cancel 在 stable commit 前胜出：不发布 ID、不 commit adapter start。

## 验证范围

- runtime ABI：旧 v1 short/full-old-size、旧 v2 old-size、v3 valid；未知 version、v3 short、两个 stable hook 同时设置均拒绝。
- runtime transaction：NOOP、COMMIT、FATAL、copy failure、invalid ticket、journal full、cancel winner；managed state无泄漏/double destroy。
- admission：顶层/嵌套 owned string成功；null datamodel、empty/missing/non-string/borrowed/read-only/system/conflicting/过深/过长失败。
- runtime：stable、transient、re-entry distinct IDs、parallel order、v1/v2 accept、recoverable reject、fatal rollback、committed/rolled-back cancel、shutdown。
- identity：state snapshot、start/cancel/forward、`_event.invokeid` 与 dynamic done name逐字节一致。
- 回归：原 invoke lifecycle/finalize/autoforward/payload、send idlocation、focused CTest 与完整 preset。

## 兼容性、迁移与回滚

公开变化仅为 additive runtime hooks ABI v3 和 `report_invoke_done` 函数；invoke adapter v1/v2 与旧 SCXML session APIs不变。无 idlocation 的 program继续走 v2。

若需回滚 SCXML 功能，恢复 idlocation admission rejection即可；runtime v3 保留为向后兼容的未使用能力。不得 fallback 到修改 const state或生成隐藏用户 Event。
