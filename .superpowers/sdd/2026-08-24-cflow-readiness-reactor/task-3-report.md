# Task 3 报告：Linux epoll readiness backend

## 状态

DONE

## RED

真实 Linux 验证运行在 `root@eu` 的独占临时副本
`/root/codex-cflow-readiness-task3.14vOXu`；未覆盖或修改远端其他工作目录。

1. native suite 首次构建运行时，8/8 tests 均因
   `turbo_readiness_reactor_init()` 返回 `TURBO_ENOTSUP (-4039)` 失败；当时只有
   unsupported factory，没有生产 backend。
2. 为每次 arm 增加独立 generation token 的 contract test 后，旧 backend vtable
   编译失败：`readiness.c` 对 `backend_ops.arm` 参数不足，epoll/fake arm 签名不匹配。
   这证明旧 registration token 不能区分同一 slot 上的 unarm/rearm 旧 ready batch。
3. stale-arm contract 首次运行时，预期 `stale_events == 1`、实际为 `2`；原因是同一
   arm 已交付后的重复事件被 arm-token 清零误分类为 stale。dispatch 现先按 slot
   非 `ARMED` 归类 duplicate，再验证仍处于 `ARMED` 的 per-arm token。

实现过程中 native repeat 还暴露测试在 callback body 已返回、state-engine epilogue
尚未完成时立即 rearm，得到 `TURBO_EBUSY`。测试改为通过 reactor stats 等待
`callbacks_inflight == 0`，没有用 sleep 掩盖状态机边界。

## GREEN

Linux 最终 fresh 验证：

```bash
cd /root/codex-cflow-readiness-task3.14vOXu
PROJECT_ROOT=$PWD VCPKG_ROOT=/opt/vcpkg cmake --fresh --preset linux-release-user
grep -Fx 'TURBO_ENABLE_EPOLL_READINESS:BOOL=ON' \
  build/linux-gcc-release/CMakeCache.txt
PROJECT_ROOT=$PWD VCPKG_ROOT=/opt/vcpkg cmake --build \
  --preset linux-release-user --target \
  platform_readiness_epoll_test platform_readiness_fake_test \
  platform_clock_test platform_thread_test platform_header_cpp_test
PROJECT_ROOT=$PWD VCPKG_ROOT=/opt/vcpkg ctest \
  --preset linux-release-user \
  -R '^platform_(clock|thread|header_cpp|readiness_fake|readiness_epoll)_test$' \
  --output-on-failure
```

结果：cache 精确显示 option `ON`；5/5 CTest 通过，0 failed，0.28 秒。直接运行
shared fake contract 为 37/37 tests、613 assertions；native pipe/socketpair suite 为
8/8 tests、102 assertions。额外执行：

```bash
ctest --preset linux-release-user -R '^platform_readiness_epoll_test$' \
  --repeat until-fail:200 --output-on-failure
```

结果：连续 200 次通过，0 failed，10.98 秒。

Windows 相邻回归通过 VS 2022 Developer Command Prompt 执行：

```powershell
cmake --preset win-release-user
cmake --build --preset win-release-user --target `
  platform_readiness_fake_test platform_readiness_unsupported_test `
  platform_header_cpp_test
ctest --preset win-release-user `
  -R "^platform_(readiness_fake|readiness_unsupported|header_cpp)_test$" `
  --output-on-failure
```

结果：3/3 CTest 通过，0 failed，0.26 秒；Windows 继续明确验证 native factory
`TURBO_ENOTSUP` 且输出 handle 为空。Task 1 已有的非 Linux 显式打开 option 时
configure fail-fast 门禁保持不变。

## 改动

- 新增 Linux-only epoll backend：一个 reactor thread、level-triggered
  `EPOLLONESHOT`、eventfd control token `0`、固定 event batch；未使用
  `EPOLLET`、poll/select、timer scan、thread-per-handle 或 fallback。
- `epoll_event.data.u64` 使用 per-arm generation token；backend record 同时保存当前
  registration token。dispatch 先验证 registration generation，再验证当前 arm，
  因而 slot/fd-number reuse 和 unarm/rearm 的旧 userspace batch均不能误投。
- backend lifecycle 增加显式 destroy hook；native backend 拥有并释放 epoll fd、
  eventfd、thread、record table、event batch、mutex/condition，registration 只借用用户
  fd。close 仅 `EPOLL_CTL_DEL`，绝不关闭用户 fd。
- Linux Release user preset 显式打开 feature；Linux CI configure 后检查 CMake cache
  精确为 `ON`。feature OFF 或 unsupported host 仍返回 `TURBO_ENOTSUP`，无 fallback。
- shared fake contract 增加 per-arm stale/duplicate 精确计数；Linux native suite 使用
  nonblocking pipe/socketpair 覆盖 capacity、read-ready、drain-to-WOULD_BLOCK/rearm、
  hangup、unarm、borrowed-fd close、fd-number reuse、terminal shutdown 和 blocked
  callback shutdown/join。所有数据 read/write 只在 test side 完成。

## 所有权、容量、线程与错误分析

- `事实`：Platform slot table 仍是 registration/state 的事实源；epoll record table
  只是固定容量 native watch 映射。两个表都只在 init 分配，register/arm/unarm/
  close/dispatch 均不分配、不扩容；Platform state engine 在满额时精确返回
  `TURBO_ENOBUFS` 并更新 `rejected_full`。
- `计算`：init 验证 `capacity > 0`、`batch > 0`、
  `batch <= capacity + 1`、`capacity <= UINT32_MAX - 1`、`batch <= INT_MAX`，并验证
  `capacity * sizeof(record)` 与 `batch * sizeof(epoll_event)` 不溢出。
- `事实`：register/arm/unarm/close 的 native hook 由 Task 1 per-slot
  `control_inflight` gate 串行化；shutdown 在 admission closed 后等待所有已开始 hook，
  再唤醒并 join reactor thread。user callback 只由 state-engine dispatch/fan-out 调用，
  不持 backend mutex 或 Platform mutex。
- `事实`：无需 event-batch barrier。`epoll_wait` 返回一个 `EPOLLONESHOT` 事件后旧
  watch 已被内核禁用；callback/control thread 的 MOD 表示新的 arm。仍在 userspace
  batch 中的旧 payload 由 per-arm generation 丢弃，因此 callback 内允许的操作不会
  等待 reactor 自身。
- `事实`：`epoll_ctl`、`fcntl`、eventfd read/write、`epoll_wait` 均重试 EINTR；
  DEL 的 ENOENT/EBADF 只在 watch 已不存在时规范化为成功。其余 hook error 在本地
  record 提交前返回，arm 失败回滚 staged token/armed 状态，可由公共 API 重试。
- `事实`：非 EINTR `epoll_wait` error 在 errno 可能被其他调用影响前保存为 exact
  negative status，经过 state engine 向 armed registrations fan-out。fatal fan-out
  完成后才发布 `thread_exited`；shutdown 只有 `turbo_thread_join` 成功才返回 OK，
  join error 原样返回并保留 thread handle 供重试。

## 自审

- `HIGH`：提交前审计发现 fatal path 曾先发布 `thread_exited` 再 fan-out，且 join
  error 被错误规范化为 OK；已按上述顺序修正，并在修正后重新 fresh configure、
  build、5/5 CTest 与 native repeat 200 次。
- `MED`：无未解决发现。检查了 arm 即时 readiness、DEL 与已返回 batch 竞态、
  shutdown 与 per-slot hook gate、callback 中 arm/unarm/close 的自等待路径；backend
  不持锁进入 state engine，state engine 不持锁调用 backend/user callback。
- `LOW`：无未解决发现。CodeGraph sync 成功，affected tests 为 epoll native、shared
  contract 和 fake backend；`git diff --check` 通过；实现/测试中无
  TODO/FIXME/HACK、`EPOLLET`、poll/select。

残余验证边界：没有对内核 `epoll_wait` fatal 分支做确定性 syscall fault injection，
其 exact errno/fan-out 路径由代码审计和 shared fake fatal contract 覆盖；本轮未运行
ASan/TSan。native suite 是 Linux syscall lifecycle 的等价 fixture assertions，fake-only
hook failure/barrier assertions 继续由全 host shared contract 运行，未伪称可由真实
epoll 注入这些 fake-only 故障。

## Commit

`feat(platform): add epoll readiness backend`（本报告与实现同一提交；最终 SHA 由提交
结果记录）。
