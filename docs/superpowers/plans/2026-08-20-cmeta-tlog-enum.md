# CMeta tlog Enum Migration Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 保持单一 `Salts::CMeta` 目标，将它提升为 `Salts::Core` 的基础依赖，并以 CMeta `Enum` 作为日志级别的单一事实源。

**Architecture:** 根工程先定义 `Salts::CMeta`，`Salts::Core` 通过 PUBLIC 链接获得其头文件和目标依赖。`tlog.h` 生成枚举及头文件内元数据，`tlog.c` 复用语义元数据但保留旧的文本解析输入集合和 INFO 回退行为。

**Tech Stack:** C11、CMake Presets、CMeta `Enum`、TinyTest、MSVC/Ninja。

---

### Task 1: 用失败测试固定元数据与兼容行为

**Files:**
- Modify: `utils/tests/test_tlog.c:185`

**Step 1: Read the test-writing guidance**

完整读取 `superpowers:test-driven-development` 引用的 `writing-good-tests.md`，再修改测试。

**Step 2: Write the failing test**

在日志级别测试附近新增独立用例，覆盖：

```c
it("should expose log level metadata without changing legacy parsing") {
  const cmeta_enum_desc *meta = salts_log_level_t_meta();

  check_equal((int)meta->count, 5);
  check_equal(SALTS_LOG_LEVEL_DEBUG, 0);
  check_equal(SALTS_LOG_LEVEL_FATAL, 4);
  check_equal(salts_log_level_t_to_string(SALTS_LOG_LEVEL_ERROR), "ERROR");
  check_equal(salts_log_level_name((salts_log_level_t)99), "UNKNOWN");
  check_equal(salts_log_level_from_name("SALTS_LOG_LEVEL_ERROR"),
              SALTS_LOG_LEVEL_INFO);
  check_equal(salts_log_level_from_name(NULL), SALTS_LOG_LEVEL_INFO);
}
```

**Step 3: Run test target to verify RED**

Run:

```powershell
cmd /c "call \"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_tlog"
```

Expected: compilation fails because `salts_log_level_t_meta` and generated helpers do not exist yet.

**Step 4: Commit only the test**

```powershell
git add -- utils/tests/test_tlog.c
git commit --only -m "test: specify tlog level metadata" -- utils/tests/test_tlog.c
```

### Task 2: 建立单一 CMeta 基础依赖并生成日志枚举

**Files:**
- Modify: `CMakeLists.txt:57`
- Modify: `utils/CMakeLists.txt:47`
- Modify: `utils/include/tlog.h:20-46`

**Step 1: Reorder existing targets**

在根 `CMakeLists.txt` 中把 `add_subdirectory(cmeta)` 移到 `add_subdirectory(utils)` 前；不新增 CMeta 子目标。

**Step 2: Export the dependency through Core**

在 `utils/CMakeLists.txt` 的链接声明中添加：

```cmake
target_link_libraries(${TARGET_NAME} PUBLIC Salts::CMeta)
```

保留现有 PRIVATE 依赖及导出名称。

**Step 3: Replace the manual enum declaration**

在 `tlog.h` 引入 `<cmeta/enum.h>`，并用单个声明替换 X-list：

```c
Enum(salts_log_level_t,
     (SALTS_LOG_LEVEL_DEBUG, 0, "DEBUG"),
     (SALTS_LOG_LEVEL_INFO, 1, "INFO"),
     (SALTS_LOG_LEVEL_WARN, 2, "WARN"),
     (SALTS_LOG_LEVEL_ERROR, 3, "ERROR"),
     (SALTS_LOG_LEVEL_FATAL, 4, "FATAL"));
```

**Step 4: Build and run the focused test to verify GREEN**

Run:

```powershell
cmd /c "call \"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --preset win-release-user && cmake --build --preset win-release-user --target test_tlog && ctest --preset win-release-user -R \"^test_tlog$\" --output-on-failure"
```

Expected: configure/build succeeds and `test_tlog` passes while `tlog.c` still uses its compatibility table.

**Step 5: Commit the dependency and declaration change**

```powershell
git add -- CMakeLists.txt utils/CMakeLists.txt utils/include/tlog.h
git commit --only -m "refactor: make cmeta the tlog enum source" -- CMakeLists.txt utils/CMakeLists.txt utils/include/tlog.h
```

### Task 3: 删除 tlog 的重复枚举表

**Files:**
- Modify: `utils/src/tlog.c:66-85`
- Modify: `utils/src/tlog.c:164-172`
- Modify: `utils/src/tlog.c:1690-1712`

**Step 1: Replace validation with metadata lookup**

```c
static int log_level_is_valid(salts_log_level_t level) {
  return cmeta_enum_item_by_value(salts_log_level_t_meta(), (int64_t)level) != NULL;
}
```

**Step 2: Replace display-name lookup**

`salts_log_level_name()` 调用生成的 `salts_log_level_t_to_string()`，并只在返回 NULL 时返回 `"UNKNOWN"`。

**Step 3: Preserve text-only legacy parsing**

`salts_log_level_from_name()` 遍历 `salts_log_level_t_meta()->items`，只比较 `item->text`；不得调用同时接受 symbol 的 `salts_log_level_t_from_string()`。

**Step 4: Remove the duplicated table and run the focused test**

删除 `SALTS_LOG_LEVEL_TABLE_ITEMS`、`salts_log_level_entry_t`、静态表和表长度宏。

Run:

```powershell
cmd /c "call \"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_tlog && ctest --preset win-release-user -R \"^test_tlog$\" --output-on-failure"
```

Expected: build succeeds and `test_tlog` passes.

**Step 5: Commit the implementation refactor**

```powershell
git add -- utils/src/tlog.c
git commit --only -m "refactor: reuse cmeta log level metadata" -- utils/src/tlog.c
```

### Task 4: 验证 C/C++、CMeta 与安装导出边界

**Files:**
- Verify: `utils/tests/test_tlog_cpp.cpp`
- Verify: `cmeta/tests/cmeta_core_test.c`
- Verify: `utils/tests/test_fmt.c`
- Verify: generated `build/Msvc-Release/SaltsTargets.cmake`

**Step 1: Build adjacent targets**

```powershell
cmd /c "call \"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target test_tlog test_tlog_cpp cmeta_core_test test_fmt"
```

Expected: all four targets build successfully.

**Step 2: Run exact adjacent regression set**

```powershell
cmd /c "call \"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && ctest --preset win-release-user -R \"^(test_tlog|test_tlog_cpp|cmeta_core_test|test_fmt)$\" --output-on-failure"
```

Expected: 4/4 tests pass.

**Step 3: Verify exported dependency**

Run:

```powershell
rg.exe -n "Salts::CMeta" build\Msvc-Release\SaltsTargets.cmake
```

Expected: exported Core target contains `Salts::CMeta` in its interface link libraries.

**Step 4: Check the patch boundary**

```powershell
git diff --check
git status --short
```

Expected: no whitespace errors; unrelated pre-existing worktree changes remain untouched.
