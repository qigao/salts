# Salts Plugin

`Salts::Plugin` is the Salts-owned dynamic-module publication and lifecycle
runtime built directly on canonical CMeta semantics.

Plugin and CFlow are independent capabilities. Plugin owns module publication,
loading, registry, leases and quiescent unload; it does not own graph/execution
semantics and does not link CFlow.

## 显式生成 Plugin 声明

`<salts/plugin_decl.h>` 复用 CMeta 精确 Function 声明和 Interface carrier。
普通反射声明不会自动发布任何 export。完整 C provider 示例：

```c
#include <salts/plugin_decl.h>

FunctionInvokeDecl(value, int, twice, (int, input, CMETA_PARAM_IN));
int twice(int input) { return input * 2; }

#define MATH_EXPORTS(X) \
    X(function, twice, "math.twice", "math", 1u, 1u)

SALTS_PLUGIN_DECLARE(math_plugin, "example.math", (1u,0u,0u),
    MATH_EXPORTS, SALTS_PLUGIN_PASSIVE());
```

以普通 DSO target 编译该源文件并链接 `Salts::PluginABI`。安装 SDK 与 in-tree
使用相同头文件和声明。每个 DSO 在一个 TU 中声明一次 query。

`SALTS_PLUGIN_DECLARE(name,id,version,exports,lifecycle)` 的参数为：本地符号前缀、
Plugin ID、`(major,minor,patch)` 三元组、非空 X-list、生命周期 tuple。
函数行是 `X(function,native_symbol,export_id,contract_id,contract_version,capabilities)`；
Interface 行是 `X(interface,(InterfaceType,&instance),export_id,contract_id,contract_version,capabilities)`。
Interface instance 必须是静态存储期的可变精确 `{self,vtable}` carrier。
同一个 native function 每个列表出现一次；不同导出别名使用显式 wrapper。
ID、版本、capabilities 均为显式声明，不从 Reflection 推断。

无导出的 provider 使用 `SALTS_PLUGIN_DECLARE_EMPTY(name,id,version,lifecycle)`。
它复用同一个 manifest/query 生成器，生成 `exports = NULL`、`export_count = 0`，
不会创建零长数组或占位 export；passive 和 managed provider 都适用。例如独立源文件：

```c
#include <salts/plugin_decl.h>
SALTS_PLUGIN_DECLARE_EMPTY(empty_plugin,"example.empty",(1u,0u,0u),SALTS_PLUGIN_PASSIVE());
```

`SALTS_PLUGIN_PASSIVE()` 将 self 与四个回调设为 NULL。
Managed provider 使用 `SALTS_PLUGIN_LIFECYCLE(&state,start,request_stop,is_quiescent,destroy)`，
四个回调填写具有 `SALTS_PLUGIN_CALL` 和现有精确签名的函数标识符。
生成层检查非零 uint32 contract version、三段 uint32 semantic version（各段允许零）、
非负 uint64 capabilities、Interface carrier 类型、callback 类型与 export 上限；
版本与 capabilities 必须是整数常量表达式，越界、负值和浮点输入均拒绝，避免静默截断。
缺字段、错误行形状或缺失精确 thunk 也在编译期失败。
ID 格式、capability admission、重复 ID、state 指针与其余外来数据约束仍由
`salts_plugin_manifest_validate()` 校验，不能绕过。

函数入口拒绝非 NULL context，使用无 capture 的精确 native thunk；
其存储、错误与所有权契约见 [CMeta 声明说明](../cmeta/LANGUAGE_REFERENCE.md#有限宏与精确调用声明)。
生成的 `salts_plugin_query()` 仅在 ABI 完全相等时返回 const manifest，否则返回 NULL。
ABI epoch、struct_size、lease/generation、STARTED admission、停止、quiescence 与 unload 规则不变。

C 使用静态初始化的 const export 表。C++17 使用显式 CMeta `AsAbi` 声明；
由于 C++17 无法以 designated initializer 选择 union 的非首成员，函数 export 的 const 表项
通过 TU-local 值构造辅助函数初始化。该初始化不注册对象，不改写 manifest，不持有 lease；
不得在其他 TU 的静态初始化期间提前消费此表。
没有 constructor 驱动的注册表、全局可变发现列表或 linker 失败回退。
可选 linker 聚合和 lease RAII 留到后续阶段，本阶段以显式数组为事实源。

正式验证包含 `salts_plugin_declaration_test`、C++ 同源用例、空导出的 passive/managed 用例、
C/C++ 编译失败用例、已有 loader/lifecycle 回归，以及 `cmeta/tests/installed`
内的已安装 SDK 声明测试。

本地复验使用仓库 preset。在 VS x64 开发环境中设置 README 要求的两个 Windows triplet
环境变量后执行以下命令，使用 MSVC：

```powershell
cmake --preset win-dev-user -DBUILD_TESTS=ON -DBUILD_BENCHMARKS=OFF -DBUILD_EXAMPLES=OFF
cmake --build --preset win-dev-user
ctest --preset win-dev-user -R "(^(cmeta_(pp|const|flags|layout|interface)_|salts_plugin_)|^cmeta_(header_cpp|function_(admission|reflection|header)|tinymock_(interface|function_auto_mock|cflow_clock_interface))_test$|^cflow_interface_reflection_test$)" --output-on-failure
```

安装验证先使用 `install-win-release-user` 构建 preset，再令 `CMETA_PACKAGE_ROOT` 指向
该 profile 的安装前缀，在 `cmeta/tests/installed` 中依次执行 configure、build、test
的 `installed-win-release` preset。负例属于 CTest 的正式编译失败测试，预期诊断出现才算通过。

## Targets

```text
Salts::PluginABI
    -> <salts/plugin.h>
    -> Salts::CMeta

Salts::Plugin
    -> Salts::PluginABI
    -> Salts::Core
    -> Salts::Platform
    -> platform dynamic loader
```

`Salts::PluginABI` is publication-only and carries no loader/registry runtime.
Plugin providers can therefore publish CMeta Function/Interface exports without
linking the host runtime.

## One ABI

Plugin supports exactly one current ABI. There is no ABI negotiation, fallback,
readable-prefix compatibility, or retry of older layouts.

Moving package/repository ownership does not change the binary ABI by itself.
A Plugin ABI bump is required when the manifest/export/query binary contract
changes **or when a transitive CMeta Reflection layout published by an export
changes incompatibly**. A host must reject the older Plugin epoch before it
dereferences reflected Function/Interface descriptors.

## Semantic boundary

Plugin publishes canonical CMeta capabilities:

```text
operation/service capability -> CMeta Function + FunctionAbi + exact adapter
stateful provider capability -> CMeta Interface + {self,vtable}
```

It does not define a second function, interface, callable or execution model.

## Lifecycle

```text
load
  -> validate current manifest ABI
  -> LOADED
  -> start
  -> STARTED
  -> acquire/release leases
  -> request_stop
  -> STOPPING
  -> plugin quiescent + leases == 0
  -> QUIESCENT
  -> unload
```

Everything borrowed from a Plugin DSO remains valid only while a live lease
keeps the module loaded.

That rule applies to the complete reachable reflection graph, not only the
top-level export pointer:

```text
Function / Interface export
    -> FunctionDesc / InterfaceDesc
    -> TypeDesc / TypeIdentity
    -> GenericDesc
    -> DataDesc / lifecycle providers when referenced
```

Descriptor copies and semantic-equality matches do not retain the module.
Cross-DSO type/generic comparison uses CMeta semantic identity; descriptor
addresses are never module-independent identity. Consumers must drop every
borrowed descriptor/view and destroy values whose lifecycle callbacks live in
the provider before releasing the final Plugin lease.

Standalone TYPE/DATA/GENERIC exports are not part of the current Plugin ABI.
Reachable Function/Interface descriptor graphs are the canonical publication
path unless a real consumer proves independent type discovery is required.

## CFlow composition

There is no Plugin -> CFlow or CFlow -> Plugin dependency.

An application that projects a Plugin Function into CFlow uses the ordinary
CMeta -> CFlow projection and keeps the Plugin lease alive for the whole
borrowed-code/reflection lifetime. Any helper for this choreography is
consumer/helper-level code, not a PluginCFlow subsystem.
