# TinyTest C11 泛型断言与静态运行时实施计划

**目标：** 删除重复的类型专用断言族，以严格 C11 traits 提供统一比较，并把语言无关
实现迁移到 `tinytest.c` 静态库。

**设计依据：**
`docs/superpowers/specs/2026-08-20-tinytest-cmeta-equality-design.md`

## 已实施

- [x] 用 `tinytest/include/tinymeta/traits.h` 建立内建类型族和关系分派，不依赖 CMeta。
- [x] 删除 typed、array、GTest 兼容、`check_str_*` 与 `check_mem_*` 公开宏。
- [x] 将二参数值比较和三参数内存比较统一到 `check_equal/check_not_equal`。
- [x] 为 C++ 保留模板值语义、字符串内容语义和异常展开。
- [x] 用 `TTEST_USER_EQUAL_TRAIT_LIST` 支持显式注册 C 结构体相等比较。
- [x] 将共享注册表、平台/文件、内部数组、结果和字符串实现迁入 `tinytest.c`。
- [x] 将 `Salts::TinyTest` 改为静态库并保持原有 CMake 导出名。
- [x] 增加 C、C++、自定义 traits 和负向编译测试。

## 最终验证

- [ ] MSVC 全量构建和 CTest。
- [ ] clang-cl 全量构建和 CTest。
- [ ] 直接 C99 负向编译与 C11 `-Werror` 语法检查。
- [ ] 安装目标验证，确认静态库和全部公开头文件被安装。
- [ ] `git diff --check`、旧 API 零引用和工作树范围检查。
