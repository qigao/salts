# Receiver operation 收敛（#976 / #980）

## 背景与选择

旧 `cmeta_receiver_method` 同时存 FunctionDesc 与 FunctionAbi；后者已经指向前者。
这要求消费者维护两份引用并检查一致性，receiver 投影验证还位于 method 模块。
已发现旧投影验证遗漏 result_flags，允许返回值所有权语义漂移（HIGH）。

采用 Function 为唯一 callable 语义事实源：

```text
FunctionDesc（param[0] 显式标记 RECEIVER）
  <- FunctionAbi
  <- receiver_operation { name, abi }
  <- operation_set { receiver_type, operations, operation_count, owner }
  -> ObjectRef 解析 operation
  -> provider 绑定具体 receiver，返回 FunctionData + exact callable
  -> Function 验证 receiver-elided 投影
  -> cmeta_invokable_bind_data
```

保留旧 method 并加 operation 别名会形成永久双路径；在 method 中继续补验证虽可修复
result_flags 漏项，却不能消除重复事实源。因此一次性迁移仓库消费者，移除旧入口。
Interface 的协议方法与 vtable 保持独立，不迁入 operation 索引。

## 契约与状态

Function 拥有 receiver 形状与投影验证。投影仅移除参数零，必须保留结果类型、result_flags、
effects、properties，以及其他参数的名字、类型、flags；投影自身的函数名可以不同。
operation 只拥有查询别名和借用的 FunctionAbi 指针，没有另一份 FunctionDesc。
set 的 receiver_type 与 canonical generic owner 继续做语义比较。

ObjectRef/provider 的 operation 指针要求属于已经选定的准确 set；这里的地址匹配表达
provider capability 成员资格，不能当作跨 TU 类型身份。provider 只绑定具体 object，
不得自行解释或改写投影语义。验证成功后仍通过既有 exact callable 执行。
错误状态保持原有编号：非法输入/集合、receiver 或 owner 不匹配、未找到 operation、
arity/type 不匹配仍可区分。失败不发布新的 invokable。

描述符、set、provider、object 与 callable 捕获均保持原所有权；没有新增分配或隐藏 lease。
不可变元数据可共享，ObjectRef 生命周期与具体 receiver 的同步仍归调用方。
借用必须在 provider/module 释放前结束，Plugin 消费者必须持有外层 lease。
不增加缓存、镜像、注册表或可独立变化的语义状态。

## 算法与兼容性

set 校验含重名检查，N 项为 O(N²) 时间、O(1) 额外空间；查找本体为 O(N)，
公开解析在完整输入校验后进行。Function 投影比较对参数线性扫描，完整验证仍包含参数重名检查。
本轮不把未经验证的外来数据转入静态快速路径，也不宣称性能优化。

这是源代码和 ABI 的有意迁移（HIGH）：头文件由 method.h 改为 operation.h，
receiver_method / object_method 命名改为 receiver_operation / object_operation；
集合及 ObjectRef 的 methods/method_count/method_provider 改为 operations/operation_count/operation_provider。
operation 行移除 function 字段，通过 `operation->abi->function` 访问 canonical FunctionDesc。
CSTL 的 `<Name>_receiver_operation_set()` 从原 canonical schema 生成这些行。

Reflection epoch 从 3 升到 4，Plugin admission epoch 从 4 升到 5。
旧 DSO 在 query/admission 处拒绝，必须与宿主一起重建；不提供旧布局读取或名称别名回退。
没有持久化数据迁移。部署先生成一致的新 SDK/provider/host，再切换整套版本；
回滚也须恢复整套旧 SDK/provider/host，不能混用两个 epoch。

## 验证范围

Function/operation 负例覆盖缺失 receiver、错误返回 ownership、effects/properties、参数类型/flags/名字、
重复别名、receiver/generic owner 不匹配。既有 ObjectRef 与 Invokable 回归保持 provider 精确绑定。
CSTL List/Map/Vec/Set 测试覆盖原操作语义和跨 TU 元数据；C++ 头文件保持 standard-layout。
Plugin 的旧 epoch 拒绝及跨 DSO 生命周期测试验证 admission 边界，installed SDK 测试验证发布入口。

## 生成 receiver/capture/bind（#976: 27–29）

`<cmeta/bind.h>` 的 `FunctionBindDeclAsAbiResult` 把原 Function 参数行标记为 `arg`、
`value` 或 `borrow`。原顺序生成精确函数调用；过滤捕获行后生成普通 FunctionDesc/Abi。
该入口复用现有 `cmeta_callable` 的 inline capture 和 invokable admission，不解析字符串
方法名，不解释原函数 ABI，也不转换 receiver/native 函数指针。

选择单个参数投影模型覆盖 receiver 和普通 bind，避免单独的 method closure 类型。
receiver 使用含 RECEIVER|BORROWED 的 borrow 行；value 仅接受已注册算术标量的 snapshot；
borrow 要求显式 BORROWED object pointer，不允许 consuming capture。所有捕获合计必须
不超过 CMETA_CAPTURE_INLINE，编译期超额失败；没有隐式堆分配、retain/release 或 SBO 降级。
本阶段投影为现有 callable registry 支持的一元/二元签名；零元、多元、未注册签名不受支持。

生成产物为 `name_capture`、`FunctionMeta(name)`、`FunctionAbi(name)` 和
`name_bind(const name_capture *, cmeta_invokable *)`。bind 先检查源 FunctionAbi 与行声明
语义一致，再通过 Function 所有的 `cmeta_function_projection_valid` 验证剩余参数，最终
进入普通 `cmeta_invokable_bind`。类型/flags/名字/ABI carrier、结果 ownership/nullability、
effects/properties 必须原样保留。错误 input 清空输出，metadata mismatch 返回 TYPE_MISMATCH。
源函数必须有 FunctionDecl；native signature 不符、非平凡 value、非 borrowed 指针、过大
capture 在编译期拒绝。显式 sig 与投影不匹配在 admission 拒绝。

捕获 struct 只复制 value 字节或借用指针。borrow 的 object、provider/module 及外层 Plugin
lease 必须活到所有 callable 副本丢弃以后；复制 callable 不延长任何传递资源的寿命。
字段名字是用户原 Function 参数名，thunk 局部参数使用生成的索引名，避免与 out/args 等
框架变量碰撞。调用没有元数据图遍历，使用 admitted 入口时仅检查调用 storage。
投影 admission 线性比较 N 个参数，完整 descriptor 校验包含 O(N²) 重名检查，N ≤ 16。

此入口增量提供，不更改 callable 表示或既有 operation/Interface 语义。编译器若已经有
FunctionData，可用生成的 callable 和 projected metadata 继续走既有 bind_data/provider
桥接。撤回生成声明时恢复手写 exact thunk 即可；没有运行时状态或持久化数据迁移。
