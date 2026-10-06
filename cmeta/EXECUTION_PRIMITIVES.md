# 执行与资源原语（#927）

本协议是实现和测试的依据。CMeta 只提供类型与生命周期适配；并发状态由
`Salts::Concurrency` 管理，对象存储复用 `Salts::Core` 的 `object_pool_t`，
线程身份由 `Salts::Platform` 提供。Pool/Local 的 descriptor ABI 保持不变；Atomic/RCU 的 owner 迁移见下述协议。

Atomic 和 typed RCU 已迁移至 Concurrency；声明、错误码、容量和回收协议见
[`TYPED_PRIMITIVES.md`](../concurrency/TYPED_PRIMITIVES.md)。

## 类型化有界对象池

`cmeta_type(Pool, Name, Type)` 生成 facade。init 验证 canonical construct ops、
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

## Thread-local

`cmeta_type(Local, Name, Type)` 提供显式 init/get/destroy 的线程绑定值；
`SALTS_THREAD_LOCAL Name variable = {0};` 由 Platform 选择每线程存储，不自动构造或清理资源。
线程退出前必须 destroy；借用不得越过 destroy、线程退出或可能迁移线程的挂起。
普通 Local 实例同样检查创建线程；地址、线程和 busy phase 由 Platform 的单一 binding 管理，
CMeta 仅绑定 canonical ops，见 [`LOCAL_BINDINGS.md`](../platform/LOCAL_BINDINGS.md)。这不构成 executor shard-local registry；
shard 生命周期继续由 executor/shard owner 管理，不能把 OS TLS 当作 shard 状态。

## 架构选择与兼容性

候选方案包括 CMeta 自带 allocator/线程 runtime、在 Core 中复用全部机制，及上述薄 facade。
选择薄 facade 可避免 CMeta → Core → CMeta 循环依赖，也不复制 allocator 或同步原语。
可选头文件明确要求相应 runtime target；基础 `cmeta/meta.h` 不引入这些依赖。
Pool/Local 是 additive lifecycle adapters；object_pool 配置结构和已有函数语义保持不变。
`CMETA_BUSY` 追加到现有 status 枚举尾部，不重编号已有值。Collector 保留该错误，
CFlow 同步 collect 将其分类为 `CFLOW_STATUS_WOULD_BLOCK` 并照常 abort；不新增等待或重试。
Pool/Local 由 consumer 显式选择，不自动改变插件注册表或 CNet 数据路径。
Atomic/RCU 已按 #957 成套迁移 include、声明和错误码，consumer 的迁移与回滚要求见 Concurrency 协议。

Pool/Local 验证覆盖满额、非法配置、部分初始化、析构和 move、错误 owner/复制 lease 与 TLS 隔离。
RCU/Atomic 的独立 owner 验证覆盖 epoch、关闭、并发发布/读取/回收及非法 memory order。
ASan 验证内存生命周期，Linux thread-tool lane 验证并发数据竞争；无锁加速不作为默认实现。

## API 与验证入口

以下 `Name` 为 `cmeta_type(Kind, Name, Type);` 声明的具体名称；
每个 provider 同时公开 `Name_value_type`。所有 instance、lease 在首次使用前初始化为零。
descriptor 和 canonical ops 必须活到池或 Local 销毁，不能提供短命的临时 metadata。

| 头文件 / 链接依赖 | 生成接口与参数 | 输出与错误 |
|---|---|---|
| `cmeta/pool.h` / CMeta + Core | `Name_init(pool, capacity)`、`Name_acquire(pool, Name_lease *lease)`、`Name_get(pool, lease)` | 容量必须非零；没有 canonical ops 为 `CMETA_TRAIT_MISSING`；布局不符为 `CMETA_TYPE_MISMATCH`；超限为 `CMETA_CAPACITY_EXCEEDED`；初始化分配失败为 `CMETA_OUT_OF_MEMORY`；get 无效时返回 NULL |
| 同上 | `Name_move_out(pool, lease, Type *zero_destination)`、`Name_release(pool, lease)`、`Name_destroy(pool)` | move 缺失为 `CMETA_TRAIT_MISSING`；目的存储必须位于池外；活跃 lease 或回调重入时 destroy 为 `CMETA_BUSY`；其他 owner/地址/线程错误为 `CMETA_INVALID_ARGUMENT` |
| `cmeta/local.h` / CMeta + Platform | `Name_init(local)`、`Name_get(local)`、`Name_destroy(local)`；`SALTS_THREAD_LOCAL` | canonical 初始化错误原样传播且恢复失败对象；get 失败返回 NULL；线程、复制、重复 init/destroy 错误为 `CMETA_INVALID_ARGUMENT`；回调重入为 `CMETA_BUSY` |

池存储预算可复算：令 `a = max(alignof(Type), sizeof(void *))`，
`stride = round_up(max(sizeof(Type), sizeof(void *)), a)`，则 slot 占用为
`capacity * stride`，bitmap 为 `ceil(capacity / 8)` 字节，另加一份 pool/chunk metadata。
加法及乘法溢出在分配前拒绝。acquire 不扩大这些存储；canonical callback 内的 payload
分配仍属于 Type 的契约，不能据此声称整个业务操作零分配。

可直接运行的使用示例与断言位于
[`cmeta_execution_test.c`](tests/cmeta_execution_test.c)，包括完整 canonical provider、
typed acquire/get/move/release 和 TLS 退出清理。
[`rcu_test.c`](../concurrency/tests/rcu_test.c) 展示跨线程 guard 交接、旧 epoch 背压和并发回收。

本地 Windows 使用已有 `win-dev-user`（MSVC + ASan）或 `win-clang-user`：

```powershell
cmake --preset win-dev-user
cmake --build --preset win-dev-user --target cmeta_execution_test concurrency_typed_primitives_test concurrency_typed_rcu_cpp_test concurrency_rcu_test cmeta_scope_test cmeta_collector_test concurrency_header_cpp_test test_object_pool
ctest --preset win-dev-user --output-on-failure -R '^(cmeta_scope_|cmeta_execution_test$|cmeta_collector_test$|concurrency_rcu_test$|concurrency_typed_|concurrency_header_cpp_test$|test_object_pool$)'
```

需先进入 VS 开发者环境并提供 user preset 所需的 vcpkg 环境，见
根目录 [`README.md`](../README.md)。CI 使用相同正式测试，Linux `linux-tsan-ci`
独立 build tree 开启 ThreadSanitizer 并关闭 ASan；其他 native lane 保持原来的 sanitizer 策略。
