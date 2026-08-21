# TinyTest C11 泛型断言与静态运行时设计

## 目标

TinyTest 只支持 C11 及以上 C 编译模式，不依赖 CMeta。断言 API 以少量泛型名称覆盖
内建标量、字符串、指针、内存和显式注册的用户值类型；语言无关的运行时能力由
`TurboUtils::TinyTest` 静态库提供，避免继续把实现堆在 `tinytest.h`。

## 接口

- `check_equal(actual, expected)` / `check_not_equal(actual, expected)`：值比较。
- `check_equal(actual, expected, len)` / `check_not_equal(actual, expected, len)`：字节比较。
- `check_greater`、`check_greater_equal`、`check_less`、`check_less_equal`：有序数值比较。
- `check_within(actual, expected, margin)`：显式容差比较。
- `check_contains`、`check_starts_with`、`check_ends_with`：字符串或 C++ 容器操作。
- 每个断言均有对应 `_warn` 非致命形式。

删除旧的按类型断言、array 断言、`check_str_*`、`check_mem_*` 和 GTest 风格兼容别名；
不提供 pre-C11 fallback。

## C11 traits

`tinytest/traits.h` 是 TinyTest 自己的编译期事实源。`TTEST_EQ`、`TTEST_NE`、
`TTEST_GT`、`TTEST_GE`、`TTEST_LT`、`TTEST_LE` 表示关系，有限的类型族 handler
处理 signed、unsigned、float、double、long double、C string 和 pointer。

用户值类型通过以下 schema 注册：

```c
#define TTEST_USER_EQUAL_TRAIT_LIST \
    , (POINT, Point, point_equal)
#include "tinytest.h"
```

每行是 `(TOKEN, C_TYPE, COMPARATOR)`；比较器语义为
`bool(const C_TYPE *, const C_TYPE *)`，只借用参数。历史文件名
`tinytest_cmeta.h` 由 `tinytest.h` 在 `traits.h` 之后单向包含；它不是独立入口，且不包含或链接 CMeta。
未注册结构体必须编译失败，不能退回 `memcmp`；整数默认 handler 仅用于兼容 MSVC
无法按底层整数类型匹配 enum 的 `_Generic` 行为，结构体无法转换为该 handler 的参数。

## 静态库边界

`tinytest.c` 实现共享 spec 注册表、计时、临时文件/目录、递归清理、内部动态数组、
结果分类和字符串辅助函数。`tinytest_internal.h` 只保存该静态库与公开头共享的最小
ABI 类型和声明，不保存函数实现，也不是用户入口。公开头文件保留 runner 宏必须
直接访问的完整状态类型、断言宏和依赖语言语义的 runner 薄层。

C++ 断言失败必须通过异常展开以运行局部对象析构；C 使用 `longjmp`。因此这部分不能
无差别编入单个 C 对象。MSVC 下 C/C++ 对 TLS 声明的处理也不同，活动 config 与当前
spec 函数继续使用头文件中的 select-any TLS 定义；共享 spec 链表由静态库唯一拥有。

## 构建与兼容性

`tinytest` CMake target 是 `STATIC`，安装导出名仍为 `TurboUtils::TinyTest`。所有使用者
必须链接该 target；直接只包含头文件再调用运行时 API 不再受支持。这是有意的构建
兼容性破坏。安装内容包括静态库以及 `tinytest.h`、`tinytest.hpp`、`traits.h`、
`tinytest_cmeta.h`、传递依赖 `tinytest_internal.h` 和 `tinymock.h`。

## 验收

- C11 内建类型、字符串、指针、内存三参数分派均通过运行测试。
- C++ 内容字符串比较、内存三参数分派和失败时析构均通过。
- 用户 traits 的相等、警告、单次求值和未注册类型负向编译探针通过。
- pre-C11 翻译单元因明确诊断而编译失败。
- MSVC 与 clang-cl 完成相关构建和 CTest；未执行的平台不得声明为运行通过。
