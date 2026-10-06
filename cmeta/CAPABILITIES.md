# 类型能力、variant 与 flags

这些入口属于 CMeta 的有限代码生成层，可从 `cmeta/meta.h` 引入，或分别
包含 `type_traits.h`、`struct.h`、`variant.h`、`flags.h`。声明生成普通 C
类型、静态元数据和具体 inline 函数；生命周期操作复用现有 DataDesc。

## 声明与编译期要求

`cmeta_traits(Name, rows...)` 的每行可写成
`cmeta_trait(Hashable, hash_callback)`，也可继续使用 `(hash, hash_callback)`。
行之间用逗号分隔。能力标志和回调槽位由同一组行生成，重复能力和错误
回调签名在编译阶段被拒绝。

| 能力 | 既有行标签 | 回调签名 |
| --- | --- | --- |
| Equal | equal | `bool(const void *, const void *)` |
| Hashable | hash | `uint64_t(const void *)` |
| Comparable | compare | `int(const void *, const void *)` |
| Copyable | copy | `bool(void *, const void *)` |
| Movable | move | `void(void *, void *)` |
| Destructible | destroy | `void(void *)` |

`cmeta_has_trait(Name, Capability)` 是编译期常量；
`cmeta_require_trait(Name, Capability)` 在能力未声明时产生静态断言失败。
查询对象必须是本 TU 可见的 trait 声明或生成的 variant/flags 类型名。
它证明声明包含该能力，不能证明回调实现正确或运行时 provider 可用。
运行时接纳仍使用 `cmeta_type_require_traits` 和 canonical DataDesc 检查。
这些入口没有自动生成序列化、allocator 或 plugin 行为。

`cmeta_require_field(Owner, member, Type)` 检查原生字段的精确类型，保留
const/volatile 和数组 typedef 的长度。不存在的字段或类型不符均不能编译。
C11 使用 `_Generic`，要求字段可取址，因此不支持 bitfield；C++17 使用
`std::is_same` 检查成员声明类型。两者均不求值对象表达式，也不读取
Reflection 元数据。

## Tagged variant

`cmeta_variant(Name, "stable.Name", cases...)` 接受 1–16 行
`cmeta_case(Case, tag, PayloadType, &payload_data)`。这些行连续书写，行之间
不加逗号。tag 必须是非零、能用 int 表示的常量，且在同一声明内唯一。
显式 tag 保证调整声明顺序不会改变 tag；0 保留给空状态。

ID 必须是非空字符串字面量；payload DataDesc 指针必须能用于静态初始化，
生命周期至少覆盖生成元数据。provider 必须与 PayloadType 的实际 C 类型
兼容。接纳检查验证 native size/alignment 及 canonical copy/move/destroy
能力；相同 size/alignment 本身不能证明两个 C 类型语义相同。
provider 接纳失败表示尚未建立可用对象，不能随后调用其生命周期操作；
应先修正声明或 provider。

| 生成入口 | 参数、结果及失败语义 |
| --- | --- |
| `Name_init(out)` | out 为原始存储或空对象；建立空状态并验证 provider。NULL 返回 INVALID_ARGUMENT，布局不符返回 TYPE_MISMATCH，能力不足返回 TRAIT_MISSING。不得重新初始化仍持有资源的对象。 |
| `Name_validate()` | 不接触对象，验证声明的所有 provider 和自身 DataDesc。 |
| `Name_select(out, tag)` | out 必须已初始化且为空；选定并初始化 payload。0、未知 tag、NULL、非空目的对象被拒绝。初始化失败会释放部分 payload 并恢复空状态。 |
| `Name_copy_Case(out, source)` / `Name_move_Case(out, source)` | 接纳该类型的 payload 指针；目的对象必须为空。拒绝 NULL 和 source 指向目的 payload 的别名。失败恢复目的为空；成功 move 将源 payload 恢复语义零态。 |
| `Name_copy(out, source)` / `Name_move(out, source)` | 接纳同一 variant 类型。目的对象必须为空，拒绝自操作。copy 保留源对象；成功 move 将源 variant 置空；空值可复制/移动。 |
| `Name_get_Case(value)` / `Name_mut_Case(value)` | 返回对应类型的借用指针；NULL 或 tag 不匹配返回 NULL。 |
| `Name_destroy(value)` | 释放活动 payload 并置空；对空对象重复调用安全。未知活动 tag 或违反 canonical 无失败清理契约会 fail fast。 |
| `Name_cmeta_data()` | 返回不可变 canonical DataDesc；包含 tag、case 名称、稳定 ID、原生 offset 和 payload provider。 |

`cmeta_match(pointer)` 展开为普通 switch。分支使用 `case Name_Case:`，
读取 payload 时调用 checked accessor。pointer 必须非 NULL；是否处理空值
和所有 case 由调用者的普通 C 控制流决定。

Copy/move/destroy 调用现有 `cmeta_data_value_*`，不维护第二份生命周期
状态。traits 构造回调可接纳原始目的存储；生成 provider 的 construct ops
先写空 tag，避免把未初始化 tag 当作活动资源。状态归属只有对象自己的
tag/union：建立 payload 时先记录活动 tag，使失败清理能找到正确 provider；
清理结束后 tag 归零。provider 的无失败 move/restore 回调仍须满足既有契约。

## Typed flags

`cmeta_flags(Name, "stable.Name", rows...)` 接受 1–16 行连续书写的
`cmeta_flag(Symbol, uint64_bits, "display text")`。生成的 Name 是独立
struct 类型，具备 uint64_t bits 字段；命名值 `Name_Symbol` 具有同一类型，
因此不同 flags 类型不能直接混用。支持最高位、零值、别名和组合 mask。

| 生成入口 | 参数、结果及失败语义 |
| --- | --- |
| `Name_meta()` | 返回 canonical unsigned64 FLAGS domain，供 CLI、binding 或诊断消费者读取名称与位值。 |
| `Name_valid(value)` | 仅当没有 declared_mask 以外的位时返回 true。 |
| `Name_from_bits(bits, out)` | 校验 uint64_t bits，成功写出 typed value；未知位或 NULL 返回 INVALID_ARGUMENT，保留原输出。 |
| `Name_or(left, right, out)` | 校验两份 typed value 后合并；非法值或 NULL 拒绝且保留输出。 |
| `Name_contains(value, mask)` | 两者合法且 value 包含 mask 的全部位时返回 true。空 mask 可包含于任何合法值。 |
| `Name_cmeta_data()` | 返回 canonical ENUM DataDesc，copy/move/destroy 接入既有 enum-bits 生命周期。 |

所有名称、位值、declared_mask 和 DataDesc 都从同一组声明行派生；消费者
负责格式转换，CMeta 不引入 CLI/config 或序列化依赖。

## 所有权、边界与兼容性

数据单元是一个 inline variant 或 flags 值。默认 single-threaded/single-owner；
跨线程传递需要所有权转移或调用方同步。accessor 是借用：mutation、move、
destroy 后失效，不能未经来源保证跨协程挂起保存。没有队列、背压或关闭线程；
所有者结束生命周期时必须 destroy 活动 variant。

容量固定为一个 tag 加最大 payload union 及原生 alignment/padding，精确预算
使用 `sizeof(Name)`。wrapper 不分配内存；payload provider 负责自己的容量
上限、checked arithmetic、分配失败和资源观测。直接 accessor/flags 检查是
O(1)；provider 接纳遍历有限声明，canonical 生命周期成本包含 payload 自身
操作和现有 descriptor 校验。嵌套生命周期沿用 canonical 递归深度边界。

现有 descriptor 布局和 tagged trait 行保持兼容。trait 回调签名现在在编译期
检查，不匹配的旧声明需要修正；C++17 声明使用 constexpr 初始化，不要求
C++20 designated initializer。旧 variant provider 没有 construct ops 时保留
其既有 zero-storage 契约；显式提供但非法的 construct ops 立即报错。

不同 TU 的静态元数据地址可以不同，身份比较使用 `cmeta_type_equal` /
`cmeta_data_desc_equal`。跨 TU 必须复用同一声明头文件和稳定 ID，不以指针
地址或仅 size/alignment 代替类型身份。此次扩展不改变既有对象格式；新 variant
的内存布局是原生 C 布局，不能直接当作跨平台 wire format。

## 可复验示例

完整声明见 [capabilities fixture](tests/cmeta_capabilities_fixture.h)。
[C11 示例及所有权测试](tests/cmeta_capabilities_test.c) 包含 trait 回调、字段
要求、普通 switch、typed flags、跨 TU 身份、深复制和失败清理；
[C++17 示例](tests/cmeta_capabilities_header_cpp_test.cpp) 是可运行 main，复用
同一声明头文件。[测试 payload provider](tests/cmeta_capabilities_owned.c)
提供有 32-byte 硬上限的 owned buffer 及可注入失败的 canonical 生命周期。

使用当前平台 user preset 构建 `cmeta_capabilities_test` 和
`cmeta_capabilities_header_cpp_test`，然后运行
`ctest --preset <user-preset> --output-on-failure -R '^cmeta_capabilities'`。
这同时运行公开示例和 [编译失败资格测试](tests/compile_fail/)。
CI 通过 GCC、GCC ASan、MSVC、ClangCL、AppleClang 的既有 presets 执行相同
正式测试，并回归 core/data/enum-bits/C++ aggregate headers。
