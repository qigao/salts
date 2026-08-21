# CMeta 驱动的 Utils 元数据设计

日期：2026-08-21

## 背景

TurboUtils 正在把 CMeta 作为整个应用可复用的基础元数据层。目前已有两个试点：

- `fmt.h` 使用 `Schema/Replay` 生成 `fmt_type_t`、`fmt_arg_t` union、构造器及 C/C++ 类型检测。
- `tlog.h` 使用 `Enum(...)` 声明 `turbo_log_level_t`，日志实现通过枚举元数据完成控制面校验和名称转换。

这两个试点已经证明 CMeta 可以减少机械重复，但应用仍无法查询 `fmt_type_t` 和 `turbo_log_entry_t` 的只读结构信息。与此同时，CMeta 的若干生成式头文件分别维护 unused/inline 属性，并且 `struct.h` 直接使用 C11 `_Alignof`，不适合作为 C/C++ 公共头文件的统一实现基础。

本设计把 fmt/tlog 作为第一个完整的 Utils 元数据试点。它只扩展描述能力，不改变格式化、日志传递、字符串所有权或 sink 调度算法。

## 目标

1. 让 `fmt_type_t` 的稳定存储类型表同时生成 ABI tag、union、构造器和公开只读枚举元数据；源语言检测继续由独立策略 Schema 映射到这些构造器。
2. 为 `turbo_log_entry_t` 提供公开只读结构元数据。
3. 统一 CMeta 头文件生成代码的 inline、unused 和 C/C++ alignof 基础宏。
4. 保持现有公开 API、ABI、错误语义、所有权、日志输出及热路径行为。
5. 以 C、C++、多翻译单元、功能测试、完整回归和性能基线证明兼容性。

## 非目标

- 不修改 `fmt.c` 的格式解析、specifier 校验或渲染算法。
- 不修改 `tlog.c` 的异步队列、内存池、sink、过滤、轮转或关闭协议。
- 不把 tlog sink vtable 迁移为 `interface/implements`。
- 不为 `tstr` 或 `vstr` 增加反射、range 或新的字符串算法。
- 不提供通过元数据写入日志条目的接口。
- 不把 Utils 类型注册到 `cmeta/src/cmeta.c` 的进程级类型注册表。
- 不承诺不同翻译单元中的描述符地址相等。

## 方案比较

### 方案一：Schema-first 公共描述符（采用）

保留 fmt 的命名 Schema 作为单一事实源，并使用 CMeta `Struct(...)` 表达现有日志条目。描述符由公共头文件生成，保持 TU-local；应用通过 typed helper 或通用 CMeta 查询函数读取内容。

优点：消除机械重复、保持依赖方向、容易验证 ABI，且不影响运行时算法。

代价：公共头文件会生成少量只读 TU-local 数据和 inline helper，需要测量编译时间及对象体积。

### 方案二：直接将所有类型改写为 `Enum/Struct`

`turbo_log_entry_t` 适合 `Struct(...)`，但 `fmt_arg_t` 包含 tag、union、构造器及多组语言检测策略，单独使用 `Enum(...)` 无法让这些产物共享同一事实源，反而会保留两份类型表。

### 方案三：注册到 CMeta runtime registry

该方案可以提供进程级描述符地址，但会让 CMeta runtime 知道 Utils 类型，形成错误的反向依赖，并扩大初始化与链接边界，因此拒绝。

## 架构与依赖边界

依赖方向保持：

```text
CMeta preprocessor / metadata primitives
        ↓
TurboUtils fmt/tlog declarations
        ↓
TurboUtils runtime algorithms
        ↓
application / plugin / diagnostics consumers
```

CMeta 只生成声明、只读描述符和薄 typed helper。`fmt.c` 和 `tlog.c` 继续拥有所有解析、格式化、异步传递和 sink 算法。元数据查询不会隐式执行日志操作，也不会推进任何运行时状态。

`TurboUtils::Core` 已公开依赖 `TurboUtils::CMeta`；本设计不新增目标、不拆分 CMeta，也不改变安装或导出目标名称。

## CMeta 基础宏

`cmeta/pp.h` 提供统一且带 `CMETA_` 前缀的生成式头文件基础宏：

- `CMETA_UNUSED`：GCC/Clang 使用 `__attribute__((unused))`，其他编译器为空。
- `CMETA_INLINE`：`static inline CMETA_UNUSED`。
- `CMETA_LOCAL`：`static CMETA_UNUSED`。
- `CMETA_ALIGNOF(type)`：C++ 使用 `alignof(type)`，C 使用 `_Alignof(type)`。

`enum.h`、`struct.h`、`interface.h` 和 `container.h` 复用这些宏。现有模块私有的等价宏删除，避免生成式公共头文件在严格 `-Werror` 消费者中产生未使用函数告警。

这些宏只控制 linkage、诊断和跨语言拼写，不改变函数签名、描述符内容或对象布局。

## fmt 类型元数据

### 单一事实源

现有 `FMT_DETAIL_TYPE_SCHEMA` 增加稳定文本字段。每个可格式化存储类型的行包含：

```text
tag, explicit_value, text, C storage type, union member, constructor
```

同一 Schema 生成：

- `fmt_type_t` 的显式数值；
- `fmt_arg_t.val` 的 union member；
- `fmt_arg_*` 构造器；
- `cmeta_enum_item_desc` 表。

数值 `1..14`、union member 名和构造器名保持现状。`FMT_TYPE_NONE = 0` 是无 payload 的 sentinel，作为一条显式元数据项加入描述符，但不生成 union member 或构造器。

源语言检测继续与存储 Schema 分开：C/C++ 检测 Schema 只映射到上述构造器，`char`、`short`、`float` 等多个源类型可以收敛到同一个存储表示；C++ 的 `c_str()`、`data()+size()`、enum 和 ADL 策略继续留在语言适配层。

### 公开只读 API

新增与 CMeta `Enum(...)` 一致的 typed helper：

```c
const cmeta_enum_desc *fmt_type_t_meta(void);
const char *fmt_type_t_to_string(fmt_type_t value);
const char *fmt_type_t_to_symbol(fmt_type_t value);
bool fmt_type_t_from_string(const char *text, fmt_type_t *out);
```

这些 helper 是 header-local inline。无效值返回 `NULL`；解析参数无效或文本不存在时返回 `false`，并且不修改调用方输出。描述符及其 items 为只读 TU-local 数据。

## 日志条目元数据

现有 `turbo_log_entry_t` 字段按原顺序放入 CMeta `Struct(...)`：

```text
level, timestamp_ms, thread_id, component, file, line, message, message_len
```

生成的公开入口为：

```c
const cmeta_struct_desc *turbo_log_entry_t_meta(void);
```

描述符公开字段名、类型文本、offset、size 和 align。字段顺序、字段类型、结构 size/align 及 sink callback 签名不变。为结构增加 C tag 属于源代码层面的兼容扩展，不改变对象布局。

`component`、`file` 和 `message` 仍是 borrowed 指针。描述符不拥有这些字符串，也不延长它们的生命周期。sink 只能在既有 callback 生命周期内读取条目；需要保存时仍必须复制。

## 数据流

日志热路径保持不变：

```text
TLOG_* / TURBO_LOG_*
  → FMT_ARGS
  → turbo_log_typed
  → fmt_print
  → async_entry_create（复制 borrowed payload）
  → queue
  → sinks
```

`FMT_ARG(x)` 仍只在调用点完成编译期类型选择并构造现有 `fmt_arg_t`。`fmt_print()` 不读取新描述符。`turbo_log_typed()` 和 sink 分发也不读取日志条目结构元数据。

应用、插件或诊断代码必须显式调用 `*_meta()` 才会访问描述符，因此控制面元数据不会进入数据面。

## 错误、状态与所有权语义

- 不新增分配、锁、函数指针、fallback 或全局可变状态。
- fmt 的无效输入、specifier 不兼容、截断和返回值语义保持现状。
- 日志级别无效、logger 配置失败、队列满、分配失败及 dropped counter 语义保持现状。
- 元数据解析失败采用 CMeta 现有 `false`/`NULL` 语义，且不改变输出。
- 同一语义声明可在不同 TU 生成不同描述符地址。调用方比较名称、值、字段、size/align 等内容，不比较地址作为类型身份。

## ABI 与兼容性

必须保持：

- `FMT_TYPE_NONE == 0`，既有 fmt tag 继续为 `1..14`。
- `fmt_arg_t.type`、union member、结构 size/align 和所有 member offset。
- `turbo_log_level_t` 的 `0..4` 数值及既有文本。
- `turbo_log_entry_t` 字段顺序、类型、size/align 和 member offset。
- `FMT_ARG`、`FMT_ARGS`、`FMT_WRAP_0..8`、`fmt`、`TLOG_*` 和 `TURBO_LOG_*` 的源兼容入口。
- `fmt_print`、`turbo_log_typed`、sink callback 等导出符号及 ABI。

新增元数据 helper 是 source-level additive API，不新增动态库导出符号。旧调用方无需修改即可重新编译。

## 性能与资源边界

元数据不进入 fmt/tlog 数据面，因此预期运行时复杂度、分配次数和调度路径不变。该结论必须由测量验证，不能仅依据代码结构宣称。

回归门槛：

- fmt/tlog benchmark 延迟增加不得超过 10%。
- fmt/tlog benchmark 吞吐下降不得超过 10%。
- 代表性 C/C++ 消费者的头文件编译时间中位数增加不得超过 10%。
- Release 对象或二进制大小增加超过 20% 必须说明来源并回退或取得明确接受。

若首次测量越过门槛，使用更多轮次排除冷启动和系统噪声；确认回归后停止扩展，不以理论上的内联或 dead stripping 代替证据。

## 测试设计

### TDD RED

先增加会在当前代码上失败的测试：

- fmt C 测试查询 15 个 item（含 NONE），验证名称、symbol、显式数值及解析失败不修改输出。
- fmt C++ 测试包含同一公共头并查询相同语义。
- tlog C 测试查询 8 个字段，验证字段名、类型文本、offset、size 和 align。
- tlog C++ 测试包含并调用 `turbo_log_entry_t_meta()`，证明 `Struct` 生成面跨语言可用。

### ABI 锁定

测试内定义只用于比较的 legacy mirror，并用静态断言比较：

- `sizeof` 和 align；
- 每个公开字段的 `offsetof`；
- fmt tag 和日志级别的显式数值。

mirror 不进入生产头文件，不形成第二个运行时事实源。

### 多翻译单元

至少两个 C TU 和一个 C++ TU 包含公共头。测试按描述符内容比较，不要求元数据地址相等。

### 回归矩阵

最小相关测试：

- `cmeta_core_test`
- `test_fmt`
- `test_fmt_cpp`
- `test_tlog`
- `test_tlog_cpp`

性能候选：

- `test_fmt_bench`
- `bench_tlog`

最终验证：MSVC Release 完整构建、完整 CTest，以及 Clang C11/C++ 严格告警语法编译。

## 实施顺序

1. 记录 C/C++ 头文件编译、fmt/tlog benchmark 和二进制大小基线。
2. 添加元数据与 ABI RED 测试并确认失败原因。
3. 统一 CMeta 基础生成宏，先验证 CMeta 自身。
4. 生成 fmt 类型元数据，验证 fmt C/C++。
5. 生成日志条目元数据，验证 tlog C/C++。
6. 运行相邻回归、性能对比和完整构建测试。
7. 扫描重复宏和未预期的公开 API/ABI 变化。

## 脏工作区策略

当前 CMeta、fmt、tlog 及测试文件已有用户暂存改动。实施时必须：

- 逐文件阅读 index 与 working-tree 差异；
- 使用小范围 `apply_patch`；
- 只运行路径限定的 diff/check；
- 不 reset、checkout、覆盖或重排既有改动；
- 实现提交由用户决定，不把重叠的既有改动擅自提交。

## 回滚方案

本改动不迁移持久化数据，也不改变动态库导出 ABI。若兼容、编译或性能验证失败，可按以下顺序回滚：

1. 删除 Utils 新增的元数据 helper 和测试；
2. 恢复 `turbo_log_entry_t` 的显式 typedef；
3. 保留或单独回滚 CMeta 基础宏统一；
4. 重新运行原有 fmt/tlog/CMeta 测试确认恢复。

由于运行时算法未改，回滚不需要数据转换、配置迁移或状态补偿。
