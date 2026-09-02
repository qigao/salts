# CHTTP HTTP/2 与 S3 设计

## 背景与范围

本阶段把同一所有者仓库 `qigao/TurboHTTP` 的 HTTP/2 与 S3 能力迁入
TurboUtils。审查源为 `C:\projects\cpp\TurboHTTP` commit
`38f1e389b3f94909db6cb2482a8cbc16522e7e4f`。源仓库没有根级
`LICENSE`、`COPYING` 或 `NOTICE`；迁移保留本节的来源、commit 与作者记录，
不推断或新增许可文本。

范围包括 HTTP/2 client、h2c、TLS ALPN `h2`、同步 request/reply、异步
submit/callback、S3 SigV4、对象 CRUD、流式传输与 multipart。HTTP/3 不在范围内，
也不得成为构建依赖或运行时 fallback。

## 架构决策

HTTP/2 不新增一套并列的公开 client。`chttp_client` 与
`chttp_async_client` 仍是唯一 client 入口，请求通过显式协议选择进入 HTTP/1.1
连接池或 HTTP/2 session 池。缺省值保持 HTTP/1.1，因此现有零初始化调用方的行为不变。

HTTP/2 分三层：

1. `chttp_h2_frame`、`chttp_h2_hpack`、`chttp_h2_proto` 是私有、无 I/O 的协议核心；
2. `chttp_h2_session` 独占一个 CNet stream、协议引擎、stream table 与 pending output；
3. `chttp_client` 把公开 request handle、deadline、取消、callback 和 owning response
   投影到 H1 slot 或 H2 stream。

S3 是独立的上层模块，只借用调用方提供的 CHTTP client。S3 的 canonical request
是签名与 wire request 的唯一事实源；签名完成后不得修改 method、authority、path、query、
headers 或 body hash。

### 下游依赖边界

`llhttp` 与 `c-ares` 是实现细节。安装包使用者只链接 `Rocida::CHTTP`、
`Rocida::CNet` 或更高层 target，不需要发现、链接或配置这两个包。由于静态库的 PRIVATE
依赖仍会以 link-only 形式进入 CMake export，CNet 与 CHTTP 使用共享库边界封装各自私有
依赖；Windows 生成稳定 import library/DLL，Linux 生成带项目版本的 shared object。公开
header 不出现第三方类型，安装测试显式禁用两个 package 的发现以验证边界。

## HTTP/2 数据与生命周期协议

- 线程拓扑：一个 `chttp_async_client` 由调用其 API/poll 的单线程拥有；一个 H2 session
  只有该 owner 推进。同步 client 的私有 worker 仍是唯一 owner。公开对象不承诺 MPMC。
- 请求输入：submit 在返回前复制 method、authority、target、headers 与 flat body；成功后
  调用方可立即释放输入。流式 source 只能在 owner 上回调，回调返回前借用输出 buffer。
- 响应输入：HPACK callback 的 name/value 与 DATA view 只在 callback 内有效。普通 async
  response view 在 completion callback 返回时失效；同步 response 复制为 owning storage。
- session key：`connection_uri + authority + TLS profile identity + HTTP version`。HTTP/1.1
  与 HTTP/2 连接不能互相复用。
- 状态：`NEW -> CONNECTING -> ACTIVE -> DRAINING -> CLOSED`，任一阶段可进入
  `FAILED`。收到 GOAWAY 后停止接收新 stream；已接受 stream 继续收敛，超过
  Last-Stream-ID 的请求返回明确的未处理错误。
- 取消：未提交 stream 直接完成为 canceled；已提交 stream 发送
  `RST_STREAM(CANCEL)`，并且每个请求只产生一次 terminal callback。
- 关闭：stop 关闭 admission、发 GOAWAY、drain 已提交 stream；timeout 后取消剩余 stream
  并关闭 CNet connection。destroy 只在 stop 完成且没有 callback 正在执行时释放。

### 硬上限与背压

所有加法和乘法先做 checked arithmetic。默认值可由 client config 调整：

| 资源 | 默认上限 | 满额行为 |
| --- | ---: | --- |
| 本地并发 stream | 100 | submit 返回 `TURBO_EBUSY` |
| frame payload | 16 KiB | connection protocol error |
| header list | 64 KiB | stream header-too-large error |
| HPACK dynamic table | 4 KiB | SETTINGS/HPACK 超界为 compression error |
| 单 stream 请求或响应 body | 100 MiB | stream body-too-large error |
| session retained response | 128 MiB | 暂停 receive；无法恢复时 fail fast |
| 单 stream pending output | 128 KiB | source 暂停并等待 WINDOW_UPDATE/send completion |
| parser input/header block | frame/header 上限 | 超界立即终止 connection |

协议核心的 growable buffer 带 `max_capacity`；reserve 超过上限返回 capacity error，不允许
无界扩容。stream table 容量在 session 创建时确定，关闭的 stream slot 可复用。

## TLS 与协议选择

- 显式 HTTP/2 over TLS 要求 TLS profile 提供 `h2` ALPN，并在 CONNECTED 后通过
  `cnet_tls_negotiated_alpn()` 验证结果恰为 `h2`；缺失或其他结果返回 protocol error。
- 显式 h2c 使用 prior knowledge client preface，不发送 HTTP/1.1 Upgrade。
- 显式 HTTP/2 失败不得降级到 HTTP/1.1。
- AUTO 只有在请求 body 尚未交给 source 且调用方显式允许时才可选择 HTTP/1.1；首版不
  暴露 AUTO，避免产生隐式重放语义。

## S3 一致性协议

- credential provider 由调用方或 S3 client 拥有，所有权由 init 选项明确；注入的 CHTTP
  client 始终 borrowed，并且必须活过 S3 client 与全部请求。
- object body、stream chunk、ETag 与 parsed XML 都有明确 owning/view 形式；callback view
  在 callback 返回时失效。
- multipart 的事实源是 upload id、part number、ETag 与已提交状态。complete 前按 part
  number 排序并拒绝缺口/重复；任何失败保留可显式 resume/abort 的状态。
- 文件下载写入同目录临时文件，校验成功后原子 rename；失败删除临时文件，不覆盖目标。
- access key、secret、session token、derived key、SSE-C key 与 presigned query 不写日志。

## 兼容性、验证与回滚

新增字段只追加到公开配置结构尾部；零初始化保持 HTTP/1.1。源码兼容，重新编译后生效；
由于 C 结构大小变化，不承诺旧二进制与新静态库混用。

验证按 frame、HPACK、in-memory protocol、CNet h2c、TLS ALPN、同步/异步 API、shutdown、
S3 signer/URL/XML、mock-server CRUD、streaming/multipart、C/C++ header、installed consumer
逐层进行，再运行 Release、ASan 与相邻 CNet/CHTTP/CRPC 回归。

协议核心、CNet adapter 和 S3 是独立提交。某一层回滚不得改变较低层公开接口；未完成的
能力保持私有，不以 experimental flag 暴露。
