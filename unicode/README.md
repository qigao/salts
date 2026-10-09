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

## #1088 迁移与发布边界

本节是 [#1088](https://github.com/qigao/salts/issues/1088) 的迁移约束。
M1 的源码迁入不等于已发布 SDK，也不提供 NFC 或 IDNA；这些后续能力见
[#1091](https://github.com/qigao/salts/issues/1091)。各阶段均不是 Salts 2.3 RC 的前置条件。

上游事实源为 SaltsUtils `c259f4c6fabc8c5d4c9663c44c5cb738519f5c30` 的 `unicode/`，
原树为 `6e040ea59aa60e2b02f984ffa6a9399daac28cf9`，包含 56 个文件。
迁移保留公开符号、结构体布局、借用生命周期、原有算法与数据；构建和安装归属改为
`SaltsTargets`。SaltsUtils/Jinja 切换后只消费安装的 `Salts::Unicode`，删除旧实现和导出，
不建立长期 shim 或反向依赖。

| Salts SDK | SaltsUtils SDK | 配对结果 |
| --- | --- | --- |
| 迁移前 | 迁移前 | Unicode 由 SaltsUtils 导出 |
| 迁移后 | 迁移后 | Unicode 仅由 Salts 导出 |
| 迁移后 | 迁移前 | 两个 export set 都定义 `Salts::Unicode`，不能作为支持的组合 |
| 迁移前 | 迁移后 | 缺少必须的 Unicode target，配置失败 |

**HIGH / 推论**：顺序发布两个浮动 latest 包会暴露上述混合组合；缩短发布间隔不能消除
这个窗口。稳定晋级前须确定包分发能否让消费者选择完整的一对版本。若不能，应先安排
受控消费方的维护窗口：暂停新 restore，等待在途 restore 完成，发布完整 SDK 对，刷新
latest 解析并完成跨平台安装测试后再恢复。无法协调的外部消费者仍存在兼容风险，不能
将其描述成原子升级。维护窗口及真正的发布操作需要单独授权，本迁移不改变分发系统。

发布顺序为：两个 PR 的待发布提交完成审查和平台验证；保留不可变候选 SDK 对及其来源；
让 Jinja、安装的 C11/C++ 消费者和 stun/flexUI 验证这一对制品；执行已确定的协调晋级；
最后在全新安装前缀中重新解析 latest 并运行正式消费测试。候选记录应包含两库和外部
消费者的提交、run/attempt、RID、配置、工具链、制品身份及实际测试范围。源码分支
集成 CI 的成功只能证明其记录的构建，不能代替全平台候选制品验收或授权稳定发布。

回滚以 SDK 对为单位。尚未晋级时保留旧发布对；晋级后暂停 restore，按分发系统允许的
方式恢复已验收的完整配对，不能只降级 Salts。使用新安装前缀，避免旧 SaltsUtils 的
Unicode header/library 残留。不得覆写不可变包、添加 EXACT pin、静默 fallback，或以
目标存在检查绕开双所有者冲突。

### 安装消费测试

原有 `test_salts_unicode_installed_sdk` 保留独立 C11/C++17 编译和运行用例。父构建
生成只用于本 build tree 的配置输入，传递实际编译器、配置、运行库、平台参数和已解析
的编译/链接选项；vcpkg 继续使用父工程的 manifest 与工具链。消费工程使用版本化的
`installed-sdk` configure/build/test preset，只从测试指定的 `SALTS_ROOT` 查找 SDK。
它不重新选择 Release、不把第一方包放入 `CMAKE_PREFIX_PATH`，也不通过读取导出文本
证明链接正确。

测试将当前构建按所选配置安装到 build tree 内的专属前缀，避免改写共享安装目录。
Debug/Release 分别使用独立前缀和缓存；CTest 仍执行真正的 C/C++ 用例。CI 的原有
Unicode/完整测试步骤已包含此测试，安装打包步骤不重复创建另一个消费 build。
Android/iOS 仅在构建阶段验证，不将宿主执行结果当作移动端运行证据。

## #1091 NFC / IDNA 设计提案（尚未实现）

以下为实现前的契约提案，不增加公开头文件或可调用占位接口；公开 API/ABI 仍需在
M3 中审查。NFC 归 `Salts::Unicode`；IDNA 的独立 target 只依赖 Unicode 和 Core。
复用 scalar/property 查询和不可变数据，不能在 IDNA 中复制另一套 Unicode 数据所有者。
CNet 的调用边界接入属于 M4，本次迁移不改变 resolver 的实际行为。

### 固定处理规则

首版明确请求 Unicode 17.0.0 和 UTS #46 revision 35，版本不支持时失败，不能随着
宿主库或包更新默默切换。基础扫描仍不执行隐式规范化；NFC 不等于 NFKC、case folding
或 locale case conversion。NFC 需要规范分解、稳定的 combining-class 重排、受阻组合、
composition exclusions 和 Hangul 规则，数据来源固定为 Unicode 17 UCD。

M3 需要补齐固定版本的 `DerivedNormalizationProps.txt` / composition exclusions、
IDNA mapping、Joining_Type 及 Script 等实际规则输入。现有 UnicodeData/Bidi 数据
不能替代全部这些属性；新增源文件与生成器必须记录官方 URL、版本、哈希和许可，
生成结果可重现并只读，不依赖宿主 Python/ICU 的 Unicode 版本。

ToASCII 首版提议固定为 nontransitional，`UseSTD3ASCIIRules`、`CheckHyphens`、
`CheckBidi`、`CheckJoiners`、`VerifyDnsLength` 全部开启，`IgnoreInvalidPunycode` 关闭。
ContextO 是另加的接纳规则，需独立错误分类与测试；这不构成完整的严格 IDNA2008
接纳声明。若另需严格 IDNA2008，须单独审查它对 UTS #46 有效输入的进一步限制。

处理顺序为严格 UTF-8 解码、UTS #46 mapping、NFC、按映射后的点分标签、A-label
解码及标签校验、Punycode 编码和 DNS 长度校验，全部成功后提交输出。已有 `xn--`
输入必须验证，解码后不重新 mapping/NFC 来修复非法标签。Bidi 是否启用标签约束取决于
整个解码后域名，不能只检查当前 RTL 标签。当前 Bidi_Class 查询可复用，但
`salts_unicode_bidi_paragraph_level` 只实现 UAX #9 P2/P3，不能代替 RFC 5893。

为使首版严格长度策略明确，提议拒绝空域名、根域名、内部空标签及尾点；不偷偷删除
尾点改变绝对域名语义。如需支持带尾点 FQDN，须作为独立的显式策略审查和测试。
长度在 ASCII 编码完成后按字节校验：标签 1–63，所选无尾点格式的域名 1–253。
IPv4/IPv6 literal、URI authority、端口和 percent-decoding 由应用边界单独处理，
ToASCII 不接收完整 URL。通用 NFC 保留合法 NUL scalar，域名接口明确拒绝 embedded NUL。

### 所有权、容量和错误

- 输入是调用期间不可变的显式长度 `vstr`，不保留、不异步使用；输出和 workspace
  均由调用者唯一持有，输入、输出及 workspace 不得重叠。
- 首版采用调用者提供的有界 workspace，不隐藏堆分配。API 设计需同时给出输入字节
  预算、workspace 字节容量和输出容量；mapping/分解扩展及 Punycode 乘加必须检查
  溢出。DNS 输出长度上限不能替代中间存储预算，资源超限与域名非法分别报告。
- 先在 workspace 中完成验证和候选输出，再一次性写入 destination。任意错误保持
  destination 与成功长度输出不变；workspace 内容不保证保留。成功的 ToASCII 输出
  以 NUL 终止，报告长度不含 NUL，容量必须容纳 `length + 1`；不截断、不发布部分结果。
- 错误至少区分参数、版本、UTF-8、mapping/禁止字符、标签/A-label、Bidi、ContextJ、
  ContextO、DNS 长度、workspace/输出容量及算术溢出。确定失败即返回，调用者不得
  使用失败输出发起 DNS。错误定位必须区分原始字节位置与规范化后标签索引；不能把
  后者冒充源输入 offset。无需承诺返回官方测试文件的所有细分状态码。
- 数据只读；不同线程可并发处理独立 workspace/output，输入可共享只读借用。
  不建立全局可变转换 context，不增加隐藏锁、队列、缓存或初始化/关闭流程。

### 实现与验收顺序

先完成 NFC 和官方 `NormalizationTest.txt`，再实现版本化 mapping、Punycode 及标签
规则；最后完成事务式输出和安装 ABI 测试。`IdnaTestV2.txt` 按 nontransitional 列和
空字段继承规则解析，分别报告基准算法覆盖、资源限制及 ContextO 等附加接纳规则；
文件本身不覆盖 ContextO，不能用它证明 ContextO 已通过。

正式测试覆盖非法 UTF-8、NUL、组合顺序/Hangul、四种标签分隔符、sharp-s、joiner、
伪 A-label、跨标签 Bidi、ContextO 正反例、空/尾点、标签 63/64 字节、域名 253/254
字节，以及 workspace/output 恰限和不足时输出不变。fuzz 与 ASan/UBSan 覆盖实际
算法库；只给外部测试 executable 加 sanitizer 不能声称整个 SDK 已插桩。
Windows/Linux/macOS 执行 C11/C++ 安装测试，Android/iOS 单独报告交叉构建覆盖。

M4 仅在应用边界成功转换后使用同一个 ASCII 身份进行 DNS、TLS hostname 验证及
上层 authority 处理；保留 numeric-IP 的独立路径。显示用原始 Unicode 与连接身份
分离，UTS #46 接纳不能作为视觉反欺骗结论。M3 未验收前不启用新的转换路径，也不
新增 ICU fallback。回滚 M3/M4 时一起撤回调用点和对应导出，不保留只声明的接口。

标准和测试事实源：
[UAX #15 revision 57](https://www.unicode.org/reports/tr15/tr15-57.html)、
[UTS #46 revision 35](https://www.unicode.org/reports/tr46/tr46-35.html)、
[RFC 5893](https://www.rfc-editor.org/rfc/rfc5893.html)、
[RFC 5892 Appendix A](https://www.rfc-editor.org/rfc/rfc5892.html#appendix-A)、
[Unicode 17 IDNA tests](https://www.unicode.org/Public/17.0.0/idna/IdnaTestV2.txt)。
