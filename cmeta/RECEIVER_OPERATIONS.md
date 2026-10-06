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
