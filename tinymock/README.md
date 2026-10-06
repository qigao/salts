# TinyMock：CMeta admission 与行为状态（#985）

TinyTest 只提供 libc 测试运行时；TinyMock 是可选的 CMeta 消费者，拥有 history、captor、
output action 和 scripted return。FunctionDesc/FunctionAbi、Data lifecycle、ObjectRef
与 receiver operation 均以 CMeta 为事实源，不复制类型注册表或接管 Plugin lease。

## 所有权与 admission 协议

- 默认单线程；init/reset/destroy 要求调用已经停止。元数据不可变，外层 provider/Plugin
  lease 必须晚于全部 mock 状态、captor、结果及回调借用释放。描述符地址不是语义身份。
- checked API 验证外来描述符；admitted API 只消费成功初始化且未修改的状态，不能接受
  手工伪造的 binding。它是调用方遵守契约的已验证借用记录，不是不可伪造的安全能力；
  不加 cookie/magic，也不隐式保活。生成 wrapper 在 reset/init 时 admission，调用时复用结果。
- history 有 `TINYMOCk_MAX_CALLS` 个槽，每槽最多 `TINYMOCk_MAX_ARGS` 个快照；达到上限
  保留原槽并只累计调用数。单值存储有 `TINYMOCk_MAX_VALUE_BYTES` 上限，超限或 OOM
  返回 false，不截断。borrowed view 在 reset/destroy/替换后失效。
- VALUE 使用 canonical type/Data lifecycle；BORROWED 仅记录明确的指针身份，不拥有
  pointee。UNKNOWN 不授权 scripted ownership；声明者必须补齐 result flags。
- OWNED 通过 canonical ObjectRef 显式转移，成功写出一次后清空脚本；第二次写出失败。
  SHARED 仅使用 ObjectRef 的 retain/release authority；retain 失败不得发布结果。
- 清理先 discharge 值的 canonical obligation，再释放 TinyMock 的本地存储；移动只转移
  一次义务。没有隐藏 retain、retry、allocator 替换或第二套 destroy 回调。
- OUT/INOUT 在修改 destination 前准备全部结果；准备失败释放临时值且保持 destination。
  managed replacement 必须有 canonical no-fail move，不能 destroy 后再执行 fallible copy。

MED：TinyMock 的公开状态布局随此次功能更新，需要库与消费者一起重编译。UNKNOWN 结果
脚本须迁移到 `FunctionDeclResult` / `FunctionDeclAsAbiResult` 或 Interface `FR` 行。
原精确函数 ABI、Interface dispatch 和 TinyTest API 保持不变。

验证使用正式 history/actions/return、generated Function/Interface、Plugin lease、C++ guard
及 installed-SDK CTest；不以 descriptor 指针相等、源码文本检查或独立类型注册表证明行为。

## 入口与迁移

`tinymock_cmeta_*_init/reset(state, function)` admission 借用的 immutable FunctionDesc；
非法 descriptor 不发布可调用状态。`history_record`、`actions_apply`、`return_write` 保留
外来 function 参数的完整检查，语义比较使用 `cmeta_function_desc_equal`。生成的精确
Function/Interface wrapper 调用对应 `_admitted` 入口，不重新接收外来描述符。

`tinymock_cmeta_return_set` 复制 VALUE 或 BORROWED 脚本；失败保留旧脚本。
`tinymock_cmeta_return_set_object(state, abi, view, object)` 要求精确 object-pointer ABI、
matching pointee Data 类型和 OWNED/SHARED ObjectRef。`view.address` 指向真实 native
pointer 变量，`view.object_pointer_identity` 必须是该变量指向的同一个对象，不得伪造。
OWNED 成功后清空源 ObjectRef；SHARED 成功后新增一份引用。失败不转移所有权。
`return_write[_admitted]` 的 state 参数可变；OWNED 写出后 caller 承担一次 destroy，
SHARED 每次成功写出后 caller 承担一次 release，authority 仍是原 ObjectRef lifecycle。

`tinymock_cmeta_return_take_data(state, function, binding, source)` 是显式的一次性 VALUE
脚本；binding 来自 `cmeta_lifecycle_admit`，必须有 move。初始化目标失败会恢复目标并
保留脚本，成功写出后清理 moved-from slot。普通 TypeDesc traits 路径不会猜测 DataDesc，
Data 路径不会降级为按字节复制。直接 value slot 的 `take_data` 使用相同协议。

所有 bool 入口失败均须检查。元数据无效、ownership 不明、类型/ABI 不匹配、缺少
copy/move authority、存储超限、OOM 或 provider copy/retain 失败均返回 false。
状态公开用于自动存储，不代表可复制/篡改 owning state；重入修改同一状态不受支持。

## 依赖审查结论

- `tinytest` 的公开头、runtime 和 target 无 CMeta 引用；installed equality/C++ 测试只链接
  `Salts::TinyTest`。`tinymock` target 才链接 CMeta，不增加 TinyMeta 兼容层。
- TinyMock 没有 `receiver_method` 存储或按名称调用层；receiver mock 保留 FunctionAbi
  的完整接收者参数、effects、properties 和 result flags。Interface 使用原 typed vtable。
- history/captor 的 pointer snapshot 不 retain pointee；共享结果的引用只来自显式
  ObjectRef。Plugin epoch 校验归 host admission，旧 epoch 不进入 mock 状态。
- TypeDesc traits 与 DataDesc construct ops 由声明者分别显式提供，均属于 CMeta；
  TinyMock 不从其中一份自动构造另一份，不创建新的 lifecycle registry。
