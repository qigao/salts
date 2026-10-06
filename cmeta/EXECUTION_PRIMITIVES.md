# 执行与资源原语（#927）

本协议是实现和测试的依据。CMeta 只提供类型与生命周期适配；并发状态由
`Salts::Concurrency` 管理，对象池存储/lease 由 `Salts::Core` 管理，线程身份/affinity/TLS 由
`Salts::Platform` 管理。CMeta 仅负责 DataDesc lifecycle adapter；不保留
Pool/Local/Atomic/RCU 作为 `cmeta_type(...)` generic kind。

## RCU 快照

参考模型采用两个 epoch 的 reader 计数及一把既有 Salts mutex。进入、退出、
发布和回收检查为 O(1)；读取借用的不可变 payload 不持锁。互斥解锁/加锁建立
发布可见性，不依赖裸指针的硬件原子性。参考实现没有 lockless fast path。

- 数据单元：一个已完整构造的快照指针。writer 可由多个线程串行化调用，
  reader 可来自多个线程；一个活跃 guard 只有一个使用者。
- 生命周期：成功初始化/替换后，快照属于 domain；失败时仍属于调用者。
  一个 domain 至多保留 current 和一个 retired 指针，reader 数由初始化上限约束。
- 状态迁移：replace 把 current 放入 retired，切换 epoch，再发布新 current。
  retired epoch 的 reader 归零后，try_reclaim 将旧指针所有权交回调用者。
  新 epoch 的 reader 不延长旧版本的 grace period。
- 背压：reader 额度满返回 `SALTS_ENOBUFS`；retired 尚未取走时 replace 返回
  `SALTS_EBUSY`；尚有旧 reader 时 try_reclaim 返回 `SALTS_EBUSY`。
  不隐式等待、执行 destructor、分配退休节点或扩大队列。
- 关闭：close 拒绝新 reader 和 writer；已存在的 guard 仍可解锁。
  destroy 要求所有 guard 已退出、retired 已取走，将 current 所有权交回调用者。
  destroy/init 要求外部停止所有 API 调用，不能与访问 domain 的线程竞争。
- guard：初始化为零、地址稳定、禁止复制。复制的 guard 不能执行解锁。
  guard 可跨挂起或在独占交接后跨线程解锁，因为计数绑定 guard 而非 OS 线程。
  guard 和 domain 必须保存在不会因挂起失效的 owner 存储中；取消路径必须解锁。
  长期挂起会产生有界背压，不能以扩大 retired 存储掩盖停滞。
- 回收：只有 try_reclaim/destroy 返回的指针可被析构。已发布的对象不得原地修改，
  也不得把 current/retired 指针重新提交为 replacement。

内存预算为固定 domain/锁元数据、最多 `max_readers` 个 caller-owned guard、
两份快照及其拥有的 payload。payload 上限由快照 producer 的契约限定。
RCU 的移除/等待 grace period/回收分层参考
[Linux RCU 文档](https://docs.kernel.org/RCU/whatisRCU.html)。

## 类型化有界对象池

`cmeta_pool_type(Name, Type)` 生成 facade。init 验证 canonical construct ops、
native size/alignment，一次预分配全部容量；capacity 不能为零。不支持的扩展对齐
直接失败。对象内部 payload 的容量仍由 Type 自己的契约限制。

- 一个池固定属于创建线程，所有操作为单 owner；不引入锁或隐式 TLS 全局池。
- 状态：free slot → acquire/init_zero → live lease → restore_zero/release → free。
  init_zero 失败时恢复失败槽并归还，池的额度不丢失。
- lease 地址稳定、禁止复制；release 校验 owner、活跃 lease 和 native slot。
  double release、错误池及复制 lease 均失败，不能先执行 destructor 再发现错误。
- 对象借用在 release 失效。canonical move 可将值移动到调用者已初始化的
  semantic-zero 目的对象；源仍是活跃 lease 中的 semantic-zero，随后必须 release。
- destroy 拒绝未归还的 lease；池和 lease 不能以赋值或 memcpy 转移所有权。
- lifecycle 回调期间拒绝重入池 API。回调不在互斥区内执行，不维护第二套生命周期表。
- native 存储复用 object_pool；初始容量与最大容量相同，满额不重新分配或退回 heap。
  acquire/release 时间复杂度沿用 native pool；内存预算包含对齐后的 slot 和 allocation bitmap。

## Thread-local 与 atomics

`cmeta_local_type(Name, Type)` 提供显式 init/get/destroy 的线程绑定值；
`SALTS_THREAD_LOCAL Name variable` 只选择每线程存储，不自动构造或清理资源。
线程退出前必须 destroy；借用不得越过 destroy、线程退出或可能迁移线程的挂起。
普通 Local 实例同样检查创建线程。这不构成 executor shard-local registry；
shard 生命周期继续由 executor/shard owner 管理，不能把 OS TLS 当作 shard 状态。

`SALTS_ATOMIC_TYPE(Name, Type)` 包装普通 C11 atomic 操作。调用者显式选择 memory order；
load/store/CAS 拒绝不合法的 order，不隐式加强或降低顺序。C++ 使用其原生 atomics，
不能把 C `_Atomic` 对象作为跨语言 ABI。平台加速只有在 benchmark 证明收益后才能另行启用。

## 架构选择与兼容性

候选方案包括 CMeta 自带 allocator/线程 runtime、在 Core 中复用全部机制，及上述薄 facade。
选择薄 facade 可避免 CMeta → Core → CMeta 循环依赖，也不复制 allocator 或同步原语。
可选头文件明确要求相应 runtime target；基础 `cmeta/meta.h` 不引入这些依赖。
新增 API 是 additive；object_pool 配置结构和已有函数语义保持不变。
`CMETA_BUSY` 追加到现有 status 枚举尾部，不重编号已有值。Collector 保留该错误，
CFlow 同步 collect 将其分类为 `CFLOW_STATUS_WOULD_BLOCK` 并照常 abort；不新增等待或重试。
迁移由 consumer 主动选择，新功能不会自动改变插件注册表或 CNet 数据路径。
回滚可删除新 facade 与 runtime/test 注册，已有业务调用点不需要迁移。

验证覆盖满额、非法配置、部分初始化、析构和 move、错误 owner/复制 lease、嵌套 reader、
旧/新 epoch、长期 reader、关闭、并发发布/读取/回收、TLS 隔离和非法 atomic order。
ASan 验证内存生命周期，Linux thread-tool lane 验证并发数据竞争；无锁加速不作为默认实现。

## API 与验证入口

以下 `Name` 为 `cmeta_type(Kind, Name, Type);` 声明的具体名称；
每个 provider 同时公开 `Name_value_type`。所有 instance、guard、lease 在首次使用前初始化为零。
descriptor 和 canonical ops 必须活到池或 Local 销毁，不能提供短命的临时 metadata。

| 头文件 / 链接依赖 | 生成接口与参数 | 输出与错误 |
|---|---|---|
| `salts/rcu.h` / Concurrency | `Name_init(domain, initial, max_readers)`、`Name_read_lock(domain, guard)`、`Name_load(guard)`、`Name_read_unlock(guard)` | `SALTS_*` 状态；load 返回 `const Type *` 借用；空快照和无效 guard 均返回 NULL，须先检查 read_lock 的结果 |
| 同上 | `Name_replace(domain, replacement)`、`Name_try_reclaim(domain, Type **out)`、`Name_close(domain)`、`Name_destroy(domain, Type **out)` | 替换失败不转移所有权；reclaim/destroy 清零输出后检查状态；关闭后不再接收新 guard 或 replacement |
| `cmeta/pool.h` / CMeta + Core | `Name_init(pool, capacity)`、`Name_acquire(pool, Name_lease *lease)`、`Name_get(pool, lease)` | 容量必须非零；没有 canonical ops 为 `CMETA_TRAIT_MISSING`；布局不符为 `CMETA_TYPE_MISMATCH`；超限为 `CMETA_CAPACITY_EXCEEDED`；初始化分配失败为 `CMETA_OUT_OF_MEMORY`；get 无效时返回 NULL |
| 同上 | `Name_move_out(pool, lease, Type *zero_destination)`、`Name_release(pool, lease)`、`Name_destroy(pool)` | move 缺失为 `CMETA_TRAIT_MISSING`；目的存储必须位于池外；活跃 lease 或回调重入时 destroy 为 `CMETA_BUSY`；其他 owner/地址/线程错误为 `CMETA_INVALID_ARGUMENT` |
| `cmeta/local.h` / CMeta + Platform | `Name_init(local)`、`Name_get(local)`、`Name_destroy(local)`；`SALTS_THREAD_LOCAL Name variable` | canonical 初始化错误原样传播且恢复失败对象；get 失败返回 NULL；线程、复制、重复 init/destroy 错误为 `CMETA_INVALID_ARGUMENT`；回调重入为 `CMETA_BUSY` |
| `salts/atomic.h` / Concurrency | `Name_init(atomic, initial)`、`Name_load(atomic, order, Type *out)`、`Name_store(atomic, value, order)`、`Name_exchange(atomic, value, order, Type *out)` | 普通 C11 `_Atomic(Type)`；无效参数/order 为 `CMETA_INVALID_ARGUMENT`，原子对象和输出保持原值 |
| 同上 | `Name_compare_exchange(atomic, Type *expected, desired, success_order, failure_order, bool *exchanged)`、`Name_is_lock_free(atomic, bool *out)` | strong CAS；失配时返回 `CMETA_OK`、exchanged 为 false，expected 更新为实际值；无效 order 在操作前拒绝；查询 native lock-free 性质 |

load 接受 relaxed/consume/acquire/seq_cst；store 接受 relaxed/release/seq_cst；
exchange 接受全部标准 memory order。CAS failure 禁止 release/acq_rel，且不能强于 success；
release success 只允许 relaxed failure，acq_rel success 至多允许 acquire failure。
依据为 [C11 委员会草案 N1570 §7.17.7](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf)。
原子初始化必须在并发访问前完成；不能以赋值或 memcpy 复制活跃 atomic。

池存储预算可复算：令 `a = max(alignof(Type), sizeof(void *))`，
`stride = round_up(max(sizeof(Type), sizeof(void *)), a)`，则 slot 占用为
`capacity * stride`，bitmap 为 `ceil(capacity / 8)` 字节，另加一份 pool/chunk metadata。
加法及乘法溢出在分配前拒绝。acquire 不扩大这些存储；canonical callback 内的 payload
分配仍属于 Type 的契约，不能据此声称整个业务操作零分配。

可直接运行的使用示例与断言位于
[`cmeta_execution_test.c`](tests/cmeta_execution_test.c)，包括完整 canonical provider、
typed acquire/get/move/release、TLS 退出清理、release/acquire 发布和 typed RCU 所有权交回。
[`rcu_test.c`](../concurrency/tests/rcu_test.c) 展示跨线程 guard 交接、旧 epoch 背压和并发回收。

本地 Windows 使用已有 `win-dev-user`（MSVC + ASan）或 `win-clang-user`：

```powershell
cmake --preset win-dev-user
cmake --build --preset win-dev-user --target cmeta_execution_test concurrency_rcu_test cmeta_scope_test cmeta_collector_test concurrency_header_cpp_test test_object_pool
ctest --preset win-dev-user --output-on-failure -R '^(cmeta_scope_|cmeta_execution_test$|cmeta_collector_test$|concurrency_rcu_test$|concurrency_header_cpp_test$|test_object_pool$)'
```

需先进入 VS 开发者环境并提供 user preset 所需的 vcpkg 环境，见
根目录 [`README.md`](../README.md)。CI 使用相同正式测试，Linux `linux-tsan-ci`
独立 build tree 开启 ThreadSanitizer 并关闭 ASan；其他 native lane 保持原来的 sanitizer 策略。
