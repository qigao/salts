# CMeta 基础层与 tlog 枚举迁移设计

日期：2026-08-20

## 背景

CMeta 计划作为整个应用的编译期元数据基础库。当前 `TurboUtils::Core` 先于
`TurboUtils::CMeta` 构建，`fmt.h` 与 `tlog.h` 又各自维护类型清单或枚举清单，因而
CMeta 还不能作为这些公共头文件的统一事实源。

本设计只处理第一个可验证切片：将现有的单一 CMeta 库提升为 TurboUtils 的基础依赖，
并将 `turbo_log_level_t` 迁移到 CMeta 的 `Enum` 描述。`fmt` 参数模型、字符串错误语义、
日志队列背压和异步内存布局留给后续独立切片。

## 目标

- 保持单一 `TurboUtils::CMeta` 目标，不拆分头文件与运行时目标。
- 让 `TurboUtils::Core` 通过公开依赖复用 CMeta 的头文件和实现。
- 让日志级别的枚举值、显示文本和元数据来自一个声明。
- 保持现有 C/C++ 公开接口、枚举数值和旧解析行为不变。
- 每个行为改动先由失败测试刻画，再实现并运行相邻回归。

## 非目标

- 不在本切片重写 `fmt_arg_t`、格式说明符解析或自定义 formatter。
- 不改变 `turbo_log_level_from_name()` 对未知字符串的 INFO 回退语义。
- 不改变日志队列的阻塞、丢弃或关闭协议。
- 不修改字符串 OOM、无效 view 或增长语义。
- 不引入新的第三方依赖，也不改变 CMeta 的 closed-world 签名策略。

## 分层与依赖

构建依赖保持为两个现有目标：

```text
TurboUtils::Core ------> TurboUtils::CMeta
```

根构建先加入 `cmeta/`，再加入 `utils/`。当前 `cmeta/src/cmeta.c` 只依赖 CMeta 自身
头文件和 C 标准库，不链接 `TurboUtils::Core`，因此不会形成循环依赖。

现有 `turbo_cmeta` 已随 `TurboUtilsTargets` 安装和导出；`TurboUtils::Core` 的 PUBLIC
链接关系会把唯一的 `TurboUtils::CMeta` 目标传递给安装包使用者，不增加新导出目标。

## 日志级别单一事实源

`tlog.h` 使用 CMeta `Enum` 声明以下稳定映射：

| 枚举符号 | 数值 | 显示文本 |
|---|---:|---|
| `TURBO_LOG_LEVEL_DEBUG` | 0 | `DEBUG` |
| `TURBO_LOG_LEVEL_INFO` | 1 | `INFO` |
| `TURBO_LOG_LEVEL_WARN` | 2 | `WARN` |
| `TURBO_LOG_LEVEL_ERROR` | 3 | `ERROR` |
| `TURBO_LOG_LEVEL_FATAL` | 4 | `FATAL` |

生成的 `turbo_log_level_t_meta()` 与 `turbo_log_level_t_to_string()` 成为可用元数据接口。
现有 `turbo_log_level_name()` 继续保留，内部委托给生成接口，并对非法值返回
`"UNKNOWN"`。

`turbo_log_level_from_name()` 必须只匹配显示文本。虽然 CMeta 生成的
`from_string()` 也接受枚举符号，但直接改用它会扩大旧 API 的输入集合；本切片不做
这种公开行为变更。空指针、未知文本和枚举符号仍回退到 INFO。

## 兼容性与 ABI

- 枚举常量显式指定原数值，避免声明顺序变化造成 ABI 漂移。
- `turbo_log_level_t` 类型名和旧函数签名保持不变。
- CMeta 枚举描述符是头文件内静态对象；调用方只能依赖语义内容，不得把描述符地址
  当作跨翻译单元或插件边界的稳定 ID。
- `TurboUtils::Core` 新增对 `TurboUtils::CMeta` 的公开依赖；安装包使用者链接 Core 时会
  获得该传递依赖，无需手工重复声明。
- 本切片不改变 `CMETA_USER_TYPE_LIST`，因此不会改变 `cmeta_sig` 或
  `cmeta_callable` 的 closed-world ABI。

## 错误与状态语义

本切片不新增状态，也不改变日志级别解析的错误模型。旧 API 没有可区分的解析错误，
因此保留 INFO 回退。后续若需要 fail-fast 解析，应新增 checked API，而不是改变旧函数。

## 测试策略

按以下顺序修改和验证：

1. 在 `test_tlog` 中新增枚举数值、元数据数量、显示文本及旧解析兼容性测试；测试应先因
   元数据接口尚不存在而失败。
2. 调整根构建顺序，并让 Core 公开链接现有 CMeta 目标，验证相关目标能配置和编译。
3. 迁移日志级别声明及实现，运行 `test_tlog`。
4. 运行 `test_tlog_cpp`，验证公共 C++ 包装未回归。
5. 运行 `cmeta_core_test` 和 `test_fmt`，验证 CMeta 与相邻格式化路径。
6. 用 CTest 的精确正则复跑上述测试，并记录构建输出。

## 迁移与回滚

该切片不需要数据迁移。若安装导出或编译兼容性失败，可独立回滚 tlog 枚举迁移与
Core 到 CMeta 的依赖；原有日志级别表仍可恢复，不影响日志数据格式。后续 `fmt/str` 改造
必须建立在本切片通过安装树和相邻回归验证之后，不能与本切片绑定提交。

## 后续切片候选

1. `fmt`：先修复动态格式说明符可能造成的 variadic 类型不匹配，再评估用 CMeta schema
   生成参数描述；性能以当前实现为基线。
2. `str`：新增可区分 OOM/无效 view 的 checked API，旧 API 只做兼容包装，避免直接改变
   公开语义。
3. `tlog` 数据路径：为长度计算增加 checked arithmetic，并单独设计队列满、关闭和背压
   协议及 benchmark。
