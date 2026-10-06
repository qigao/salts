# Local thread binding 与 metadata adapter（#957）

`<salts/local.h>` 属于 `Salts::Platform`。它只管理一份 caller-owned binding：
自身地址、owner 地址、存活的 OS 线程 token 和 ZERO/BUSY/READY phase。
没有 allocator、payload 存储、lifecycle callbacks、TLS 注册或 thread-exit destructor。
`SALTS_THREAD_LOCAL` 继续属于既有 `<salts/thread.h>`。

## 状态与使用协议

数据单元为一个固定 binding，最多绑定一个 owner，空间 O(1)、每次转换 O(1)。
owner 和 binding 均需保持原地址、在原线程清理；初始化为零后再调用 begin。
Native Type 的大小、对齐、payload 容量与构造/析构由 consumer 自己的契约约束。
结构字段供 caller 分配存储，绑定后不能直接修改，也不能复制或跨线程迁移。

| API（参数均为 `state, owner`） | 允许转换 / 结果 |
|---|---|
| `salts_local_begin` | ZERO → BUSY；绑定当前线程，owner 必须为非 NULL 的存活地址 |
| `salts_local_publish` | BUSY → READY；consumer 已成功构造或完成独占操作 |
| `salts_local_check` | READY 返回 SALTS_OK，原 owner 的 BUSY 返回 SALTS_EBUSY |
| `salts_local_enter` | READY → BUSY；consumer 随后执行独占回调；BUSY 返回 SALTS_EBUSY |
| `salts_local_reset` | BUSY → ZERO；consumer 已恢复失败对象或完成析构；不运行 payload callback |

所有转换成功返回 `SALTS_OK`。NULL、错误 owner/线程、复制 binding、无效 phase、
重复 begin/reset 及在 READY 上 reset 均返回 `SALTS_EINVAL`；失败不改变 binding。
每次成功 begin 必须 publish 后正常清理，或在恢复失败 payload 后 reset，不能遗留 BUSY。
每次 enter 必须在操作结束后 publish，或在析构后 reset。机制不隐式 retry、阻塞或 drain。

同步模型为 single owner：只有绑定线程可改变 phase。Begin/reset 属于控制面，要求外部
quiescence。发布后 self/owner/thread 身份不变；foreign API 调用在读取 phase 前拒绝
错误线程，可以与 owner 的 BUSY/READY 转换并行，但不得与 begin/reset 并行。
共享给 foreign caller 前仍需通过线程创建、锁或其他既有同步发布 binding。
`phase` 的直接读取仅限 owner，不是跨线程 atomic 查询。

借用在 reset、对象析构或 owner 线程退出前结束；不得跨可能迁移 OS 线程的协程挂起。
线程 token 可能在线程退出后复用，因此必须在退出前完成清理。所有操作须使用创建 binding
的同一 Platform runtime/token 实例；不同静态链接 DSO 的 TLS token 不是跨模块线程身份契约。
Shard-local 生命周期仍属于 executor，不由 OS TLS 或本机制模拟。

## CMeta 的剩余职责与错误

`<cmeta/local.h>` 保留 `cmeta_type(Local, Name, Type)`，因为它绑定并消费 canonical
DataDesc construct ops、校验 native layout，并生成 exact Type 的借用接口。
`cmeta_local_state` 只含 Platform binding 和借用的 canonical ops，不再保存独立线程/busy 状态。

Init 先验证 canonical ops/size/alignment，再 begin、执行 canonical init_zero；失败时
在 BUSY 下调用同一 restore_zero，最后 reset，原 canonical 错误原样传播。
成功后 publish，只有 READY 可 get。Destroy 先 enter，再 restore_zero/reset。
回调可尝试 facade 操作但会收到 CMETA_BUSY；不能直接修改 owner binding、ops 或执行原生转换。
没有第二套生命周期表，也没有 constructor、隐式 thread-exit cleanup 或 provider retention。
Platform 的 SALTS_OK/EBUSY/EINVAL 在 adapter 边界明确转换为 CMETA_OK/BUSY/INVALID_ARGUMENT。
Metadata 预检失败不启动 binding；原 descriptor 必须存活到 Local 销毁。

## 决策、兼容性与回滚

候选方案为仅移除 aggregate include、给 Platform 引入 generic lifecycle callback table，
或迁移纯绑定状态机。选择状态机可以让 Platform 不解释 canonical CMeta 错误/ops，也避免
用第二份 callback table 复制生命周期事实。CMeta 保留 callback 调用顺序与语义，Platform
拥有所有地址、线程和 phase 转换；依赖仍为 optional adapter → CMeta + Platform。
Pure Reflection 的 CMeta archive/aggregate 不引入 Platform。

兼容性（HIGH）：`cmeta_local_state` 和生成的 Local 实例布局改变，含 Local 的消费者需
全量重编译；不能跨 DSO 混用旧/新 layout。Descriptor/Reflection ABI 不变。
删除纯 TLS alias `cmeta_thread_local`，改用 `static SALTS_THREAD_LOCAL Name variable = {0};`；
不保留转发 spelling。Typed Local 的 init/get/destroy 返回码与成功/失败行为保持一致。
迁移只发生于编译/链接，不搬迁 live 实例；回滚需成套恢复 adapter、Platform target 与消费者。

## 正式验证

[`platform_local_test.c`](tests/platform_local_test.c) 和
[`platform_local_cpp_test.cpp`](tests/platform_local_cpp_test.cpp) 只链接 Platform/TinyTest，
实际执行 phase 转换、错误不变性、复制/错误 owner 拒绝和跨线程/TLS 清理。
[`cmeta_execution_test.c`](../cmeta/tests/cmeta_execution_test.c) 验证同一 canonical ops 下的
失败恢复、构造/析构 callback 重入、metadata 预检、真实 typed get、TLS 隔离及原 Pool 回归。
既有 scope/execution CI 的六工具链（含 Linux ASan/TSan）构建并运行这些正式测试。

在 VS 开发者环境并提供仓库 preset 所需环境后：

```powershell
cmake --preset win-dev-user -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=OFF -DBUILD_EXAMPLES=OFF
cmake --build --preset win-dev-user --target platform_local_test platform_local_cpp_test cmeta_execution_test --parallel 4
ctest --preset win-dev-user --output-on-failure -R '^(platform_local_|cmeta_execution_test$)'
```

Core object_pool 的 allocation/lease policy 已由 managed owner 承载，见
[`OBJECT_POOL_MANAGED.md`](../utils/OBJECT_POOL_MANAGED.md)。
