# DataBind 2.0 ABI

DataBind 2.0 使用纯 C schema parser、动态值树和 typed descriptor。运行时不加载
中间代码、不生成机器码，也不要求宿主进程提供编译器。

## 版本边界

- 库版本：`2.0.0`
- C ABI：`8`
- schema codec provider ABI：`tbe_schema_codec_v1_t`
- 旧的运行时 IR、缓存和产物加载接口已删除，不提供兼容层

调用方必须在加载动态库后检查 `data_bind_abi_version()`。ABI 不等于
`DATA_BIND_ABI_VERSION` 时应立即拒绝使用，不能继续解析或释放跨 ABI 对象。

## 对象与所有权

- `DataBind` 持有解析后的 schema AST；`data_bind_free()` 释放它。
- `DataBindValue`、`DataBindObject` 和 `DataBindRecord` 是拥有型结果，不借用
  schema AST；按各自的 `*_free()` API 释放。
- `DataBindFieldView` 等 view 只借用所属 record；record 释放后 view 立即失效。
- 文本和二进制序列化结果必须使用 DataBind 对应的释放函数，不跨 CRT 直接
  `free()`。

## 两条强类型路线

1. schema 生成 `.h/.c`：生成代码公开 owning struct、typed descriptor 和格式
   入口。生成源可编译进静态库或动态库。
2. schema 映射现有 C struct：应用通过 `TBE_TYPED_*` 宏声明 descriptor，
   不生成业务头文件，仍复用相同 DataBind 格式适配器。

两条路线都以 schema 为格式与名称的唯一事实源。`[name]` 和 `[alias]` 决定
外部名称与反序列化别名；`[c]` 决定生成代码的成员名，宏 descriptor 则直接
指定现有 struct 成员。

## 生成动态库

生成头定义 `TBE_GENERATED_API`：

- 构建 Windows DLL 或 ELF shared object 时定义 `TBE_GENERATED_BUILD_SHARED`。
- 使用 Windows DLL 时定义 `TBE_GENERATED_USE_SHARED`。
- 构建或使用静态库时无需定义二者。

生成库仍链接 `TurboUtils::DataBind`；它不包含 C 编译器，也不在运行时编译
schema。

## RulesForge/TurboScript 集成

运行时可直接保存 `DataBind *`，调用动态 `DataBindObject` API；这种方式只部署
schema 和 DataBind 动态/静态库。若需要 `order.id` 一类原生 C 成员访问，则在
RulesForge/TurboScript 的构建或发布阶段生成并编译 schema library，运行时加载
已构建的 library 和 `tbe_schema_codec_v1_t` provider。编译器是构建工具，不是
运行时依赖。
