# CFlow SCXML QuickJS 沙箱与 HTTP 会话服务设计

**状态：** Proposed

**Issue：** [qigao/salts#122](https://github.com/qigao/salts/issues/122)

**日期：** 2026-08-30

**标准参考：** [W3C SCXML 1.0](https://www.w3.org/TR/scxml/)

**QuickJS 参考：** [QuickJS 官方文档](https://bellard.org/quickjs/quickjs.html)

## 1. 决策摘要

新增一个可选的、默认拒绝宿主能力的 `datamodel="quickjs-sandbox"`，用
QuickJS 执行 SCXML 条件、值表达式、位置表达式和 `<script>`。该模型是
Salts 扩展，不宣称等同于 W3C `datamodel="ecmascript"`。

核心决策如下：

- QuickJS 只负责同步求值，不直接访问 HTTP、文件、环境变量、进程、线程、
  动态 native module 或其他宿主资源。
- 每个活动 SCXML session 独占一个 `JSRuntime`，每个 run-to-completion macrostep
  创建一个 working `JSContext`；二者只能由该 session 的 SerialExecutor 调用，
  不同 session 不共享 QuickJS object heap。
- CMeta 管理的 staged state 仍然是事务内唯一事实源。QuickJS global data 是该
  state 的有界投影，不是另一个可独立提交的业务状态副本。
- 持久数据限制为 CMeta/CSerde 可无损表达的受限值域。函数、Promise、Symbol、
  循环引用和 native object 不得进入 session state。
- Bearer 认证、tenant/session 鉴权、HTTP ingress 和 HTTP egress 位于上层
  legacy HTTP repository/Iris 服务。Bearer token 永不进入 QuickJS 或 `_event.data`。
- 外部 HTTP 副作用只能由 SCXML `<send>`/`<invoke>` 经现有 v3 adapter 的
  prepare/commit/discard 协议产生。JS 不提供 `fetch()`。
- 第一阶段 session 是内存态并绑定一个服务进程。持久化/迁移必须先补充完整的
  Statechart configuration/history/datamodel checkpoint ABI；不得只保存 data 后
  假装能够恢复完整 session。

## 2. 背景与仓库证据

### 2.1 事实

- `cflow-scxml/src/scxml.c` 当前使用私有 `scxml_data_model` 区分
  `SCXML_DATA_MODEL_NULL` 与 `SCXML_DATA_MODEL_CMETA`，表达式、assignment、
  payload 和 session 初始化均由同一 frontend/runtime 协调。
- `cflow-scxml/include/cflow/scxml.h` 已提供版本化 CMeta compile/session options、
  有界 session 配置，以及 v1/v2/v3 Event I/O 和 invocation adapter。
- v3 adapter 的 request view 只在 prepare callback 内借用；adapter 必须在
  accepted ticket 中复制稍后仍需使用的数据。runtime publication 后才 commit，
  rollback 时 discard。
- CMeta profile 已把 published extended state 作为主事实源，并在 alternate/scratch
  slot 上执行 copy/move/destroy 事务。失败不发布半写状态。
- `_event` 的 v3 structured content 已经要求 schema 匹配并复制进 session-owned
  storage；调用方在 `try_send_v3()` 返回后可以立即修改或释放原对象。
- native Statechart instance 已能复制 active configuration/version；但
  `cflow_scxml_session` 尚未转发该只读查询，managed extended state 也不能通过通用
  byte-copy API 导出。当前两层 API 都没有能够完整恢复 history、pending timers、
  invocations 与 datamodel 的 durable checkpoint 契约。
- 当前仓库没有 QuickJS source、CMake target 或 public API。

相关设计与行为来源：

- [SCXML core frontend](2026-08-29-cflow-scxml-core-design.md)
- [CMeta datamodel](2026-08-29-cflow-scxml-cmeta-data-model-selection.md)
- [`_event` object](2026-08-30-cflow-scxml-event-object-design.md)
- [v3 content ABI](2026-08-30-cflow-scxml-content-v3-design.md)
- [`cflow/scxml.h`](../../../cflow-scxml/include/cflow/scxml.h)
- [SCXML CMeta tests](../../../cflow-scxml/tests/cflow_scxml_cmeta_test.c)

### 2.2 推论

- 把 QuickJS heap 当持久化事实源会形成第二套状态生命周期，并绕开现有 CFlow
  transaction；因此 QuickJS state 必须能够从 staged CMeta state 重建。
- 把 QuickJS 放进共享 runtime 即使使用多个 `JSContext`，仍会共享 object heap，
  扩大会话间泄漏和资源耗尽影响面。QuickJS 官方也明确 runtime 内不支持
  multi-threading，所以 per-session runtime 是最小可推理边界。
- 只保存业务 data 不能恢复完整 Statechart session；在 checkpoint ABI 完成前，
  HTTP service 必须把 session 描述为内存态，而不是 durable workflow。

## 3. 范围与非目标

### 3.1 本设计覆盖

- `datamodel="quickjs-sandbox"` 的语言和数据契约；
- QuickJS 依赖、target、公开配置与内部 adapter 边界；
- CMeta state 与 JS value 的有界双向转换；
- per-session runtime、事务、异常恢复、关闭与资源限制；
- Bearer + event/data 的 HTTP session API；
- `<send>` 到下游 HTTP server 的 outbox 边界；
- 安全、错误、可观测性、兼容性和验证矩阵。

### 3.2 本设计不覆盖

- 不实现浏览器 DOM、HTML、Web API 或 Node.js API；
- 不允许用户在请求中上传并立即执行任意 XML/JS；
- 不声明完整 W3C ECMAScript datamodel conformance；
- 不在 Salts 中实现 Bearer/JWT、数据库或 HTTP server；
- 不在第一阶段实现跨进程 session 迁移、灾难恢复或 exactly-once HTTP；
- 不用 QuickJS bytecode 作为持久化、缓存或 wire format；
- 不改变现有 `null` 与 `cmeta` profile 的公开行为。

## 4. 架构与依赖方向

```text
Client
  -> TLS + Bearer middleware                         legacy HTTP repository / Iris
  -> tenant/workflow/session authorization           service layer
  -> CSerde/CBind decode + optional CMeta event lambda
  -> cflow_scxml_session_try_send_v3()                Salts
  -> SCXML run-to-completion
       -> QuickJS sandbox expression/script           optional private engine
       -> staged CMeta state
       -> v3 Event I/O prepare tickets
  -> atomic runtime commit
       -> published state/configuration
       -> committed adapter tickets
  -> HTTP response and/or service outbox
  -> the legacy HTTP facade
  -> downstream HTTP server
```

依赖只能单向：

```text
private vendored QuickJS
        -> Salts::CFlowScxml (optional implementation)
        -> Salts::CFlow
        -> Salts::CMeta

the legacy Iris service
        -> Salts::CFlowScxml

Salts -X-> legacy HTTP repository
```

QuickJS 作为 `Salts::CFlowScxml` 的可选 PRIVATE 实现依赖，不安装
`quickjs.h`，不在 public struct 中暴露 `JSRuntime`、`JSContext` 或 `JSValue`。
本阶段不引入全局 datamodel registry 或动态插件系统；当前 frontend 是封闭的
`null/cmeta` 选择，直接加入一个内部 adapter 比公开服务定位器更小且更安全。

## 5. Workflow artifact 与信任边界

服务只执行预部署 artifact：

```text
workflow_id
workflow_version/content_digest
SCXML UTF-8 source
referenced JS UTF-8 source
CMeta root schema identity
sandbox policy identity
allowed Event I/O destination IDs
```

部署流程必须先完成 XML、SCXML、JS、schema、limits 和 destination admission，成功后
发布不可变 artifact。session 绑定精确的 `workflow_version`，已有 session 不随最新
版本隐式升级。

HTTP client 只能选择它有权限使用的 `workflow_id` 和已创建的 `session_id`。请求体
不能携带源代码、QuickJS bytecode、filesystem path、native module name 或任意出站
URL。管理员上传 workflow 属于独立控制面，需要单独的认证、审核和审计。

`<script src>` 若被 profile 接受，只能由部署控制面在 artifact admission 时按管理员
配置的 source policy 解析、获取、校验 digest 并复制进 artifact。session runtime 不
读取 URL 或文件；artifact 发布后 source bytes 不再随远端内容变化。

## 6. `quickjs-sandbox` 语言契约

### 6.1 SCXML 表面

支持：

- transition/`<if>` 的 Boolean `cond`；
- `<assign>` 的 ECMAScript value expression，以及 identifier/field/bounded-index 组成的
  schema-backed left-hand-side subset；
- `<foreach>` 的有界 CMeta sequence expression、item 与 index location；
- `<send>`、`<invoke>`、`<content>` 等已有动态 expression；
- 同步 `<script>`；
- 只读 `_name`、`_sessionid`、`_event`、`_ioprocessors`；
- 全局只读 `In(stateId)`。

`<data id="x">` 必须映射到 application-supplied CMeta root schema 中同名、可写、
可转换的字段。不存在的 field、重复 field、schema 不匹配或无法无损转换均在 compile
或 session admission 阶段失败。

### 6.2 持久值域

可提交值：

- null/optional absence；
- Boolean；
- CMeta 声明的有符号/无符号整数，转换时保持精确范围；
- finite floating-point；
- UTF-8 string；
- 有界 sequence；
- 有界 reflected struct/plain object；
- schema 明确声明并有无损 JS 表示的 enum、UUID、bytes 等类型。

下列值不能写入 session state：

- function、generator、Promise；
- Symbol、WeakMap、WeakSet；
- 循环引用或共享引用图；
- Date、RegExp、Map、Set、ArrayBuffer/TypedArray，除非 schema adapter 明确提供一个
  无损且有界的目标表示；
- QuickJS native class 或 opaque host object；
- NaN、positive/negative infinity；
- 超出目标 CMeta 类型范围或精度的 Number/BigInt。

转换失败产生 `error.execution` 并 rollback 当前 executable block，不截断、不强制
coerce，也不改用字符串等 fallback。

### 6.3 严格同步

run-to-completion 内不执行 Promise job queue，不允许 async function、top-level await、
Worker 或跨 callback suspension。脚本返回 Promise 或在求值结束后留下 pending job，
都视为 `error.execution`。

## 7. 受限宿主 API

### 7.1 默认可见对象

```javascript
// application data，名称来自已验证 CMeta root schema
order
customer
result

// SCXML read-only system values
_name
_sessionid
_event
_ioprocessors

// SCXML state query
In("state-id")
```

system values 使用 non-writable、non-configurable property；其中 object/array projection
冻结后再交给脚本。`_event` 在每次 Event selection 时替换，旧 view 不得被 native
代码继续借用。

### 7.2 永远不进入 API 表面

以下能力不是“默认关闭的 capability”，而是该 target 根本不实现：

- `fetch`、XMLHttpRequest、WebSocket、raw socket；
- filesystem、environment、process、shell、exit；
- database、secret store、credential provider；
- `std`/`os` module、`quickjs-libc.c` globals；
- native `.so`/`.dll` module；
- host allocator pointer、session pointer、CMeta object pointer；
- inbound Authorization header 或 Bearer token。

### 7.3 可配置语言策略

版本化 policy 只控制语言能力，不扩展 I/O 权限：

```c
typedef enum cflow_scxml_quickjs_policy_flag {
    CFLOW_SCXML_QUICKJS_ALLOW_DYNAMIC_CODE = UINT64_C(1) << 0,
    CFLOW_SCXML_QUICKJS_ALLOW_CLOCK = UINT64_C(1) << 1,
    CFLOW_SCXML_QUICKJS_ALLOW_RANDOM = UINT64_C(1) << 2
} cflow_scxml_quickjs_policy_flag;
```

默认不启用这些 flag：

- 无 `eval`/Function constructor/dynamic import；
- 无 wall clock；
- 无 nondeterministic random。

如果启用 clock/random，它们必须来自注入的有界 provider，不能由 sandbox 直接调用
OS。policy identity 是 workflow artifact identity 的一部分，session 期间不可热切换。

## 8. CMeta 与 QuickJS 的事务桥

### 8.1 主事实源

```text
published CMeta state
    -> CFlow copy/move construct staged state
    -> import selected fields into JS working globals
    -> evaluate one guard/action/script
    -> validate and export into a fresh CMeta scratch value
    -> replace staged state exactly once
    -> CFlow microstep commit OR destroy staged/scratch values
```

QuickJS global objects只是一次求值的工作投影。业务数据的 authoritative owner 始终是
CFlow staged/published CMeta value。JS 不能持有跨 callback 有效的 C pointer。

### 8.2 导入

- schema graph 在 program 生命周期内 borrowed immutable；
- staged object 只在当前 SerialExecutor callback 中 borrowed；
- JS import 创建独立值，不能通过 opaque pointer 回看 native storage；
- 外部 object 使用 null prototype，并拒绝 `__proto__`、`prototype`、`constructor`
  等危险输入键，除非 schema 将其声明为不能触发 prototype 行为的普通字段；
- depth、property count、sequence length、string bytes 与累计 converted bytes 使用
  checked arithmetic 和 hard limits。

### 8.3 导出

- 先创建 zero-state CMeta scratch value；
- 按 root schema 遍历 JS own properties；
- unknown/missing/duplicate/type mismatch 按 schema required/optional 契约处理；
- 每个 child 成功构造后才能加入 scratch owner；
- 完整导出成功后，以现有 CMeta move/copy protocol 替换 staged value；
- 任一失败销毁 scratch，staged value 保持进入当前 JS action 前的值。

### 8.4 `<assign>` 与 `<script>`

`<assign>` 优先复用现有 location admission 和 exact destination conversion，只让
QuickJS 计算 RHS；这样不必让 arbitrary JS property trap 穿透 native state。

guard、RHS、payload 和 target/value expression 使用 read-only `const` data bindings；
表达式内赋值产生异常，不能形成被忽略的隐藏写入。只有 `<assign>` 的 admitted
location、`<foreach>` item/index 和 `<script>` 可以修改 staged data。

`<script>` 执行后必须导出完整 root projection，因为脚本可能同时修改多个 data
variable。完整导出和 staged replacement 是一个 transaction；不能逐字段发布。

### 8.5 CSerde/CBind 边界

CSerde/CBind 位于格式适配器边界：

- HTTP JSON/body 或部署期 `<data>` content 先经 schema 校验，再 decode 到 exact CMeta
  root/event type；
- committed `service:reply` 按明确 response schema serialize；
- 将来 durable checkpoint 可以复用同一 typed data codec，但必须与 Statechart
  configuration/history/timer/invocation checkpoint 共同版本化；
- 不序列化 QuickJS heap、JSValue、native pointer 或 QuickJS bytecode；
- 不在 CSerde/CBind 中执行 expression、推进 session 或维护业务事实源。

Salts SCXML core 不依赖 TurboParser DataBind facade；需要 DataBind 的上层服务可在
自己的 target 中适配为 exact CMeta/event envelope。

## 9. QuickJS runtime 生命周期

### 9.1 Program compile

1. SCXML/XML admission 先验证结构和所有 bounds。
2. 使用临时 QuickJS compiler runtime 验证 expression/script syntax 与 policy。
3. program 只保留 immutable UTF-8 source、source location、expression kind 与 CMeta
   location/schema facts。
4. 不把 QuickJS bytecode 持久化或暴露给调用方。官方文档说明 binary object format
   可能无通知变化，因此不能作为 persistent data。

### 9.2 Session init

1. 验证 QuickJS options ABI、所有 positive limits、root schema identity 和 initial data。
2. 创建 session-owned `JSRuntime`，立即安装 heap/stack/interrupt limits。
3. 注册 runtime-level class/finalizer facts，但不创建共享 data global 或保留跨
   macrostep `JSValue`。
4. 任一步失败按逆序释放 runtime 和 CMeta temporary state；session
   handle 保持 zero-state。

### 9.3 执行

所有 QuickJS API 只能从 session SerialExecutor 线程调用。一个 session 同时最多有一个
active evaluation；HTTP handler 只能向有界 external mailbox admission，不能直接调用
`JS_Eval`。

每个 macrostep 创建一个 restricted working `JSContext`，只安装枚举过的 intrinsics、
SCXML bindings 和从当前 staged CMeta state 导入的 data。expression/script source 在该
context 第一次使用时编译，产生的 `JSValue` 只活到本 macrostep 结束。eventless
microsteps 共用同一 working context，因此观察同一 retained `_event`；macrostep commit
或 rollback 后 context 都被销毁。脚本声明的 helper function 可以在当前 macrostep 内
使用，但不能跨 Event 持久化。

所有 source 以 strict wrapper 执行。data identifiers 是 wrapper-local bindings；未声明
global write、向 `globalThis` 持久化值或修改 frozen system object 都产生异常。只有
schema-backed data bindings 会在成功路径导出。

每次 evaluation 安装绝对 deadline 和 cancellation generation。interrupt callback 只读
原子 deadline/cancel state，不分配、不记录日志、不调用用户 callback。

### 9.4 异常恢复

普通 JS exception：

1. 抽取有界、脱敏 diagnostic；
2. 销毁当前 working context 及其中全部 JSValue；
3. rollback CMeta block transaction；
4. 为同一 run-to-completion cycle 创建新的 clean context；
5. enqueue `error.execution` 后继续 selection。

timeout、stack limit、heap limit/OOM 或 sandbox contract violation 会把 realm 标记为
`DIRTY`。当前 transaction rollback 后，session 从 immutable program source 重建整个
`JSRuntime`，再创建 clean context 处理 `error.execution`。重建失败使 session terminal failed；
不能继续使用半可信 realm，也不能回退到 CMeta/null evaluator。

### 9.5 关闭

```text
OPEN
  -> CLOSING: stop external admission, set cancellation generation
  -> interrupt active evaluation
  -> cancel/drain session work according to existing SCXML contract
  -> discard uncommitted Event I/O tickets
  -> release active macrostep JSValues
  -> destroy active working JSContext
  -> JS_FreeRuntime
  -> close adapters
  -> wait adapter quiescence
  -> CLOSED
```

QuickJS finalizer 只能释放 C resource，不能执行 JS。destroy 要求 session executor 与
adapter callbacks 都已 quiescent。

## 10. 资源、容量与背压协议

所有 limits 由 versioned options 提供，零值无特殊“无限”含义：无效组合在 compile 或
session init 直接拒绝。

```c
typedef struct cflow_scxml_quickjs_limits_v1 {
    uint32_t abi_version;
    size_t struct_size;
    size_t max_source_bytes;
    size_t max_heap_bytes;
    size_t max_stack_bytes;
    uint64_t max_evaluation_time_ns;
    size_t max_conversion_depth;
    size_t max_properties;
    size_t max_array_items;
    size_t max_string_bytes;
    size_t max_snapshot_bytes;
    size_t max_diagnostic_bytes;
} cflow_scxml_quickjs_limits_v1;
```

配置层同时保留现有 external/internal/completion/effect/adapter queue capacities。满时：

- HTTP ingress mailbox full：返回明确 overload/`429` 或 service-defined `503`；
- session version conflict：`409 Conflict`；
- outbox full：当前 transition 不能提交，产生可区分错误；
- QuickJS heap/stack/time full：`error.execution`，realm 进入 DIRTY；
- response projection 超限：状态可提交与否必须由 endpoint contract 预先决定，不能在
  commit 后才发现响应必须截断。

禁止 silent drop、无界扩容、永久等待或在资源满时改成同步直连下游。

## 11. 公开 API 草案

公开 ABI 不包含 QuickJS 类型：

```c
typedef struct cflow_scxml_quickjs_compile_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const cmeta_data_desc *root;
    cflow_scxml_quickjs_limits_v1 limits;
    uint64_t policy_flags;
} cflow_scxml_quickjs_compile_options_v1;

typedef struct cflow_scxml_quickjs_clock_provider_v1 {
    uint32_t abi_version;
    size_t struct_size;
    bool (*now_ns)(void *user, uint64_t *out_now_ns);
    void *user;
} cflow_scxml_quickjs_clock_provider_v1;

typedef struct cflow_scxml_quickjs_random_provider_v1 {
    uint32_t abi_version;
    size_t struct_size;
    bool (*fill)(void *user, unsigned char *out, size_t out_size);
    void *user;
} cflow_scxml_quickjs_random_provider_v1;

typedef struct cflow_scxml_quickjs_session_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const void *initial_state;
    const cflow_scxml_quickjs_clock_provider_v1 *clock;
    const cflow_scxml_quickjs_random_provider_v1 *random;
} cflow_scxml_quickjs_session_options_v1;

cflow_scxml_status cflow_scxml_compile_quickjs(
    cflow_scxml_program *out,
    const char *input,
    size_t input_size,
    const cflow_scxml_limits *limits,
    const cflow_scxml_quickjs_compile_options_v1 *options,
    cflow_scxml_diagnostic *diagnostic);

cflow_statechart_instance_status cflow_scxml_session_init_quickjs(
    cflow_scxml_session *session,
    const cflow_scxml_session_config *config,
    const cflow_scxml_quickjs_session_options_v1 *options);

cflow_statechart_snapshot_status cflow_scxml_session_copy_configuration(
    const cflow_scxml_session *session,
    cflow_machine_state_id *out_states,
    size_t state_capacity,
    size_t *out_state_count,
    uint64_t *out_configuration_version);

bool cflow_scxml_program_state_name(
    const cflow_scxml_program *program,
    cflow_machine_state_id state,
    const char **out_name,
    size_t *out_name_size);
```

clock/random table 在 session init 时复制；其中 `user` 由 application 持有并必须
保持有效到 session destroy/quiescence。未启用对应 policy flag 时 table 必须为 NULL；
启用 flag 却缺少合法 table 时 session init 失败。

configuration accessor 只是 native read-only copy 的 SCXML wrapper；短 buffer 报告所需
count 且不写 partial result。reverse state-name view 由 immutable program 持有，到
program destroy 失效。这两个 API 不导出 managed data，也不能伪装成 durable
checkpoint。

当构建未启用 `CFLOW_ENABLE_SCXML_QUICKJS` 时，QuickJS compile entry point 明确返回
`CFLOW_SCXML_UNSUPPORTED_DATAMODEL`，不加载动态库、不注册 fallback。该选项默认
`OFF`，直到 Windows/MSVC、Linux 和 macOS 构建及 sandbox tests 全部通过。

## 12. HTTP session 服务契约

HTTP server 属于 legacy HTTP repository/Iris 或应用仓库，不加入 Salts target。

### 12.1 创建 session

```http
POST /v1/workflows/{workflow_id}/sessions
Authorization: Bearer <token>
Idempotency-Key: <opaque-key>
Content-Type: application/json

{
  "input": {
    "order_id": "A001",
    "amount": 100
  }
}
```

成功响应：

```json
{
  "session_id": "service-generated-id",
  "workflow": "order-flow",
  "workflow_version": "sha256:artifact-digest",
  "version": 1,
  "states": ["waiting_payment"],
  "reply": null
}
```

### 12.2 推进 session

```http
POST /v1/sessions/{session_id}/events
Authorization: Bearer <token>
Idempotency-Key: <opaque-key>
If-Match: "17"
Content-Type: application/json

{
  "event": "payment.completed",
  "data": {
    "transaction_id": "T100"
  }
}
```

成功响应携带 service-owned version、active states，以及 workflow 通过
`target="service:reply"` 明确产生并经过 response schema 校验的 committed reply。
默认不读取或返回全部 managed session data，避免把内部字段或 secret 派生数据暴露
给 client。

### 12.3 鉴权

Bearer middleware 验证 issuer、audience、signature、expiry 和 required scopes，随后把
可信 identity 复制进 request-owned auth context。所有 session 查询键为：

```text
(tenant_id, workflow_id, session_id)
```

不能先按全局 `session_id` 取出对象再检查 tenant。route/header/body 都是 Iris
request-arena borrowed views；任何数据一旦进入异步 mailbox、session registry 或
idempotency store，必须先完成长度校验并复制到其长期 owner。

### 12.4 幂等与并发

- 同一 tenant/session/idempotency-key + 相同 request digest 返回已提交结果；
- 相同 key + 不同 digest 返回 conflict；
- `If-Match` 不等于当前 session version 返回 `409`，不执行 Event；
- service session owner gate 串行覆盖 version check、mailbox admission、idle settlement、
  committed reply/configuration snapshot 与 version publication；锁内不执行下游 HTTP；
- 同一 session 的 admitted Events 由 SerialExecutor FIFO 执行；
- HTTP connection 并发不等于 session execution 并发。

service version 是成功 settled 外部 Event 的版本，不直接复用 native
`configuration_version`，因为 targetless transition 或纯 data mutation 未必改变 active
configuration。失败 Event 不消耗 service version，其精确结果保存在同一
idempotency record 中。

## 13. CMeta event lambda 边界

application 可以在 HTTP decode 后、mailbox admission 前运行一个 typed CMeta callable：

```text
decoded request event
  -> validate/normalize/authorize business fields
  -> produce exact SCXML Event envelope
  -> cflow_scxml_session_try_send_v3()
```

该 callable：

- 只处理 Event，不创建或持有 QuickJS runtime；
- 不执行网络 I/O；
- 输入只在调用期间 borrowed，输出由 caller 明确 copy/move；
- 若可能失败或观察状态，不能标记为 PURE，也不能被 CFlow 重排；
- signature 由 application 的有限 CMeta callable registry 注册，不加入通用 sandbox
  ABI，也不把任意 `void *` Event map 暴露给脚本。

Bearer identity 只供鉴权层决定“能否提交这个 Event”，不作为业务 data 自动注入 JS。

## 14. 下游 HTTP 与 transactional outbox

QuickJS 不调用 HTTP。外部请求来自 `<send>` 或 `<invoke>`：

```text
SCXML <send target="payment-service">
  -> v3 adapter validates named destination and payload schema
  -> prepare reserves bounded outbox row and copies callback-scoped data
  -> Statechart publishes state
  -> ticket commit makes outbox row visible
  -> legacy HTTP repository worker sends downstream request
```

`target` 解析为服务端配置的 destination ID，不能直接接受 event data 中的 URL。
destination config 负责 scheme/host/port/path prefix、TLS policy、service credential、
redirect policy、timeout、retry 与 response limits。

出站请求不得转发 inbound Bearer。worker 使用独立 service credential，并带稳定
delivery/idempotency ID。HTTP 失败不能回滚已经提交的 SCXML state；outbox row 保持
`PENDING/RETRYABLE/TERMINAL_FAILURE` 的唯一状态，按有界策略重试或进入 dead-letter
观察面。

现有 SCXML adapter ticket 只保证进程内 state/effect publication ordering。若服务需要
数据库级原子 session + outbox，storage transaction 由上层 session store 实现；在该
接口完成前不得宣传 crash-safe exactly-once。

同步 HTTP reply 是同一 adapter 的特殊 named destination `service:reply`，不是 URL。
prepare 将 reply payload 复制进 request-owned reservation，Statechart commit 后 ticket
commit 才令 reply 可见；discard 不产生部分 response。每个请求至多接受一个 reply，
重复 reply 产生 `error.execution`。客户端断开后 committed reply 仍进入有界
idempotency record，重试同一 key 可取得同一结果。

## 15. 错误语义

| 阶段 | 条件 | 结果 |
|---|---|---|
| workflow admission | JS syntax、unsupported language/API、schema mismatch | compile 失败，无 program |
| session init | ABI/limit/provider/initial-state/QuickJS allocation 失败 | session 保持 zero-state |
| event admission | auth、schema、body、metadata、mailbox capacity 失败 | 不消费 session queue row |
| guard/value evaluation | JS exception、conversion failure | `error.execution`，当前 block rollback |
| sandbox limit | timeout、heap、stack、snapshot limit | `error.execution`，realm DIRTY 并重建 |
| `<send>` materialize | invalid target/payload | `error.execution` |
| Event I/O delivery admission | destination unavailable/full | `error.communication` 或显式 FULL |
| adapter contract violation | accepted ticket invalid、commit/discard contract 破坏 | session fatal，不发布半写 |
| shutdown | new request after gate closed | CLOSED/HTTP service unavailable |

compile status 新值只能追加在 enum 尾部，不能重排现有数值。错误消息只包含 source
location、expression kind、phase 和脱敏摘要；不记录 Bearer、secret、完整请求 data 或
任意用户脚本全文。

## 16. 可观测性

按 tenant/workflow/session 聚合但不输出敏感 payload：

- active/creating/closing/failed session 数；
- mailbox current/peak occupancy、FULL 和 CLOSED 次数；
- JS evaluation count、P50/P95/P99 latency；
- timeout、heap、stack、conversion、exception 与 realm rebuild 次数；
- snapshot bytes、array/property/depth high-water marks；
- outbox pending/retry/terminal failure、delivery latency；
- session version conflict、idempotency replay/conflict；
- auth deny 与 cross-tenant lookup deny。

日志只在 HTTP、session owner 或 outbox worker 消费/转换错误的边界记录一次。QuickJS
interrupt handler、hot guard 和每个 property conversion 不记录日志。

## 17. 安全风险与控制

- **HIGH（事实）：** QuickJS `std`/`os` 提供 libc/file/process 能力。构建不得链接
  `quickjs-libc.c`，context 不注册这些 module/global。
- **HIGH（推论）：** 任意 `targetexpr` URL 会形成 SSRF 与 credential exposure。只允许
  destination ID，并在服务端解析目标。
- **HIGH（推论）：** 只按 `session_id` 查询会形成跨 tenant object authorization
  缺陷。所有 registry/storage 操作必须包含 `tenant_id`。
- **HIGH（推论）：** 共用 runtime 会放大会话间 heap 泄漏和 DoS。每个 session 独占
  runtime，且每次调用受 SerialExecutor 约束。
- **HIGH（事实）：** 当前没有完整 durable checkpoint API。第一阶段不得在进程重启后
  只恢复 data 而丢弃 active configuration/history/timers。
- **MED（事实）：** QuickJS 官方只描述 Linux/macOS Makefile 和通过 MinGW
  cross-compilation 的 preliminary Windows support。引入前必须用仓库 MSVC preset
  验证 upstream source 或最小可追溯 patch。
- **MED（推论）：** per-macrostep context/source compile 和完整 root import/export 可能
  增加延迟与分配。先实现安全基线并 benchmark，再决定是否缓存 immutable conversion plan；
  不先引入共享 heap 或 lock-free pool。
- **LOW（常用做法）：** policy、limits、artifact digest 与 QuickJS upstream revision
  都应进入诊断/version metadata，便于定位行为差异。

## 18. QuickJS 依赖准入

采用官方 MIT-licensed QuickJS source 的固定 release/commit，放入 `vendor/quickjs/`，
保留 upstream URL、version、LICENSE 和本地 patch 清单。只编译 engine 所需 source；
不构建 `qjs`、`qjsc`、`quickjs-libc.c` 或 native module loader。

依赖 PR 必须同时证明：

- MSVC、Clang/GCC C build；
- Windows/Linux/macOS；
- `CFLOW_ENABLE_SCXML_QUICKJS=OFF` 时现有 package/link surface 不变；
- enabled installed consumer 不需要包含 QuickJS header；
- ASan/UBSan 或平台等价内存验证；
- upstream license/source/version 可追溯；
- local patch 小而独立，升级时能重新应用或删除。

若 MSVC 需要不可维护的 fork，依赖准入失败；不能静默改用另一个 JS engine。

## 19. 验证矩阵

### 19.1 Compile/admission

- exact `datamodel="quickjs-sandbox"` accepted；其他拼写 fail fast；
- invalid JS syntax、dynamic import、disallowed eval/Function rejected；
- unknown data ID、schema mismatch、read-only system assignment rejected；
- source/depth/instruction/property/string/array limits 的 0、1、exact、+1 与 overflow；
- feature OFF 返回 unsupported，null/cmeta regression 不变。

### 19.2 API absence与逃逸

- `fetch/std/os/process/require/Worker` 不存在；
- module loader 无法解析 relative/system/native module；
- `constructor.constructor`、prototype chain、`__proto__` payload 不能获得动态执行或
  修改 system objects；
- inbound Authorization 和 service credentials 对 JS 不可见；
- 客户端 data 不能选择任意 URL。

### 19.3 Resource isolation

- infinite loop 被 interrupt；
- recursive stack、allocation bomb、large string/array/object 被 hard limit 拒绝；
- timeout/OOM 后当前 state rollback，realm rebuild 后下一合法 Event 可执行；
- 一个 session 的 limit failure 不改变另一 session 的 globals、state 或 counters；
- 同一 session 的并发 HTTP Events 按 FIFO/version/idempotency 契约处理。

### 19.4 Data/transaction

- CMeta ↔ JS every supported scalar/cstl/struct round trip；
- exact integer boundary、non-finite number、UTF-8 invalid/limit、nested limit；
- `<assign>` 单写、`<script>` 多写、exception after write 全部 atomic；
- `_event` metadata/data retention through eventless microsteps and replacement；
- structured event caller mutation isolation；
- unknown/missing/extra property、copy/move/destroy failure 无 leak/double-free。

### 19.5 Adapter/outbox/shutdown

- prepare accepted/rejected/full/closed/invalid-ticket；
- runtime rollback exactly-once discard，commit exactly once；
- outbox full 不发布部分 state/effect；
- close during evaluation、close during adapter work、quiescent destroy；
- HTTP retry 使用 service idempotency ID，不复用 inbound Bearer；
- invalid redirect/DNS/private-address policy fail closed。

### 19.6 Build/package

- focused QuickJS/SCXML TinyTest；
- adjacent CMeta/CFlowScxml/Statechart regression；
- MSVC、Linux Clang/GCC、macOS；
- feature OFF/ON configure-build-test；
- isolated install + external C/C++ consumer；
- sanitizer、leak、fuzzed CMeta↔JS conversion；
- benchmark typical/peak/saturated sessions，报告 latency、heap、rebuild 和 conversion
  bytes，不用估算替代测量。

## 20. 迁移与回滚

实现按以下兼容顺序推进：

1. vendored QuickJS 与 disabled-by-default CMake boundary；
2. private sandbox context、limits、API-absence tests；
3. CMeta scalar/struct/sequence conversion bridge；
4. `compile_quickjs` 与 session lifecycle；
5. guard/value/location/`<assign>`；
6. transactional `<script>`；
7. v3 Event I/O integration；
8. 独立 legacy HTTP repository/Iris session service 与 outbox；
9. durable checkpoint 作为单独架构增量。

任何阶段都不能让未实现能力被 public admission 接受。回滚时关闭
`CFLOW_ENABLE_SCXML_QUICKJS`，`quickjs-sandbox` 文档明确返回 unsupported；现有
`null/cmeta` program、ABI、wire data 与 session 行为保持不变。已经创建的
quickjs-sandbox session 不能跨禁用部署恢复，部署系统必须在 rollout 前 drain 或拒绝
该 workflow version。

## 21. 最终边界

```text
Bearer 决定“谁可以访问 session”
CMeta event lambda 决定“输入事件是否有效以及如何归一化”
SCXML 决定“状态如何迁移”
QuickJS sandbox 决定“受限表达式如何求值”
CMeta staged state 决定“事务内业务数据的唯一真值”
Event I/O adapter 决定“哪些副作用可以被预留并提交”
legacy HTTP repository outbox worker 决定“如何可靠地访问下游 HTTP server”
```

这些职责不能相互穿透：认证资料不进入脚本，脚本不执行 I/O，HTTP worker 不直接
修改 session state，CMeta lambda 不拥有 QuickJS runtime。
