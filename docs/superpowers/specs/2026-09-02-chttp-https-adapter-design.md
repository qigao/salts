# CHTTP HTTPS 适配与 TLS 连接池隔离设计

## 背景

CNet 已提供 TLS 1.2+ 客户端和服务端传输，包含系统/显式信任源、主机名校验、SNI、ALPN、mTLS、握手超时和 `close_notify`。CHTTP 当前仅接纳 `tcp://` 与 `pipe://`，连接池只以 `connection_uri + authority` 为键，服务端只调用明文 `cnet_listener_accept()`。

本阶段只把现有 HTTP/1.1 客户端和服务端接到 CNet TLS；不实现 HTTP/2、WebSocket Upgrade、S3 或 HTTP/3。TLS 协议仍完全由 CNet/OpenSSL 实现，CHTTP 不复制握手或密码学逻辑。

## 目标与非目标

目标：

- 同步和高级异步 CHTTP 客户端均可使用 `tls://host:port`。
- 保持强制 peer/hostname verification，不提供明文降级或关闭校验的开关。
- 相同 TLS profile、`connection_uri` 与 `authority` 的顺序请求可复用 keep-alive 连接。
- 不同 TLS profile 绝不共享连接。
- CHTTP 服务端可在相同路由、中间件、Session 与响应 API 上启用 TLS 或必需的 mTLS。
- TLS 配置在调用返回后不依赖调用方字符串、数组或密码内存。

非目标：

- 不增加 `https://` URL 解析；`connection_uri` 继续表达传输地址，使用 `tls://`。
- 不实现 HTTP/2 ALPN 分派；本阶段只允许空 ALPN 或唯一的 `http/1.1`。
- 不改变一个 HTTP/1.1 连接同一时刻最多一个 in-flight request 的约束。
- 不增加自动重试、代理、重定向、压缩或流式 body。

## 候选方案

### A. 请求直接借用 `cnet_tls_client_config *`

优点是 API 最小。缺点是 keep-alive 连接会超过请求参数生命周期；若只比较指针会出现悬空或地址复用，若深拷贝则必须长期保存私钥密码，并重复维护 CNet 的配置校验。拒绝。

### B. 每个自定义 TLS 请求禁用连接池

旧 HTTP codebase 的 H1 对自定义 TLS 身份采用过这一保守策略。它安全但不能满足本仓库已经明确提出的“TLS profile 进入连接池键”与连接复用目标。拒绝。

### C. CNet 可复用客户端 TLS profile + CHTTP 引用计数 profile

CNet 在 profile 初始化时构造 OpenSSL `SSL_CTX`，同步消费路径、密码和 ALPN；连接只引用该不可变上下文。CHTTP profile 包装 CNet profile并作为池键身份，由请求/空闲 slot 持有引用。服务端在 `chttp_server_init()` 中建立 CNet TLS server context。选择此方案。

## 公开接口

### CNet

新增 opaque `cnet_tls_client`：

```c
typedef struct cnet_tls_client { void *impl; } cnet_tls_client;

int cnet_tls_client_init(cnet_tls_client *client,
                         const cnet_tls_client_config *config);
int cnet_tls_client_destroy(cnet_tls_client *client);
```

`cnet_connect_options` 保留现有一次性 `tls` 配置，并新增 `tls_client`。两者互斥；都为空时 `tls://` 使用现有 verified defaults。`cnet_connect()` 在返回前持有 profile context，因而调用返回后 profile wrapper 可以被销毁，已接纳连接仍然安全。

### CHTTP 客户端

新增 opaque `chttp_tls_profile`：

```c
typedef struct chttp_tls_profile { void *impl; } chttp_tls_profile;

int chttp_tls_profile_init(chttp_tls_profile *profile,
                           const cnet_tls_client_config *config);
int chttp_tls_profile_destroy(chttp_tls_profile *profile);
```

`chttp_request_options` 与 `chttp_options` 新增 `const chttp_tls_profile *tls`。profile 仅能用于 `tls://`。空 profile 表示 CNet verified defaults；非空 profile 在 submit 成功前由 slot 持有引用，因此回调前销毁公开 wrapper 不会导致悬空。

连接池键为：

```text
(connection_uri, authority, tls_profile_identity)
```

其中明文与默认 TLS 使用各自固定身份；显式 TLS profile 使用不可变内部对象身份。两个内容相同但分别初始化的 profile 保守地视为不同安全域；要复用连接，调用方应复用同一个 profile。

### CHTTP 服务端

`chttp_server_config` 新增：

```c
const cnet_tls_server_config *tls;
```

非空时，`chttp_server_init()` 同步建立并拥有 CNet TLS server context，`chttp_server_start()` 仍建立一个 TCP listener，但接受路径改用 `cnet_listener_accept_tls()`。停止并销毁所有连接后才释放 server context。证书配置无效时初始化立即失败，不启动监听器，也不回退到明文。

## 状态与所有权协议

### 客户端

- 数据单元：一个 HTTP request slot 及其独占 CNet connection。
- 主事实源：slot 状态机；TLS profile 只是不可变连接策略。
- 生命周期：profile public ref 从 init 到 destroy；每个新建或空闲 TLS slot 持有一个内部 ref；slot release 恰好释放一次。
- 线程拓扑：CHTTP client 仍为单 owner；不同 client 可在不同线程共享不可变 profile，引用计数为原子操作。
- 容量与背压：profile 不改变 `request_capacity` 或 CNet 硬上限；池满仍返回 `SALTS_ENOBUFS`。
- 失败：profile/ALPN/URI 组合无效在 submit 前失败；握手错误继续通过现有 `chttp_error` transport stage 传播。
- 关闭：取消、stop、peer close 和 idle eviction 都先关闭连接，再释放 slot/profile；没有隐式重连或降级。

### 服务端

- 数据单元：一个 accepted CNet TLS connection 对应一个现有 server connection slot。
- 主事实源：`chttp_server_impl` 拥有唯一 TLS server context。
- 生命周期：init 成功后持有 context；accepted session 由 CNet 自行 retain；server 网络完成 stop/destroy 后释放 public context。
- 线程拓扑：accept 和 TLS handshake 由现有 owner worker 驱动；handler 仍串行运行。
- 容量与背压：TLS 继续受 `network.connection_capacity`、`tls_io_buffer_bytes` 和握手超时限制。
- 失败：证书/密钥/ALPN/TLS buffer 配置无效时 fail fast；单个握手失败只关闭该连接并计入既有终态路径。

## ALPN 契约

CHTTP 当前只有 HTTP/1.1 parser。客户端与服务端 TLS 配置只允许：

- 未配置 ALPN；或
- 唯一协议 `http/1.1`。

含 `h2` 或其他协议的配置返回 `SALTS_ENOTSUP`。后续引入 H2 时，应把 ALPN 选择提升为 HTTP transport dispatcher，而不是在 H1 parser 内增加 fallback。

## 兼容性、迁移与回滚

- 所有变化均为结构尾部字段或新增函数；现有 designated/zero initialization 的源码行为保持不变。
- `cnet_connect_options`、`chttp_options`、`chttp_request_options` 与 `chttp_server_config` 没有
  `struct_size`，追加字段不是二进制 ABI 兼容变化；本次静态库消费者必须使用匹配头文件重新编译并
  重新链接，后续稳定 ABI 应改用版本化 options。
- `tcp://`、`pipe://`、同步/异步 API、路由、中间件和 Session 语义不变。
- 使用 HTTPS 的调用方需在 client/server 的 `network` 配置中设置非零 `tls_io_buffer_bytes` 与 `tls_handshake_timeout_ms`。
- CRPC 不改公开结构；其 `crpc_options` 后续可单独增加 profile。当前阶段用 CHTTP 回归确保明文 RPC 不退化。
- 回滚可删除新增字段/API和 TLS 分支；原有明文路径未被替换。

## 验证范围

- CNet：profile 参数校验、一次性配置/profile 互斥、明文拒绝、profile wrapper 销毁后已接纳连接仍完成握手。
- CHTTP client：verified HTTPS request/reply、同 profile keep-alive 复用、不同 profile 池隔离、plain/profile 组合拒绝、H2 ALPN 拒绝。
- CHTTP server：证书在 init 后可释放、TLS 与 mTLS 初始化失败边界、路由/中间件/Session 在 HTTPS 下保持行为。
- 相邻回归：全部 CNet、CHTTP、CRPC tests，C/C++ header tests，installed consumer，最终全量 Release CTest。

## 依据

- [RFC 9110: HTTP Semantics](https://www.rfc-editor.org/rfc/rfc9110)
- [RFC 7301: TLS ALPN](https://www.rfc-editor.org/rfc/rfc7301)
- [OpenSSL SSL_CTX](https://docs.openssl.org/3.0/man3/SSL_CTX_new/)
