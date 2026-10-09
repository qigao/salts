<!-- Migrated from qigao/salts-utils @ c259f4c6fabc8c5d4c9663c44c5cb738519f5c30 (Unicode 17.0.0). -->
# Salts Unicode

`Salts::Unicode` 是一个独立的 C11 库：用 re2c 生成的 UTF-8 DFA 顺序读取 Unicode scalar，
并返回固定 Unicode 17.0.0 版本的 `XID_Start`、`XID_Continue` 与 `White_Space` 属性。
它不包含 Jinja 或 Mustache 语义，也不替代 Salts 的 `tstr` / `vstr` 字符串能力。

`salts_unicode_decimal_value(uint32_t scalar, uint32_t *out_value)` 查询Unicode 17的
十进制数字（Nd），成功返回`SALTS_UNICODE_OK`并写入0–9；非Nd返回`SALTS_UNICODE_NO_MATCH`。
NULL输出、surrogate或超出U+10FFFF返回`SALTS_UNICODE_ERR_INVALID_ARGUMENT`，所有非成功路径
均保持输出不变。接口不分配内存、不访问共享可变状态，可并发调用；不改变已有scalar布局。
例如U+0662得到2，而上标²与汉字〇不是Nd。

实现复用 re2c 4.6 的 Unicode 17 分类表，无另一份数字值映射。
在连续Nd区间中按偏移模10取值，支持相邻多组数字；时间O(L)、空间O(1)，固定数据的L最多50。
官方UnicodeData.txt全码点对照验证770个Nd数字、非数字和非法scalar状态。
数据语义参见[Unicode 17数字章节](https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-22/)。

## 使用

```c
#include <salts_unicode.h>

#include <stdio.h>

int main(void) {
  const char text[] = "用户";
  vstr input = vstr_from_buf(text, sizeof(text) - 1u);
  salts_unicode_scalar scalar;
  size_t cursor = 0u;

  while (salts_unicode_utf8_next(input, &cursor, &scalar) == SALTS_UNICODE_OK) {
    printf("U+%04X at byte %zu, XID_Start=%u\n", (unsigned)scalar.value,
           scalar.byte_offset,
           (scalar.properties & SALTS_UNICODE_PROPERTY_XID_START) != 0u);
  }
  return 0;
}
```

常用的零分配字符语义操作直接接受 `vstr`：

```c
vstr trimmed;
size_t identifier_end;
int is_identifier;

salts_unicode_trim_whitespace(input, &trimmed);
salts_unicode_xid_span(trimmed, 0u, &identifier_end);
salts_unicode_is_xid(trimmed, &is_identifier);
```

`salts_unicode_trim_whitespace` 返回借用的子 view，并验证完整输入；
`salts_unicode_xid_span` 只实现 Unicode XID 规则，不把 `_` 等语言扩展算作起始字符；
`salts_unicode_is_xid` 即使已经发现非 XID 字符，也会继续验证余下 UTF-8。空串不是 XID。
`SALTS_UNICODE_NO_MATCH`、`SALTS_UNICODE_END` 与负错误码可明确区分。

```cmake
find_package(Salts CONFIG REQUIRED)
target_link_libraries(app PRIVATE Salts::Unicode)
```

输入是 caller-owned borrowed `vstr`；返回的子 view 使用期间，源存储不得修改或释放。函数不保留 view、无堆分配、
无全局可变状态；不同线程可并发扫描不同或相同的 immutable input。每次调用最多复制 4 bytes
到固定栈 buffer，以隔离 re2c DFA 的最长 lookahead。单 scalar 查询时间与额外空间均为 O(1)；
trim/XID 整体操作按输入 bytes 为 O(n) 时间、O(1) 额外空间。

成功时 cursor 前进一个 scalar；到达末尾返回 `SALTS_UNICODE_END`。参数错误或非法 UTF-8
返回负错误码，cursor 与 output 均保持不变。支持 embedded NUL；拒绝 overlong、surrogate、
超出 U+10FFFF、非法 continuation 与截断序列。

## 构建依赖与数据版本

Unicode 17 的 build-time re2c 属性事实源固定为上游 re2c 4.6 的
`unicode_properties.re` / `unicode_categories.re`。构建配置使用规范化 LF
后的 SHA-256 验证两个数据文件（分别为 `56b5f16e…2aa21` 和
`3d0a3198…5b0689`）。re2c 工具继续来自最新版构建工具包，不固定软件包
版本；如果未来工具携带 Unicode 18 等不同分类表，将 **fail fast**，
必须显式升级 Unicode 数据、ABI 版本和官方 conformance 测试，绝不静默漂移。


构建需要 re2c 4.6 或更新版本，以及它安装的 `unicode_properties.re`。使用 re2c 4.6
基于 Unicode 17.0.0 生成的数据；不接受随日期变化的 `latest`：

```powershell
cmd /d /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --preset win-release-user'
```

`CMakeUserPresets.json` 的 Windows profile 通过 `RE2C_ROOT` 统一指定 re2c 4.6 安装根，
并从同一根目录查找可执行文件与 Unicode 数据。清理缓存后也能重现配置。其他机器需在 user preset
配置实际工具根，并先进入本机 VS 环境。

数据来源：

- [Unicode 17.0.0](https://www.unicode.org/versions/Unicode17.0.0/)
- [DerivedCoreProperties.txt](https://www.unicode.org/Public/17.0.0/ucd/DerivedCoreProperties.txt)
- [PropList.txt](https://www.unicode.org/Public/17.0.0/ucd/PropList.txt)
- [Unicode Terms of Use](https://www.unicode.org/copyright.html)
- [re2c 4.6 unicode_properties.re](https://github.com/skvadrik/re2c/blob/4.6/include/unicode_properties.re)

升级 Unicode 数据必须显式修改公开 version macros、预期 property-file hash、边界测试和兼容
说明；只替换本地 re2c 数据文件会 fail fast。
