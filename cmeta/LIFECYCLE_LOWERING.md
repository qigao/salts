# 静态生命周期与动态 admission（#980）

## 背景与选择

事实：CSTL 类型声明已经同时生成 native storage、concrete construct ops 和 DataDesc，
但原 scope 每次进入仍经 `cmeta_lifecycle_bind()` 检查完整描述符和存储契约。
本阶段将声明期已知的操作入口与外来描述符的 admission 分开，不改变 lifecycle authority。

`cmeta_scope` 使用类型声明提供的 `Type_cmeta_lifecycle(const Type *)`，直接借用
canonical construct ops；`cmeta_scope_checked` 明确通过 `Type_cmeta_data()` 和
`cmeta_lifecycle_bind()` 完整验证。没有按类型名称、callback 地址或验证结果自动选路。

保留所有 scope 的运行时验证不能完成静态入口目标；自动探测 accessor 并回退会隐藏
声明缺失；额外维护另一套生命周期回调会产生两个事实源。因此使用显式静态声明和
checked 入口，共享初始化、回滚、清理展开。

## 声明与证明边界

`CMETA_DEFINE_STATIC_LIFECYCLE(Type, ops)` 只用于拥有 native Type 和 canonical ops
定义的本地声明/generator。它生成带 native type witness 的 accessor，校验 ops 的
精确 const 类型；scope 在编译期检查 accessor 的完整函数类型。ops 必须具有静态存储期，
与同一声明的 DataDesc.construct_ops 指向同一对象。CSTL 从同一生成器输出这个关联。

这些证明覆盖 native 类型和声明关联，不推断回调的行为，也不证明任意手写描述符的
成员值正确。手写声明的所有者必须保证 ops 的 ABI、storage identity/layout、init/restore
契约。外来、Plugin 或运行时选择的描述符不得通过该宏冒充静态声明；使用 checked admission。

当前不为缺少 concrete construct ops 的 Struct 等类型推导生命周期，也不按 C 类型拼写
推断 trivial/no-fail。生命周期分类和进一步移除每项 live/ops 状态由 #980 后续完成。

## 所有权、状态与错误

- 数据单元为一个 native value；scope 独占自动存储，资源总数仍为 1–16。payload 容量由
  既有 provider 契约控制；不增加分配、队列、缓存、registry、线程或 module lease。
- construct ops 是唯一初始化/清理 authority。不可变元数据可共享，value 默认单线程、
  不可并发修改。provider/module 必须活到最后一次清理；scope 不延长其生命期。
- 每项按声明顺序初始化。失败项立即 restore 一次，跳过 body，先前成功项按 LIFO restore；
  未开始的项不清理。body 返回的状态继续传播，清理不覆盖状态。
- move 继续执行既有语义，源恢复 semantic zero；源在 scope 结束时仍可安全清理。
- body 只借用对象到返回为止，不允许保存悬空指针、挂起借用或跨函数 goto。
- checked admission 失败不调用 init/restore，保留明确的 INVALID_ARGUMENT、TRAIT_MISSING
  或 TYPE_MISMATCH；静态声明缺失或 native accessor 类型不符在编译期拒绝。

## 兼容性、验证与回滚

MED：手写类型只提供 `Type_cmeta_data()` 时，原 scope 调用须迁移到 `cmeta_scope_checked`；
本地声明所有者也可在保证上述契约后发布静态 accessor。CSTL 调用语法不变。
不改变 DataDesc/construct ops 的布局、ABI 版本、回调或 native 容器算法。
回滚只需一起恢复 scope 入口和消费者，持久化数据无迁移。

静态 setup/cleanup 为 O(N) 次回调、O(N) 有界自动状态；checked 路径另加原描述符校验。
本阶段验证移除重复 admission，不声明未经测量的吞吐或延迟收益。
正式测试覆盖静态/checked 同一 ops 身份及行为、失败回滚、拒绝错误 accessor、动态坏元数据、
CSTL 资源释放、C++ 头文件和 installed SDK。
