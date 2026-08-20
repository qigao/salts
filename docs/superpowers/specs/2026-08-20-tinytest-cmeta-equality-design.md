# TinyTest CMeta Equality Design

## 目标

新增可选头文件 `tinytest_cmeta.h`，让严格 C11 用户可以继续使用
`check_equal(actual, expected)` 比较显式注册了 CMeta equality trait 的结构体，
同时保持 `tinytest.h` 的现有独立使用方式与内建类型比较语义不变。

## 已选方案

采用编译期 trait 策略表与薄适配器：

```c
typedef struct Point {
    int x;
    int y;
} Point;

static bool point_equal(const Point *actual, const Point *expected) {
    return actual->x == expected->x && actual->y == expected->y;
}

#define CMETA_USER_EQUAL_TRAIT_LIST \
    , (POINT, Point, point_equal)
#include "tinytest_cmeta.h"
```

注册行字段为 `(TOKEN, C_TYPE, COMPARATOR)`。`TOKEN` 是生成内部符号所用的唯一
C 标识符；`COMPARATOR` 必须具有 `bool(const C_TYPE *, const C_TYPE *)` 语义。
比较器只借用两个参数，不能保存指针，也不取得对象所有权。

选择该方案的原因：

- 相比向 `cmeta_type_desc` 增加函数指针，它不改变 CMeta ABI。
- 相比 `memcmp`，它不会把 padding、指针地址或浮点位表示误当成值语义。
- 相比显式 `check_equal_as(Type, ...)`，它保留 TinyTest 已有双参数调用形式。
- trait 是有限的编译期事实源，不需要全局运行时注册表或初始化顺序。

未选择的方案：

- 修改 `cmeta_type_desc`：会改变公开结构布局与所有描述符初始化点，迁移范围过大。
- 自动 `memcmp`：结构体 padding 可能未初始化，且无法表达深比较和领域相等语义。
- 在调用点传入类型或比较器：实现简单，但破坏 `check_equal(actual, expected)` 的目标接口。

## 组件边界

### `cmeta/include/cmeta/traits.h`

定义 equality trait 的编译期 schema、擦除后的只读描述符和类型安全分派助手。
该头文件保持 header-only，不要求修改或链接新的运行时注册表。内建 CMeta 类型具有
标准值相等 trait；用户 trait 由 `CMETA_USER_EQUAL_TRAIT_LIST` 追加。

公开契约包括：

- `cmeta_equal_trait`：类型名、大小、对齐与擦除比较函数；
- `cmeta_equal(actual, expected)`：对已注册类型进行一次求值的类型安全比较；
- `cmeta_equal_trait_of(value)`：取得对应的只读 trait；
- `cmeta_equal_values(trait, actual, expected)`：擦除边界的空指针检查与调用。

未注册类型不提供默认比较。调用 `cmeta_equal` 或经适配后的 `check_equal` 时，编译器
必须报错，不能静默退回整数转换或字节比较。

### `tinytest/tinytest.h`

仅把现有 C11 `check_equal` 与 `check_equal_warn` 的 `_Generic` 内建关联提取为内部
可复用选择宏。直接包含 `tinytest.h` 的源码继续得到完全相同的内建类型处理器、
浮点容差、字符串内容比较和指针地址比较。

### `tinytest/tinytest_cmeta.h`

先包含 `tinytest.h` 与 `cmeta/traits.h`，再为用户 equality trait 生成 TinyTest
处理器，并重定义 `check_equal`/`check_equal_warn` 的分派。自定义值按值传入生成的
短处理器，处理器将局部副本地址借给 comparator，因此每个宏实参只在函数实参位置
求值一次。

比较失败时报告类型名，例如 `expected values of type Point to be equal`。适配器不负责
格式化任意结构体字段，避免引入第二套 formatter trait。`check_not_equal` 不在本次范围。

该适配器只支持 C11；C++ 用户继续使用 `tinytest.hpp`，不会经过 `_Generic` 分派。

## 数据流与错误语义

1. 预处理器从 `CMETA_USER_EQUAL_TRAIT_LIST` 生成每种类型的 typed/erased adapter。
2. `_Generic` 只检查 `actual` 的类型，不求值。
3. 选中的 typed adapter 接收 `actual` 与 `expected` 各一次。
4. adapter 将两者地址传给 trait comparator。
5. `true` 进入 TinyTest 成功路径；`false` 进入现有 fatal 或 warn 断言路径。

`actual` 和 `expected` 类型不兼容时由生成函数的参数类型产生编译错误。擦除 API 收到
NULL trait 或 NULL value 时返回 `false`，不解引用无效指针。

## 构建、安装与兼容性

- `tinytest_cmeta.h` 随 TinyTest 头文件安装。
- `traits.h` 已由 CMeta 的 `include/cmeta/*.h` 安装规则覆盖。
- 使用适配器的构建同时消费 `TurboUtils::TinyTest` 与 `TurboUtils::CMeta` 的 include
  interface；不新增第三方依赖。
- 不修改 `cmeta_type_desc`、`cmeta_callable`、TinyTest 公开函数或现有宏的直接行为。
- `CMETA_USER_EQUAL_TRAIT_LIST` 必须在首次包含 `cmeta/traits.h` 之前定义；同一翻译单元
  内它是唯一 trait 事实源。

## 测试与验收

新增 TinyTest/CMeta 集成测试，至少验证：

- 两个相等的注册结构体通过 `check_equal` 与 `check_equal_warn`；
- 不相等结构体在 `it_should_fail` 中触发预期失败；
- 带副作用的 actual/expected producer 各调用一次；
- `cmeta_equal` 与擦除 trait API 返回正确结果，并拒绝 NULL；
- TinyTest 原有 C11 generic 测试继续通过；
- 严格 C17 警告配置下 GCC/Clang/MSVC 可编译相关目标。

未注册结构体的编译失败通过独立负向编译探针验证，期望失败原因是不存在可用的
`_Generic` association，而不是链接错误。
