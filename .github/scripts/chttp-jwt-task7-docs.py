from pathlib import Path


JWT_SOURCE = Path("chttp/src/chttp_jwt.c")
README = Path("chttp/README.md")


def rewrite_jwt_parser_style() -> None:
    text = JWT_SOURCE.read_text()
    start_marker = "static const char *chttp_jwt_bearer_token(const chttp_server_request_view *request) {"
    end_marker = "\n\nstatic int chttp_jwt_claims_match"
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    replacement = '''static const char *chttp_jwt_bearer_token(const chttp_server_request_view *request) {
  const char *authorization = NULL;
  size_t authorization_size;
  size_t index;
  if (request == NULL || request->headers == NULL) return NULL;
  for (index = 0u; index < request->header_count; ++index) {
    const chttp_header *header = &request->headers[index];
    if (chttp_jwt_ascii_equal_ci(header->name, "Authorization")) {
      if (authorization != NULL || header->value == NULL) return NULL;
      authorization = header->value;
    }
  }
  if (authorization == NULL) return NULL;
  authorization_size = strlen(authorization);
  if (authorization_size <= CHTTP_JWT_BEARER_SCHEME_SIZE ||
      authorization[CHTTP_JWT_BEARER_SCHEME_SIZE] != ' ')
    return NULL;
  for (index = 0u; index < CHTTP_JWT_BEARER_SCHEME_SIZE; ++index) {
    const unsigned char value = (unsigned char)authorization[index];
    const unsigned char expected = (unsigned char)"Bearer"[index];
    const unsigned char lower =
        value >= 'A' && value <= 'Z' ? (unsigned char)(value + ('a' - 'A')) : value;
    const unsigned char expected_lower = expected >= 'A' && expected <= 'Z'
                                             ? (unsigned char)(expected + ('a' - 'A'))
                                             : expected;
    if (lower != expected_lower) return NULL;
  }
  index = CHTTP_JWT_BEARER_SCHEME_SIZE;
  while (index < authorization_size && authorization[index] == ' ') ++index;
  return index == authorization_size ? NULL : authorization + index;
}'''
    JWT_SOURCE.write_text(text[:start] + replacement + text[end:])


def rewrite_readme() -> None:
    text = README.read_text()
    old_version = "CHTTP 当前 library version 是 2.0.0，ABI major/SOVERSION 是 2。"
    new_version = "CHTTP 当前 library version 是 2.2.0，ABI major/SOVERSION 是 2。"
    if text.count(old_version) != 1:
        raise SystemExit("unexpected CHTTP version paragraph")
    text = text.replace(old_version, new_version, 1)

    start_marker = "### JWT Bearer（HS256）\n"
    end_marker = "\n需要阻塞数据库或外部服务时"
    start = text.index(start_marker)
    end = text.index(end_marker, start)
    section = r'''### JWT Bearer（HS256）

CHTTP 公开的是一个有界的 HS256 JWT Bearer admission 边界，不公开 CJWT 类型或头文件。客户端用
`chttp_jwt_hs256_token_create()` 生成拥有型 token，再用 `chttp_jwt_bearer_header()` 写入调用方持有的
Authorization header buffer；请求提交完成后用 `chttp_jwt_token_destroy()` 释放 token。

服务端先初始化一个 validator，再在 `chttp_server_start()` 之前把它绑定为 server-wide policy、单个
HTTP route policy，或单个 WebSocket route policy。JWT 不再作为 ordinary middleware 注册：

```c
static const unsigned char key[] = "0123456789abcdef0123456789abcdef";
chttp_jwt_bearer_validator validator = {0};
chttp_jwt_bearer_validator_options jwt = {
    .size = sizeof(jwt),
    .key = key,
    .key_size = sizeof(key) - 1u,
    .expected_issuer = "issuer.example",
    .expected_audience = "api.example"};

chttp_jwt_bearer_validator_init(&validator, &jwt);
chttp_server_use_jwt_bearer(&server, &validator);               /* all routes */
/* or chttp_server_route_with_jwt_bearer(..., &validator); */  /* selected HTTP route */
/* or chttp_server_websocket_with_jwt_bearer(..., &validator); */
```

route-specific validator 覆盖 server-wide default；配置了 server-wide policy 后没有隐式 per-route opt-out。
认证时序固定为：validated headers → route selection → JWT verification → `100 Continue` →
`body_open`/HTTP/2 DATA → ordinary middleware → handler/WebSocket opening callback。因此未认证的 streaming
request 不会先进入应用 body sink；ordinary middleware 可以读取已验证的 `request->jwt_claims`，但不负责
执行 JWT admission。

当前安全 contract：HS256 secret 至少 32 bytes；`exp` 默认必须存在，只有显式设置
`allow_missing_exp != 0` 才允许缺失；Bearer scheme 大小写不敏感，并要求 scheme 后至少一个 literal SP，
多个 SP 合法，HTAB 不是 separator。缺失/重复 Authorization、malformed token、错误 key/签名、非 HS256、
issuer/audience 不匹配、expired 或 future-`nbf` token 都统一返回 `401 Unauthorized` 与
`WWW-Authenticate: Bearer`，不把内部验证原因暴露给客户端。

验证成功后的 claims view 与 request 一样只在当前 callback 有效，不能跨 callback、线程或异步 handoff 保存
裸指针。validator 持有 key/expected claims 的副本；所有使用该 validator 的 server 必须先 stop/destroy，
之后才能调用 `chttp_jwt_bearer_validator_destroy()`。HTTP/1.1、HTTP/2、HTTP/1.1 WebSocket Upgrade 与
RFC 8441 Extended CONNECT 共用同一个 pre-body admission contract。完整验证见
`chttp/tests/chttp_jwt_test.c`、`chttp/tests/chttp_server_test.c`、`chttp/tests/chttp_h2_server_test.c` 与
`chttp/tests/chttp_websocket_test.c`。
'''
    README.write_text(text[:start] + section + text[end:])


rewrite_jwt_parser_style()
rewrite_readme()
