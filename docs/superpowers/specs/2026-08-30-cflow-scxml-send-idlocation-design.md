# CFlow SCXML `send/@idlocation` 设计

## 背景与范围

SCXML 要求没有显式 `id` 的 `<send>` 在执行时生成会话内唯一 ID；存在
`idlocation` 时，还要把该 ID 写入数据模型。当前实现已经校验 `id` 与
`idlocation` 互斥，但拒绝 `idlocation`，并要求延迟发送必须提供字面量
`id`。

本次只实现 `send/@idlocation`。`invoke/@idlocation` 仍保持拒绝：invoke
只在稳定态 hook 中求值，而该 hook 只接收只读已发布状态；在进入动作中
提前写入会让立即退出的状态错误地产生 invoke ID。解决它需要另行设计
可事务写入的稳定态 hook 或调整 invoke 执行阶段，不能作为本改动的隐式
公开运行时语义变更。

## 状态与所有权

- 生成序号由 `cflow_scxml_session_impl` 独占，SerialExecutor 上的每次
  `send` 求值推进一次。序号耗尽时 fail fast 为 `error.execution`。
- 生成文本使用固定上限的调用期缓冲区，格式为
  `send.<session-uuid>.<decimal>`；会话 UUID 与计数器共同避免与其他
  session 或静态文档 ID 意外碰撞。
- `idlocation` 编译为现有 `cflow_scxml_cmeta_location`。目标必须是可写
  `CMETA_DATA_STRING`，且 buffer adapter 必须声明 `OWNED`，避免把调用期
  ID 缓冲区作为悬空 borrowed view 保存。
- 写回目标是 executable 的 `out_state`。block 失败时现有回滚会恢复
  `state`，因此 ID 与该 block 的其他状态变更共享事务边界。
- Event I/O prepare 回调只在调用期间借用 request；延迟发送注册表改为
  有界复制 ID，使 registry 生命周期不依赖字面量或栈缓冲区。

## 执行顺序与错误语义

1. 先求值 event、target、type、delay 与 payload 等现有参数。
2. 若 descriptor 有 `idlocation`，生成 ID 并写入 staged CMeta state。
3. internal send 使用已生成 ID 完成写回，但不要求 Event I/O adapter。
4. external send 把同一 ID 传给 adapter；延迟发送先复制到 registry，再
   prepare adapter ticket。
5. ID 生成、字符串写入、容量或 adapter 准备失败时，不发送消息；可恢复
   错误映射为 `error.execution`，fatal contract 失败沿用现有语义。

## 兼容性与资源边界

- 不修改公开结构体、函数或 adapter ABI。
- 既有显式 `id` 行为不变。
- 每个延迟发送 registry row 增加固定 257-byte 生成 ID 存储；既有字面量
  ID 仍借用 program-owned storage，避免收紧原有长度边界。row 数量仍由
  `delayed_send_capacity` 配置控制。
- 本次不让普通无 `idlocation` 的即时 send 对 adapter 可见地获得生成 ID，
  避免在同一补丁中扩大既有公开行为；完整无 id 语义可在后续兼容性变更中
  单独评估。

## 验证范围

- 编译期接受 owned string location，拒绝未知、非字符串、borrowed、系统
  只读 location，以及 `id`/`idlocation` 并存。
- 即时 external send 的 adapter ID 非空，且 staged state 能读取同一 ID。
- 延迟 send 后用 `sendidexpr` 取消，证明 registry 与 CMeta 保存的是同一 ID。
- 同一 session 多次执行产生不同 ID。
- internal send 可写回 ID，且不引入 Event I/O adapter 依赖。
- 运行全部 SCXML 测试及仓库完整 CTest 回归。
