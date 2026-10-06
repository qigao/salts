# #979 的语义审查与合并边界（#984）

本分支采用独立提交与独立验收单元，不重写已经发布的历史。以下表格定义审查顺序、
依赖和通过条件；网络、runner 与 benchmark 的成功不能替代 CMeta 语义验收。

| 单元 | 契约与主要文件 | 依赖 | 正式验收面 |
|---|---|---|---|
| A：#976 声明基础 | `include/cmeta/{pp,compiler,function,interface,struct}.h` 及声明生成器；有限 PP、编译器能力、精确类型证明 | 无新 runtime authority | `cmeta_pp_*`、`cmeta_lowering_*`、`cmeta_interface_arity_*`、compile-fail、C/C++ installed SDK |
| B：#980/#982/#983 生命周期 | `include/cmeta/{lifecycle,scope,cleanup,object_scope}.h`；static/checked admission、partial rollback、LIFO、move、exception、no-fail discharge | A；ObjectRef 使用 C 的唯一模型 | `cmeta_scope_*`、`cmeta_cleanup_*`、`cmeta_object_scope_*`、`cmeta_pool_cpp_test`、Plugin scope/fatal tests |
| C：receiver 收敛 | `include/cmeta/{operation,function,invokable,object}.h` 及对应 `src/`；FunctionAbi 是唯一 callable 事实源 | A；与 B 的 ObjectRef 适配一起验收 | `cmeta_operation_test`、`cmeta_invokable_test`、`cstl_semantic_projection_test`、`cflow_function_projection_test`、旧 epoch 拒绝 |
| D：#977 Plugin | `plugin/include/salts/plugin*.h`、`plugin/src/`；精确 adapter、manifest/query、跨 TU 聚合与 lease | A、C；scope 适配使用 B | `cmeta_plugin_*`、ELF/Mach-O/COFF linker tests、installed SDK |
| E：#981 可选 native | `cmeta/native/`、`include/cmeta/native*.h`、相关 Platform executable-memory backend | A–D admission 已完成后才可执行 specialization | `cmeta_native_*`、W^X/ABI/失败回滚、独立 benchmark；不作为 B 的前提 |
| F：独立修复 | CNet、NativeIO、runner、benchmark workflow | 各模块自身契约 | 各模块 CTest 与独立 benchmark jobs |
| G：#985 TinyMock 消费者 | `tinymock/`；history/action/return 只拥有行为状态，复用 CMeta 类型和 ownership | A–D；不依赖 E | `cmeta_tinymock_*`、TinyTest 独立链接、C/C++ installed SDK |

表中 `include/` 和 `src/` 默认相对 `cmeta/`。B/C 共享 ObjectRef 和 Reflection epoch，
必须一起满足公开契约，不能为了机械拆分恢复 `receiver_method`、旧布局或兼容 alias。

## 可追踪的提交

- A：`0c72f4ab`、`e516171b`、`ec56fd27`、`f8d3e731`、`bb6ae9b6`。
- B/C：`f102cfc9`（receiver cutover）、`d8cdd1ae`（static/checked）、
  `2e962a33`（admitted capability）、`92b06360`（managed C++ unwind）、
  `d393f7fb`（no-fail discharge qualification）。
- D：`cb7e1efe`（跨 TU aggregation 与 lease scope）、`40ed9743`（typed pointer tests）。
- E：`39d255e1`（native consumer integration）；native target 单独由 configure 开关控制。
- F：`ec58aa4a`（CNet benchmark）、`8d19c98e`（NODELAY）、`490c15b7`（vcpkg runner）、
  `b5ce93f4` / `9b07ffa1` / `a6c8f6ba`（Clang/Darwin 构建修复）。
- G：`dfc8849a`（TinyMock admission、ownership、transactional output）。

`4be93148` 包含跨目录公共前缀切换和 native 工作；它是历史上的整体验收切换，不能按
文件 cherry-pick 后声称得到独立可发布版本。当前 API 只有 `cmeta_*` 一条路径，Reflection
epoch 4 / Plugin epoch 5 均为显式 cutover。旧 epoch 必须拒绝，不做协商或恢复兼容布局。
本表是可审查边界，不把历史混合提交伪称为已经拆开的 stacked PR。

## CI 门禁

`<profile> / semantic` 在 GCC、Linux Clang、MSVC、macOS GCC、AppleClang 上独立运行
A–D/G 的 CTest。它只报告语义测试，不混入 CNet、NativeIO、trace/native specialization
或性能阈值。现有 `native`、`plugin`、`projection`、`execution` 与 benchmark jobs 保留
各自的资格结论；语义绿灯不能被拿来宣称平台或性能全部通过。
测试由所属 CMeta、Plugin、TinyMock 目录标记 `cmeta-semantic`，不按全仓库 `cmeta_*`
前缀猜测归属；CSTL/CFlow consumer 与 TinyTest installed 独立性继续由对应正式测试验收。

普通 PR 的非 fastpath configure 关闭 `CMETA_BUILD_NATIVE_THUNKS`，证明 core 不需要
E。fastpath configure 显式打开已支持平台的 native 后端，独立验证 E。手工 release
准备保留原支持平台的 native SDK 产物。所有 profile 仍完整构建各自 configure graph，
不通过 `--target` 裁剪 CI 构建，也不直接执行测试程序。

每个合并单元需要对应的实际 compiler/平台输出；源码检查、Windows 的通过或 Linux
Clang 的通过都不能替代 macOS Mach-O qualification。远端 Linux 的单项 NativeIO
失败应单独追踪，不能修改 CMeta 语义 gate 来掩盖。

## 回滚与后续拆分

新修复按以上单元提交。撤销 B、C、D 的破坏性切换时必须连同消费者回滚，不能留下
两个 Reflection 模型。E 可通过 configure 关闭而不改变 scope、Data、ObjectRef、Plugin
admission 的语义；删除或改进 E 不得触及 B 的 cleanup authority。

若后续要求真正改写 #979 为 stacked PR，应从共同基线重建并逐个完整构建，保留 B/C
的 cutover 边界，不在共享分支强推重排，也不靠临时旧 ABI 让中间提交假绿。
