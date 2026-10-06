# CMeta 分层与机制归属（#957）

## 决策与影响

事实：原 `Salts::CMeta` 编译 static-key、fault consume 和三组 acquire-load 汇编；`meta.h` 自动包含 fastpath、trace 和 structured scope。这使纯 Reflection 消费与可选执行机制共用一个入口。

选择单向分层：CMeta 保存 canonical metadata、identity 与生命周期语义；Platform 保存原子 key、一次性 fault 许可和架构 acquire-load；`Salts::CMetaFastpath` 是显式选择的 metadata/type facade。相比仅从 aggregate header 删除 include，迁移机制才能解除 CMeta archive 对架构选项的关联。相比建立第二套 CMeta backend，迁移保留一个事实源和已有原子发布协议。

`Salts::CMeta` 不链接 Platform/Concurrency/Coroutine/Core/Plugin，不编译 fastpath 汇编；`meta.h` 只聚合 core header。Structured scope 由 `<cmeta/scope.h>` 显式引入。Typed static-call 和 typed tracepoint 由 `<cmeta/fastpath.h>` / `<cmeta/trace.h>` 显式引入，并链接 `Salts::CMetaFastpath`，其依赖方向为 `CMetaFastpath -> CMeta + Platform`。

兼容性（HIGH）：#957 要求删除错误归属的 spelling，不保留转发别名。通用 `cmeta_static_key*`、branch/enable/disable 和 `cmeta_fault_*` 迁移至 `<salts/fastpath.h>` 的 `salts_*` API；机制返回 `SALTS_OK` / `SALTS_EINVAL`，typed static-call 与 tracepoint 仍返回 CMeta 状态。Native 开关迁移为 `SALTS_NATIVE_FASTPATH`，默认 OFF。现有 source consumer 须更新 include、link target、option 和状态检查；descriptor 布局、manifest 格式、provider lifetime、key 的 acquire/release 与一次性消费语义不变。

状态仍由 point owner 持有的单个 atomic key 和 typed atomic target 保存；迁移不复制状态、不加载模块、不保留 lease。初始化/销毁要求 quiescent，替换/关闭不 drain 已开始的 callback。迁移本身只发生在编译/链接时，不涉及运行期数据迁移。回滚须成套恢复调用点及 owner，不能引入两个 namespace 同时运行的兼容层。

验证涵盖 Platform 单独的 C/C++ key/fault 测试、CMeta typed fastpath/trace、native 五平台、portable TSan、pure Reflection aggregate、正式安装包消费与相邻 metadata/生命周期回归。Manifest 的 validated typed view 只解释 canonical metadata，保持不分配、不调用对象回调、不保留 provider 的边界。

## 每个公开 header 的分类

| 层 | Header |
|---|---|
| Core：基础类型、身份、构造 kernel | `abi.h`, `cmeta.h`, `types.h`, `type_identity.h`, `type_select.h`, `type_traits.h`, `status.h`, `pp.h`, `signatures.h`, `generated/builtin_signature_manifest.h` |
| Core：canonical metadata 与生命周期 | `data.h`, `data_select.h`, `declared_type.h`, `enum.h`, `flags.h`, `struct.h`, `variant.h`, `lifecycle.h`, `function.h`, `interface.h`, `method.h`, `object.h`, `object_interface.h`, `manifest.h`, `manifest_view.h`, `fingerprint.h` |
| Core：typed carrier、projection 与协议 | `collector.h`, `compute.h`, `container.h`, `contract.h`, `entry.h`, `fixed_array.h`, `generic.h`, `infer.h`, `invokable.h`, `policy.h`, `range.h`, `relations.h`, `value.h`, `vector.h` |
| Core aggregate | `meta.h` |
| Structured-C | `scope.h`；`struct.h` 内的 intrusive projection 只增加静态 owner/member/type 检查，不拥有容器运行期 |
| Optional metadata/type facade | `atomic.h`, `rcu.h`, `local.h`, `pool.h`, `fastpath.h`, `trace.h` |

## 可选 facade 的 owner

| Facade | CMeta 增加的语义 | 机制 owner 与边界 |
|---|---|---|
| Atomic | finite generic exact type construction | C11 atomic；memory-order validator 的归属仍由 #957 后续收窄，不进入 core aggregate |
| RCU | typed value/guard/reclaim projection | `Salts::Concurrency`；不实现回收算法、不销毁 provider 对象 |
| Local | DataDesc 生命周期 binding | `Salts::Platform` thread identity；TLS 不运行构造/析构，owner 在退出前显式销毁 |
| Pool | typed DataDesc 初始化/清理与借用 | Core `object_pool`；分配、lease/thread policy 的进一步分离仍由 #957 跟踪 |
| Static call | FunctionAbi exact contract validation | `Salts::Platform` key/atomic/native acquire-load；无 JIT、patch、runtime Reflection 查找 |
| Trace | exact typed payload 与 canonical Struct metadata | `Salts::Platform` gate；backend 生命周期及同步归调用方 |

`likely/unlikely` 与 generic compiler/bit/array-count helpers 不新增 CMeta spelling。CMeta 不选择调度、线程放置、分配增长、模块保留、重试、阻塞或进程终止策略。未完成的 owner 拆分不视为 #957 已验收；安装包与新的 manifest view 也不替代 #926 后续完整 fingerprint/Plugin 验收。

## 安装包的正式 Reflection 验证

`tests/installed` 是 #957 验收要求的正式 TinyTest 集成测试。它只通过安装后的 `SaltsConfig.cmake` 与 public include 消费 `Salts::CMeta`，另链接测试框架 `Salts::TinyTest`；不链接 Core、Platform、Concurrency、Coroutine、CFlow 或 Plugin。C11/C++17 用例实际查询 canonical 类型、struct layout、Function ABI 与 manifest view 和 canonical contract fingerprint，并在编译时拒绝 core aggregate 引入可选 fastpath/trace/scope。

在 VS x64 开发环境中，根目录配置 `win-release-user` 后：

```powershell
cmake --build --preset install-win-release-user --parallel 4
$env:CMETA_PACKAGE_ROOT = "$env:PROJECT_ROOT/external/pkgs/salts/release"
cd cmeta/tests/installed
cmake --preset installed-win-release
cmake --build --preset installed-win-release --parallel 4
ctest --preset installed-win-release
```

Fixture 使用版本化 preset、vcpkg manifest 和现有 CMake/TinyTest helper。Linux CI 使用 `installed-linux-release`；其 `CMETA_PACKAGE_ROOT` 必须指向本次安装的 package，find_package 禁用默认路径以避免意外消费其他 checkout。测试不在 CMake 中维护功能断言。
