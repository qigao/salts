# Salts

Salts 是一个以 C11 为核心、兼容 C++17 的现代 C 基础设施库。它以 CMeta
统一类型与语义，以 CFlow 提供流式计算、Reactive、Actor 与状态机执行模型，并以
CSTL 承载类型化容器和算法，让 C 在保留显式内存、错误与生命周期控制的同时，
拥有更现代的编程模型和接口。

Salts 不是另一套运行时，也不会用隐式分配和无界状态隐藏成本。公开能力强调明确的
所有权、容量、背压、错误传播和跨平台行为。

## 核心能力

| 模块 | CMake target | 职责 |
| --- | --- | --- |
| [CMeta](cmeta/README.md) | `Salts::CMeta` | 类型标识、Enum/Struct 元数据、typed callable、interface、contract 与 range |
| CSerde / [CBind](cbind/README.md) | `Salts::CSerde` / `Salts::CBind` | 格式无关 token 协议与原生 C 数据绑定 |
| [CFlow](cflow/README.md) | `Salts::CFlow` | typed graph、Stream、Reactive、Actor、状态机与可解释/编译执行 |
| [CSTL](cstl/README.md) | `Salts::CSTL` / `Salts::CSTLStream` | C11 类型化容器、算法，以及基于 CFlow 的现代流式 facade |
| Platform / Concurrency | `Salts::Platform` / `Salts::Concurrency` | 跨平台抽象、线程池、Disruptor 与调度基础设施 |
| [Coroutine](coroutine/README.md) / [NativeIO](native-io/README.md) | `Salts::Coroutine` / `Salts::NativeIO` | 有界 coroutine 执行与原生异步 I/O |
| [CNet](cnet/README.md) | `Salts::CNet` | 网络 transport、TLS 与 WebSocket session |
| URI parser | `Salts::UriParser` | CNet 与底层网络能力共享的 URI 词法/结构解析 primitive |
| Core | `Salts::Core` | 字符串、文件、日志、正则、进程、内存与通用工具 |

模块边界、依赖方向和 canonical ownership 详见 [ARCHITECTURE.md](ARCHITECTURE.md)。

CHTTP、S3、CRPC 已迁入独立的 [HTTPServices 仓库](../http-services/README.md)。
消费端先安装 Salts，再构建安装 HTTPServices，增加 `find_package(HTTPServices CONFIG REQUIRED)`；
HTTP/RPC 分别使用 `CHttp::Client`、`CHttp::Server`，独立 S3 客户端使用 `CHttp::S3`；
头文件迁移到各模块公开入口，消费端需要重新编译。
Salts 本身不依赖或构建 HTTPServices。

Crypto、CFlow 文件系统适配器与 CFlow 进程适配器由 SaltsUtils 提供，公开 target 分别为
`Salts::Crypto`、`Salts::FS` 与 `Salts::Process`。Salts 继续拥有它们依赖的
`Salts::Platform`、`Salts::Core`、`Salts::CFlow` 以及底层同步文件系统/进程 API。

QueryVM 与 JSON、XML、YAML、CSV、INI、TLV/LTV、Modbus、SOA、DotEnv、Cmd、TOON、TOML、
DateTime 等格式/协议 parser 也由 SaltsUtils 构建、安装并导出。它们继续使用共享的
`Salts::` target namespace，但不再由 Salts package 导出。`Salts::UriParser` 是唯一保留在
Salts 的 parser target，因为 `Salts::CNet` 直接依赖这一低层 URI primitive；Salts 不依赖
SaltsUtils。

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

安装后的 CMake package 位于 `<prefix>/lib/cmake/Salts`。

## 在 CMake 工程中使用

```cmake
find_package(Salts CONFIG REQUIRED)

target_link_libraries(my_target PRIVATE
  Salts::CMeta
  Salts::CFlow
  Salts::CSTL)
```

## STL 接口

Salts 是 CMake project、package 和导出 namespace：

- 使用 `find_package(Salts CONFIG REQUIRED)`；
- 使用 `Salts::*` targets；
- package metadata 安装到 `lib/cmake/Salts`；
- preset 安装根变量为 `SALTS_ROOT`。

CSTL 是 Salts 内部的容器子系统名称，公开头为 `<cstl/...>` 和聚合头
`<cstl.h>`。`salts_*` / `cstl_*` C 标识符与物理库名保持稳定。

## 延伸阅读

- [CMeta 语言与语义参考](cmeta/LANGUAGE_REFERENCE.md)
- [CFlow 示例](cflow/examples/README.md)
- [CSTL 类型化容器与 Stream](cstl/README.md)
