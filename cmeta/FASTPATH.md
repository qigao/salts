# Typed static call over Platform fastpath (#923 / #963)

## 决策与边界

Salts::Platform 提供 C11 原子 bool gate 和可选 native acquire-load backend；CMeta 只提供精确类型的原子函数指针槽与 Function ABI 校验。默认使用
acquire load / release store 的可移植实现；没有反射热路径、JIT、可执行内存分配、
运行时指令修改或词法析构汇编。`FunctionDecl` 同时生成函数指针 typedef，槽复用它，
不另写签名。已有 descriptor 的布局和相等查询不变。
ELF x86_64 汇编入口保留 ENDBR64 与 IBT/SHSTK property，目标调用仍由 C 编译器
生成，不绕开现有控制流保护。没有降低项目的编译或链接安全选项。

候选方案包括原子 C、固定汇编 load、运行时 direct-call patch。选择原子 C 作为
语义基准，固定汇编只作显式选择的后端；patch 会引入 W^X、指令缓存同步、并发代码
修改和平台策略依赖，当前不实现。这里借用 Linux static-key 的控制面/数据面分离，
不承诺 Linux 内核指令 patch 的零 load 成本。

`SALTS_PLATFORM_NATIVE_FASTPATH=ON` 编译独立汇编：Windows x64 MASM、Unix x86_64、
Unix AArch64。它们通过普通平台 C ABI 返回原子槽中的值，最终调用仍保留精确 C
签名及平台对 scalar、aggregate、浮点和函数指针的传参/返回约定。x86_64 使用
对齐 load，AArch64 使用 `LDARB` / `LDAR` acquire load。只接受始终 lock-free、
布局符合断言的原子对象；不依赖 C++ `std::atomic` 的布局。明确请求 native 而
架构、布局或 OS 不支持时失败，不自动改用其他后端。
MSVC C header 的 lock-free 宏统一报告 1；其实现对不超过 8 字节的 2 的幂大小
保证 lock-free。该后端由一/八字节布局断言及实际对象的 `atomic_is_lock_free`
回归测试限定；GCC/Clang 另要求 bool/pointer lock-free 宏为 2。

汇编读不能由 ThreadSanitizer 插桩，因此 native + TSan 在 configure 时拒绝。
并发 TSan 验证使用可移植实现；native correctness 另由 ABI、并发和 ASan 测试验证。
汇编不替代 minicoro 的既有上下文实现；测试在同一协程中比较 reference/native
调用并跨 yield 保存局部值，使用 #922 的既有执行和栈生命周期。

## 状态与生命周期协议

每个 key 的事实源是一个 `atomic_bool`；每个 call 的事实源是一个精确类型的
原子函数指针。无队列、动态容量或堆分配，空间 O(1)，load/store O(1)。初始化、
销毁以及存储所在模块卸载要求 quiescent；运行期间允许 MPMC 更新和读取，最后
一次原子修改决定当前值。普通 invoke 和显式 native invoke 均 acquire 一次，然后
执行该次读出的目标；它可能在后续 update 前后继续执行。

发布使用 release。只有 acquire 读到对应发布（或 C 原子规则允许的 release
sequence）才建立对此前数据的可见性；bool 反复切换不是版本或完成通知。
关联可变数据仍需自己的同步或不可变快照协议，不能靠 gate 获得互斥。

目标代码由调用方所属模块持有。ABI metadata 只在控制面校验期间借用，不保存
进槽；candidate metadata 可在 update 返回后释放，default metadata 在每次更新
时必须仍有效。update 不 retain、drain、取消或卸载旧目标；所有旧代码目标必须
存活到读者和在途调用结束。停机顺序是停止
接收调用、等待所有读者/调用结束，再释放槽或卸载提供者。替换不引入第二份
状态，不依赖 #927 未合并的 guard 实现。

## 公开使用

`<salts/fastpath.h>` 暴露 Platform-owned static key；`<cmeta/fastpath.h>` 显式暴露 CMeta typed static-call projection。`<cmeta/meta.h>` 不暴露这些 optional runtime/control-plane 能力。C11 的
`SALTS_FAST_KEY(name, initial)` 定义原子 gate；`cmeta_fast_branch(&name)`
读取，`cmeta_fast_enable` / `cmeta_fast_disable` 发布新值，`cmeta_fast_key_set` 可显式
指定状态。NULL 更新返回 `SALTS_EINVAL`；读取要求非 NULL 活对象。
C++17 只借用 C 定义的 opaque key，通过同名读取和控制 API 访问。

`cmeta_static_call(slot, default_function)` 必须位于 C 文件作用域；在一个 TU
定义槽，不能放进被多个 TU 包含的头中重复定义。默认目标需先有 `FunctionDecl`
或 `Function0Decl`。`cmeta_static_invoke(slot, args...)` 调用精确签名；零参数用
`cmeta_static_invoke0(slot)`，避免 C11 空 variadic 参数扩展。显式 native API 是
`cmeta_fast_key_read_native`、`cmeta_static_native_invoke` / `invoke0`，仅 native
构建暴露。默认 invoke 始终使用参考实现，开启构建选项也不改变它。

`cmeta_static_update(slot, target_function)` 从同一声明取 pointer 和 ABI，编译期
拒绝签名不匹配的符号。生成的 `slot_set` 是高级控制接口：调用方必须如实关联
精确类型 pointer 与 metadata。NULL slot/target、无效 metadata 返回
`CMETA_INVALID_ARGUMENT`；不兼容返回 `CMETA_TYPE_MISMATCH`。失败保留原目标。

`cmeta_function_abi_contract_compatible` 忽略函数名和参数名，要求有效且显式的
ABI carrier、语义类型、参数方向/所有权、结果 flags、effects 和 properties 精确
一致；UNSPECIFIED carrier 不兼容。它只用于控制面，含 descriptor 校验的时间为
O(n²)，生成声明最多支持既有的 16 个参数。它不证明提供者代码履行了声明的契约。

完整可运行示例见 `tests/cmeta_fastpath_test.c` 与
`tests/cmeta_fastpath_coroutine_test.c`，包含声明、实现、切换和错误检查。

## 验证与默认策略

formal TinyTest 覆盖 gate publication、MPMC 替换、失败不修改、跨 TU metadata、
精确 C 签名、C++ opaque 消费及 coroutine yield/resume。CI 在 GCC、GCC ASan、
MSVC、ClangCL、AppleClang 上开启 native，验证 x86_64 和 AArch64。
Release `cmeta_fastpath_benchmark` 比较普通可预测 branch、reference/native key
以及 direct/reference/native call。`CMETA_BUILD_BENCHMARKS=ON` 可只构建此 benchmark，
不需打开所有项目 benchmark。结果有正确性断言，不设跨机器不稳定的性能阈值。

native 增加一次普通外部 C 调用，可能比内联原子更慢。默认保持 OFF；只有目标
负载的测量支持后才能调整默认策略。回滚可关闭选项并移除显式 native 使用，
不改变槽、metadata 和所有权契约；无数据格式迁移和新增外部依赖。

本地 Windows x64 / Ryzen 9 7940HX / MSVC 19.44 Release `/O2 /Ob2`
（25 samples × 1,000,000 ops，禁用 LTO）
实测均值如下，单位 ns/op；属于该机器上的事实，不作为其他机器的性能保证：

| 路径 | 普通 C | reference | native |
| --- | ---: | ---: | ---: |
| disabled key | 0.430 | 0.437 | 1.109 |
| enabled key + callback | 1.132 | 1.126 | 2.016 |
| call | 1.331 | 1.320 | 2.195 |

此结果不支持默认开启 native。用 user preset 复验：进入 Visual Studio x64
开发环境，设置现有 vcpkg 环境后执行
`cmake --preset win-release-user -DBUILD_BENCHMARKS=OFF -DCMETA_BUILD_BENCHMARKS=ON -DSALTS_PLATFORM_NATIVE_FASTPATH=ON`、
`cmake --build --preset win-release-user --target cmeta_fastpath_benchmark`、
`ctest --preset win-release-user -V -R '^cmeta_fastpath_benchmark$'`。

参考：[Linux static keys](https://cdn.kernel.org/doc/html/latest/staging/static-keys.html)、
[GCC atomic memory models](https://gcc.gnu.org/onlinedocs/gcc/_005f_005fatomic-Builtins.html)、
[GNU linker CET properties](https://sourceware.org/binutils/docs/ld/Options.html)、
[Arm A64 instruction set](https://documentation-service.arm.com/static/6245c734b059dc5ff9a8bdab)。
