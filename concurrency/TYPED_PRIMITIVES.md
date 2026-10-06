# Concurrency typed atomic 与 RCU（#957）

`<salts/atomic.h>` 的 `SALTS_TYPED_ATOMIC(Name, Type)` 和
`<salts/typed_rcu.h>` 的 `SALTS_TYPED_RCU(Name, Type)` 属于 `Salts::Concurrency`。
两者不依赖 CMeta，不生成 descriptor、注册表或第二套生命周期语义。
Atomic 使用原生 C11 `_Atomic(Type)`；RCU 直接调用既有 `salts_rcu`，不改变算法、锁、
reader 上限、retired 槽位或析构归属。声明时使用完整 native Type（指针类型可先 typedef）。
两者均公开 `Name_value_type`。Atomic 仅支持 C11；C++ 使用 `std::atomic`。
Typed RCU 同时支持 C11/C++17；domain 和 guard 必须零初始化且保持原地址。

兼容性（HIGH）：删除 `<cmeta/atomic.h>`、`<cmeta/rcu.h>` 及
`cmeta_type(Atomic/Rcu, ...)`，不保留 aliases。Consumer 更新 include 和声明；
Atomic 状态由 `CMETA_OK/CMETA_INVALID_ARGUMENT` 改为 `SALTS_OK/SALTS_EINVAL`，
不能按原数值判断。RCU 继续返回同一 `SALTS_*` 状态。
迁移发生于编译/链接阶段，不迁移运行期存储；需成套更新调用点。
回滚也必须成套恢复一个 owner，不能让两套 spelling 独立推进相同状态。

## Atomic API 与失败语义

所有实例指针和输出指针必须非 NULL。成功返回 `SALTS_OK`；非法参数或顺序返回
`SALTS_EINVAL`，不触碰原子对象、expected 或输出。初始化必须在并发访问前完成；
原子存储由该实例独占，不允许复制活跃对象。初始化后的 load/store/exchange/CAS
允许 MPMC，顺序由调用者显式指定，不隐式加强或降低。每个实例只占原生 atomic
及其对齐，无额外 allocation、队列或 callback；所有操作为 O(1) API，是否 lock-free
由原生实现决定。销毁存储前由 owner 停止并发访问，不提供隐式 drain。

| 生成函数 | 参数与返回输出 |
|---|---|
| `Name_init` | `Name *atomic, Type initial`；exclusive initialization |
| `Name_load` | `const Name *atomic, memory_order order, Type *out` |
| `Name_store` | `Name *atomic, Type value, memory_order order` |
| `Name_exchange` | `Name *atomic, Type value, memory_order order, Type *out`；输出旧值 |
| `Name_compare_exchange` | `Name *atomic, Type *expected, Type desired, memory_order success, memory_order failure, bool *exchanged`；strong CAS，失配仍成功、exchanged=false 且 expected 更新为实际值 |
| `Name_is_lock_free` | `const Name *atomic, bool *out`；查询原生 lock-free 性质 |

load 接受 relaxed/consume/acquire/seq_cst；store 接受 relaxed/release/seq_cst；
exchange 接受全部标准 memory order。CAS failure 禁止 release/acq_rel，且不能强于 success；
release success 只允许 relaxed failure，acq_rel success 至多允许 acquire failure。
依据为 [C11 委员会草案 N1570 §7.17.7](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf)。
原子初始化必须在并发访问前完成；不能以赋值或 memcpy 复制活跃 atomic。


## RCU 快照协议

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

## Typed RCU API

| 生成函数 | 参数与返回输出 |
|---|---|
| `Name_init` | `Name *domain, Type *initial, size_t max_readers`；仅成功转移 initial |
| `Name_read_lock` | `Name *domain, Name_guard *guard`；成功借用不可变快照 |
| `Name_load` | `const Name_guard *guard`；返回 `const Type *`，空快照/无效 guard 为 NULL，须先检查 read_lock |
| `Name_read_unlock` | `Name_guard *guard`；释放原 guard 一次 |
| `Name_replace` | `Name *domain, Type *replacement`；失败不转移 replacement |
| `Name_try_reclaim` | `Name *domain, Type **out`；交回 retired，失败清零输出 |
| `Name_close` | `Name *domain`；停止新 reader/writer admission |
| `Name_destroy` | `Name *domain, Type **out`；交回 current，busy 时 domain 不变；失败清零输出 |

所有状态和 NULL 语义直接遵循 [`salts/rcu.h`](include/salts/rcu.h)。Typed wrapper
不保留 provider、执行 destructor 或选择 retry，调用者只析构明确交回的 payload。

## 正式验证与示例

[`typed_primitives_test.c`](tests/typed_primitives_test.c) 提供可编译运行的声明与调用：
integer/pointer atomic、完整 C11 CAS success/failure 矩阵、release/acquire 发布和 typed RCU
所有权交回。C++ 的 borrowed const snapshot、复制 guard 拒绝和错误输出见
[`typed_rcu_cpp_test.cpp`](tests/typed_rcu_cpp_test.cpp)。两者只链接 Concurrency 与 TinyTest。
既有 [`rcu_test.c`](tests/rcu_test.c) 继续验证并发 readers/writers、跨线程 guard 和有界回收。

在 VS 开发者环境及仓库 preset 所需环境下：

```powershell
cmake --preset win-dev-user -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=OFF -DBUILD_EXAMPLES=OFF
cmake --build --preset win-dev-user --target concurrency_typed_primitives_test concurrency_typed_rcu_cpp_test concurrency_rcu_test --parallel 4
ctest --preset win-dev-user --output-on-failure -R '^(concurrency_typed_|concurrency_rcu_test$)'
```

CI scope/execution gate 同时构建这些正式测试；Linux TSan/ASan 和
GCC/MSVC/ClangCL/AppleClang 覆盖发布/借用和语言契约。CMeta Pool/Local 的 metadata
适配仍见 [`EXECUTION_PRIMITIVES.md`](../cmeta/EXECUTION_PRIMITIVES.md)，owner 拆分继续由 #957 跟踪。
