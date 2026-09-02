# Rocida

Rocida 是一个以 C11 为核心、兼容 C++17 的现代 C 基础设施库。它以 CMeta
统一类型与语义，以 CFlow 提供流式计算、Reactive、Actor 与状态机执行模型，并以
Rocida STL 承载类型化容器和算法，让 C 在保留显式内存、错误与生命周期控制的同时，
拥有更现代的编程模型和接口。

Rocida 不是另一套运行时，也不会用隐式分配和无界状态隐藏成本。公开能力强调明确的
所有权、容量、背压、错误传播和跨平台行为。

## 核心能力

| 模块 | CMake target | 职责 |
| --- | --- | --- |
| [CMeta](cmeta/README.md) | `Rocida::CMeta` | 类型标识、Enum/Struct 元数据、typed callable、interface、contract 与 range |
| CSerde / [CBind](cbind/README.md) | `Rocida::CSerde` / `Rocida::CBind` | 格式无关 token 协议与原生 C 数据绑定 |
| [CFlow](cflow/README.md) | `Rocida::CFlow` | typed graph、Stream、Reactive、Actor、状态机与可解释/编译执行 |
| [Rocida STL](turbostl/README.md) | `Rocida::STL` / `Rocida::STLStream` | C11 类型化容器、算法，以及基于 CFlow 的现代流式 facade |
| Platform / Concurrency | `Rocida::Platform` / `Rocida::Concurrency` | 跨平台抽象、线程池、Disruptor 与调度基础设施 |
| [Coroutine](coroutine/README.md) / [NativeIO](native-io/README.md) | `Rocida::Coroutine` / `Rocida::NativeIO` | 有界 coroutine 执行与原生异步 I/O |
| [CNet](cnet/README.md) / [CHTTP](chttp/README.md) / [CRPC](crpc/README.md) | `Rocida::CNet` / `Rocida::CHTTP` / `Rocida::CRPC` | 网络 transport、HTTP 与 RPC |
| Parser engines | `Rocida::JsonParser`、`Rocida::XmlParser`、`Rocida::QueryVM` 等 | JSON、YAML、TOML、XML、CSV、URI、协议解析与查询执行 |
| Core | `Rocida::Core` | 字符串、文件、日志、正则、进程、内存与通用工具 |

模块边界、依赖方向和 canonical ownership 详见 [ARCHITECTURE.md](ARCHITECTURE.md)。

## 构建与测试

最低要求为 CMake 3.20、支持 C11/C++17 的编译器，以及 vcpkg。仓库 preset 使用
`VCPKG_ROOT` 定位 vcpkg，并使用 `PROJECT_ROOT` 推导统一的构建产物与安装根目录。

Windows Release：

```powershell
cmake --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user
cmake --build --preset install-win-release-user
```

Linux Release：

```sh
cmake --preset linux-release-user
cmake --build --preset linux-release-user
ctest --preset linux-release-user
cmake --build --preset install-linux-release-user
```

安装后的 CMake package 位于 `<prefix>/lib/cmake/Rocida`。可用以下 target 验证一份
干净安装能否被独立工程消费：

```sh
cmake --build --preset win-release-user --target verify_installed_package
```

## 在 CMake 工程中使用

```cmake
find_package(Rocida CONFIG REQUIRED)

target_link_libraries(my_target PRIVATE
  Rocida::CMeta
  Rocida::CFlow
  Rocida::STL)
```

完整的安装包消费工程和各 target 的头文件验证见
[tests/install_consumer](tests/install_consumer)。

## STL 接口

Rocida 是 CMake project、package 和导出 namespace：

- 使用 `find_package(Rocida CONFIG REQUIRED)`；
- 使用 `Rocida::*` targets；
- package metadata 安装到 `lib/cmake/Rocida`；
- preset 安装根变量为 `ROCIDA_ROOT`。

Rocida STL 是 Rocida 内部的容器子系统名称，公开头为 `<rocida/stl/...>` 和聚合头
`<rocida/stl.h>`。`turbo_*` / `turbostl_*` C 标识符与物理库名保持稳定。

## 延伸阅读

- [CMeta 语言与语义参考](cmeta/LANGUAGE_REFERENCE.md)
- [CFlow 示例](cflow/examples/README.md)
- [Rocida STL 类型化容器与 Stream](turbostl/README.md)
- [安装包验证工程](tests/install_consumer)
