# 有界 Pool 的机制与 lifecycle 边界（#957）

## 状态与归属

`<object_pool_managed.h>` / `Salts::Core` 管理固定容量、线程绑定、lease 和独占事务。
`<cmeta/pool.h>` 是可选生命周期适配器：仅保存 native owner 与借用的 canonical
`cmeta_data_construct_ops`，验证 exact native size/alignment 并调用原 ops。
Core 不接收 descriptor、callback table 或析构策略，不增加第二份 lifecycle authority。

数据单元是一份固定 stride 的 slot。`object_pool_t` 是 slot bitmap、free-list、容量、
当前/峰值占用的唯一事实源；`salts_local_state` 是地址、线程和 ZERO/BUSY/READY 的
唯一事实源；`active` 只记录 BUSY 事务对应的唯一 lease。所有公开字段只读，
调用方不得直接改字段、复制活跃 owner/lease 或在回调中调用 native 完成操作。

Pool 与 lease 首次使用必须零初始化，活跃地址必须稳定。所有 payload 操作为创建线程
单 owner，排除 init/destroy 与任何其他调用重叠；不能把此 API 当成 SPSC/MPMC 池。
外部线程可以在 identity 存活且经外部同步发布的情况下调用检查/命令并得到 EINVAL；
Core 先检查 binding，拒绝外线程后才访问可变 storage/lease。所有操作使用创建时的
同一 Platform runtime/token 实例。普通指针与 publish 命名不提供跨线程同步。

借用在 discard 后失效；lease 不延长 pool 外部资源的生命。线程退出前必须显式清理
payload、discard 每份 lease，再 destroy pool。destroy 不 drain、不执行 destructor、
不隐式等待或重试，仍有 slot/事务时返回 EBUSY。借用不能跨线程迁移或 slot 复用。

## Native API

所有状态码为 `SALTS_*`，返回 int；无效 NULL、自复制、错误 owner/线程、无效 phase
为 EINVAL。失败不改变有效事务的所有权。

| API / 参数 | 成功效果 | 其他错误与约束 |
|---|---|---|
| `validate(size, alignment, capacity)` | 无分配的配置/算术校验 | size/capacity 必须非零；alignment 必须受 native allocator 支持且为 2 的幂；非法配置 EINVAL；stride/乘积溢出 ENOSPC |
| `init(pool, size, alignment, capacity)` | 一次预分配全部容量，ZERO → READY | 配置同 validate；分配失败 ENOMEM 且返回未绑定零态；重复 init EINVAL |
| `check(pool)` | READY 为 OK | 原线程 BUSY 为 EBUSY；外线程/无效 binding 为 EINVAL |
| `claim(pool, lease)` | reserve slot、绑定 lease、READY → BUSY | lease 必须全零；满额 ENOSPC；BUSY 时 EBUSY；不会初始化 payload |
| `publish(pool, lease)` | 完成当前 BUSY 事务 → READY，lease 继续存活 | 非 active lease 或错误 phase 为 EINVAL；逻辑状态转换，跨线程发布仍需外部同步 |
| `discard(pool, lease)` | BUSY → READY，归还 slot 并清零 lease | 先由调用方清理 payload；不执行 callback；仅接受 active lease |
| `lease_check(pool, lease)` / `get(pool, lease)` | 校验 READY lease / 借用 value | 检查 lease self、owner、native allocation；BUSY 为 EBUSY；get 校验失败返回 NULL |
| `enter(pool, lease)` | 校验 READY lease 后进入 BUSY | 调用方完成 mutation/destruction 后恰好 publish 或 discard 一次 |
| `move_begin(pool, lease, destination)` | 校验后 enter | destination 必须非 NULL 且位于所有 pool storage 外；semantic-zero/layout 由生命周期调用方验证 |
| `destroy(pool)` | READY 空池 → ZERO，释放存储 | 未归还 lease/BUSY 为 EBUSY；重复 destroy EINVAL |

流程：

```text
claim → BUSY → payload init → publish → READY live lease
              └ init fails → payload cleanup → discard → READY free slot
READY live lease → enter → BUSY → payload cleanup → discard → READY free slot
READY live lease → move_begin → BUSY → payload move → publish → READY live lease
```

成功 claim 的正常终态必须为 publish 或 discard。publish/discard 对照 `active` 验证，
其他 live lease 无权完成当前事务。lease 地址变化或重复完成均失败，不能通过原 slot
指针伪造另一个 lease。原 owner 的事务重入返回 EBUSY；没有锁内 callback。

存储已预分配且有 free slot 时，native alloc 正常不会失败；若该不变量被破坏，claim
返回 EINVAL 并保留 BUSY，禁止自动修复或继续使用半可信存储。调用方必须遵守字段只读。

`object_pool_contains(pool, address)` 检查所有 slot 存储字节，包括空闲槽位和内部地址。
它不同于 `object_pool_is_allocated` 的 checked-out slot start 查询，不解引用 address。
move 的外部存储校验由 owner 使用前者完成；原来的 allocated-only 查询无法保证外部目的地址。

## 容量、复杂度与错误传播

计算输入为 concrete size、alignment、capacity。令
`a = max(alignment, sizeof(void *))`，`stride = round_up(max(size, sizeof(void *)), a)`。
slot 字节数为 `capacity * stride`，bitmap 为 `ceil(capacity / 8)`，另加一份
`object_pool_s`、`object_pool_chunk_s`、caller-owned managed owner 与每份活跃 lease。
round-up 的加法和容量乘法在分配前校验溢出；bitmap 沿用 native checked allocation。
payload 保留字节与 payload 分配失败属于 concrete Type 的独立契约。

初始容量 = 最大容量，因此恰有一个 chunk；不在满额时尝试增长或 heap fallback。
init 的时间/空间为 O(capacity * stride)，正常 check/claim/get/enter/publish/discard
及 move membership/destroy 的 owner 额外工作为 O(1)；native 查询仍沿用原 chunk 算法。
没有吞吐或零分配收益声明；canonical payload callback 仍可分配自己的有界资源。
观测使用 storage 的 capacity、allocated/free count、peak usage，禁止另维护镜像计数器。

CMeta 使用 owner 的配置校验与事务接口，不保存 busy/thread/self/lease policy。
canonical init 失败时，在 BUSY 内调用原 restore_zero，再 discard；清理成功后原错误
不变。canonical move 可缺失（TRAIT_MISSING），owner 事务仍恰好 publish 结束，source
lease 存活。release 先 enter，再 restore_zero/discard；destroy 成功后清除借用 ops。
SALTS_OK/EBUSY/ENOSPC/ENOMEM 映射至原 CMETA_OK/BUSY/CAPACITY_EXCEEDED/OUT_OF_MEMORY，
canonical callback 错误原样传播；owner 完成错误直接报告，不强行修复。

## 方案、迁移与验证

候选包括仅移动 header（仍在 CMeta 持有 policy）、Core 存储额外 lifecycle callback table，
以及 Core 事务 + canonical CMeta adapter。选择第三种：复用原 allocator/Platform binding，
metadata 与机制依赖单向，不新增 callback registry、调度、锁或 grow policy。
CMeta core aggregate/Reflection/DataDesc ABI 与原 object_pool 配置/函数语义保持不变。

兼容性（HIGH）：`cmeta_pool_state` 与生成 Pool/lease 的布局改变，consumer 需全量重编译；
低层 `cmeta_pool_lease` 和纯 lease-check helper 移除，使用 owner 的
`object_pool_managed_lease` / `object_pool_managed_lease_check`，不保留 forwarding alias。
生成的 typed init/acquire/get/move_out/release/destroy 签名和正常状态码保持一致。
非法池内 move destination 现在在 callback 前拒绝。多项同时非法输入先验证 owner 配置，
再验证 canonical metadata，不承诺先前不同非法错误的优先级。

迁移先同步 owner/header，再全量重编译 consumer；不改变序列化数据或 descriptor epoch。
回滚需成套回退 owner、adapter、consumer 和 CI，并重编译，禁止混用新旧 Pool 布局。

正式 TinyTest 用例：`utils/tests/test_object_pool_managed.c` / C++ counterpart 验证真实
事务、容量/溢出、复制/错误 lease、wrong active、所有 foreign 命令、重复 reuse 与非法
move storage。`test_object_pool.c` 验证 contains 对 live/free/interior storage 的区分。
`cmeta_execution_test.c` 验证同一 canonical ops 的部分构造、恢复、move、release 重入、
CSTL owned Vec payload、metadata 拒绝与 TLS 相邻回归。`cmeta_pool_cpp_test.cpp` 验证 C++ typed adapter 的真实 canonical lifecycle。测试使用 TinyTest 正式 runner，不建立临时 consumer 或脚本测试。

Windows 在 VsDevCmd x64 环境使用根版本化 user presets：

```powershell
cmake --preset win-dev-user
cmake --build --preset win-dev-user --parallel 4 --target test_object_pool test_object_pool_managed test_object_pool_managed_cpp cmeta_execution_test cmeta_pool_cpp_test
ctest --preset win-dev-user --output-on-failure -R '^(test_object_pool|test_object_pool_managed|test_object_pool_managed_cpp|cmeta_execution_test|cmeta_pool_cpp_test)$'
```

相邻回归与六平台 CI 同时验证 scope、Local、Concurrency、CSTL 和 CFlow；使用既有
MSVC ASan、Clang、Release 与 GCC ASan/TSan lanes。安装包 Reflection-only C/C++ 测试
仍仅消费 CMeta/TinyTest，避免把 optional Pool 依赖引入 core aggregate。
