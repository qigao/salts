# CHTTP WebSocket/WSS 实施计划

1. 新增私有 `chttp_websocket_handshake` 模块和单元测试，覆盖 RFC 6455 server validate/Accept 与 client request/response validation。
2. 扩展 server parser 的 Upgrade callback 与 consumed-byte 结果，先用 parser 测试固定 coalesced frame 边界。
3. 新增显式 WebSocket route 和 middleware terminal adapter，将成功连接桥接到 CNet WebSocket engine。
4. 扩展 server connection 的有界 upgrade buffer、WS send/receive/close 状态和 cleanup。
5. 新增 single-owner 同步 WebSocket client，内部驱动 CNet，支持 `ws://` 和严格 `wss://`。
6. 增加 plain/TLS end-to-end、边界、安装消费和公开头文件测试，更新 README/TODO。
7. 运行 clang-format、focused tests、Release 全量测试与 ASan focused tests；最后运行 CodeGraph sync、`git diff --check` 和状态审计。
