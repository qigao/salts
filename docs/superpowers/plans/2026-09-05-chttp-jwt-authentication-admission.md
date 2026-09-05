# CHTTP JWT Authentication Admission Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move JWT Bearer verification into a single pre-body request-admission phase, harden HS256/Bearer validation, and prove identical authentication semantics across HTTP/1.1, HTTP/2, and WebSocket admission.

**Architecture:** Add one private headers-complete admission seam to the HTTP/1 parser and one private `chttp_server_request_admit()` state transition shared by HTTP/1.1 and HTTP/2. JWT policy is bound explicitly at server, route, or WebSocket registration time; ordinary middleware runs only after admission succeeds. Keep CJWT private and keep the public JWT surface HS256-only in this change.

**Tech Stack:** C11, CHTTP, CNet, llhttp, private CJWT, OpenSSL, TinyTest, CMake presets/CTest.

**Spec:** `docs/superpowers/specs/2026-09-05-chttp-jwt-authentication-admission-design.md`

**Implementation diff base:** `78335820a601f7b024b26e0870a3485921016475` (`docs: define CHTTP JWT authentication admission`). Final scope audits compare production/test changes against this fixed commit instead of a relative `HEAD~N` range, so extra RED/fix commits cannot invalidate the checkpoint.

## Global Constraints

- Authentication runs exactly once after validated headers and route selection, before `100 Continue`, application body admission, ordinary middleware, HTTP handlers, or WebSocket opening callbacks.
- Do not introduce a generic public Authenticator abstraction in this change.
- `chttp_server_route_with_jwt_bearer()` remains public but no longer inserts JWT into the ordinary middleware array.
- Add `chttp_server_use_jwt_bearer()` for one server-wide default policy and `chttp_server_websocket_with_jwt_bearer()` for a protected WebSocket route.
- A route-specific JWT validator overrides the server-wide default; there is no per-route opt-out from a configured server-wide policy.
- Remove `chttp_jwt_bearer_middleware` from the public application contract and implementation path.
- HS256 issuance and validation reject secrets shorter than 32 bytes.
- Bearer validation requires `exp` by default; `allow_missing_exp != 0` is the only escape for a token without `exp`.
- A present expiration is invalid when `now - clock_skew_seconds >= exp`; `nbf` remains valid at its exact skew-adjusted boundary.
- The Bearer scheme is case-insensitive and requires one or more literal SP characters before a non-empty credential; HTAB is not a scheme separator.
- Authentication failures remain uniform `401 Unauthorized` with `WWW-Authenticate: Bearer` and do not expose the validation reason.
- The same admission invariant applies to HTTP/1.1, HTTP/2, HTTP/1.1 WebSocket Upgrade, and RFC 8441 Extended CONNECT.
- Request/stream reset destroys the decoded CJWT owner and clears all admitted route/authentication state.
- Do not add RS256/PS256/ES256/EdDSA, `kid`, JWK/JWKS, custom claims, scope/role authorization, OAuth `at+jwt`, revocation, refresh tokens, or JWE.
- Keep `CHTTP_LIBRARY_VERSION 2.2.0` and `CHTTP_ABI_VERSION 2` unchanged in this correction: the JWT surface being corrected is the immediately preceding unreleased master change, and this branch must not turn the security correction into a library-wide ABI release operation.
- Use the existing repository presets; do not encode compiler/toolchain setup directly into test commands or GitHub workflow YAML.

## File Structure

- `chttp/include/chttp/chttp.h` — public JWT validator options and dedicated server/route/WebSocket JWT binding APIs.
- `chttp/src/chttp_jwt.c` / `chttp/src/chttp_jwt_internal.h` — HS256 key policy, Bearer parsing, deterministic JWT verification, uniform 401 helper, request-owned claims.
- `chttp/src/chttp_server_internal.h` / `chttp/src/chttp_server_parser.c` — private headers-complete parser admission callback and stop-before-body semantics.
- `chttp/src/chttp_server_runtime.h` — one admitted route/policy state shared by HTTP/1.1 and HTTP/2.
- `chttp/src/chttp_server_route.c` — server-wide, HTTP route, and WebSocket JWT control-plane bindings.
- `chttp/src/chttp_server.c` — HTTP/1.1 admission, `100-continue` ordering, body-open and final dispatch reuse of admitted state.
- `chttp/src/chttp_websocket_server.c` — HTTP/1.1 WebSocket opening reuses the already-admitted route and claims.
- `chttp/src/chttp_h2_server.c` — per-stream admission before body DATA or RFC 8441 opening.
- `chttp/tests/chttp_jwt_test.c` — deterministic JWT/key/Bearer policy tests.
- `chttp/tests/chttp_server_parser_test.c` — parser stop-before-continue/body contract.
- `chttp/tests/chttp_server_test.c` — HTTP/1.1 route/global/streaming/100-continue integration.
- `chttp/tests/chttp_websocket_test.c` — HTTP/1.1 protected WebSocket admission.
- `chttp/tests/chttp_h2_server_test.c` — HTTP/2 and RFC 8441 protected-stream admission and reuse.
- `chttp/README.md` — supported JWT binding/lifetime/security contract and migration away from ordinary JWT middleware.

---

### Task 1: Harden the HS256 and Bearer Validation Primitive

**Files:**
- Modify: `chttp/include/chttp/chttp.h`
- Modify: `chttp/src/chttp_jwt.c`
- Modify: `chttp/src/chttp_jwt_internal.h`
- Modify: `chttp/tests/chttp_jwt_test.c`

**Interfaces:**
- Consumes: existing `chttp_jwt_hs256_token_create()`, `chttp_jwt_bearer_validator_init()`, private CJWT decode, `chttp_server_request_state` ownership.
- Produces:
  - `int allow_missing_exp` appended to `chttp_jwt_bearer_validator_options`.
  - private `int chttp_jwt_bearer_request_validate_at(chttp_server_request_state *state, const chttp_server_request_view *request, chttp_jwt_bearer_validator *validator, int64_t now_seconds);`
  - existing `chttp_jwt_bearer_request_validate()` becomes the wall-clock wrapper used by server admission.

- [ ] **Step 1: Replace short test secrets and add key-size RED coverage**

Use one canonical 32-byte secret everywhere in JWT tests and retain a 31-byte negative fixture:

```c
static const unsigned char valid_key[] = "0123456789abcdef0123456789abcdef";
static const unsigned char short_key[] = "0123456789abcdef0123456789abcde";

it("rejects HS256 secrets shorter than 32 bytes") {
  chttp_jwt_claims claims = {.subject = "alice", .expires_at = INT64_C(3000000000)};
  chttp_jwt_bearer_validator validator = {0};
  chttp_jwt_bearer_validator_options options = {
      .size = sizeof(options), .key = short_key, .key_size = sizeof(short_key) - 1u};
  char *token = NULL;

  check_equal(chttp_jwt_hs256_token_create(&claims, short_key, sizeof(short_key) - 1u, &token),
              SALTS_EINVAL);
  check_null(token);
  check_equal(chttp_jwt_bearer_validator_init(&validator, &options), SALTS_EINVAL);
  check_null(validator.impl);
}

it("accepts a 32 byte HS256 secret") {
  chttp_jwt_claims claims = {.subject = "alice", .expires_at = INT64_C(3000000000)};
  char *token = NULL;
  check_equal(chttp_jwt_hs256_token_create(&claims, valid_key, sizeof(valid_key) - 1u, &token),
              SALTS_OK);
  check_not_null(token);
  chttp_jwt_token_destroy(token);
}
```

Update the existing `client-server-test-key` and `server-test-key` fixtures before running the suite so unrelated happy paths do not fail for the new policy.

- [ ] **Step 2: Run the key-size tests to prove RED**

Run on Windows:

```bash
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target chttp_jwt_test
build\Msvc-Release\bin\chttp_jwt_test.exe --filter "HS256 secret"
```

Expected: the 31-byte case fails because current production code accepts any non-empty key.

- [ ] **Step 3: Enforce the 32-byte minimum in both issuance and validator initialization**

Add one private constant and use it at both public entry points:

```c
enum { CHTTP_JWT_HS256_MIN_KEY_BYTES = 32u };

if (claims == NULL || key == NULL || key_size < CHTTP_JWT_HS256_MIN_KEY_BYTES ||
    out_token == NULL)
  return SALTS_EINVAL;
```

and:

```c
if (validator == NULL || options == NULL || options->size != sizeof(*options) ||
    options->key == NULL || options->key_size < CHTTP_JWT_HS256_MIN_KEY_BYTES ||
    options->clock_skew_seconds < 0)
  return SALTS_EINVAL;
```

- [ ] **Step 4: Add deterministic missing-exp, expiration, skew, and `nbf` RED tests**

Append `allow_missing_exp` to the public options struct:

```c
typedef struct chttp_jwt_bearer_validator_options {
  size_t size;
  const void *key;
  size_t key_size;
  int64_t clock_skew_seconds;
  const char *expected_issuer;
  const char *expected_audience;
  int allow_missing_exp;
} chttp_jwt_bearer_validator_options;
```

Declare the private deterministic entry point in `chttp_jwt_internal.h`:

```c
int chttp_jwt_bearer_request_validate_at(chttp_server_request_state *state,
                                         const chttp_server_request_view *request,
                                         chttp_jwt_bearer_validator *validator,
                                         int64_t now_seconds);
```

Use a bare request state because this helper only owns/destroys the decoded CJWT object:

```c
it("requires exp by default and uses strict expiration boundaries") {
  chttp_server_request_state state = {0};
  chttp_jwt_bearer_validator validator = {0};
  chttp_jwt_bearer_validator_options options = {
      .size = sizeof(options),
      .key = valid_key,
      .key_size = sizeof(valid_key) - 1u,
      .clock_skew_seconds = 0};
  chttp_jwt_claims claims = {.subject = "alice", .expires_at = INT64_C(200)};
  char *token = NULL;
  char authorization[512];
  chttp_header header = {0};
  chttp_server_request_view request = {0};

  check_equal(chttp_jwt_bearer_validator_init(&validator, &options), SALTS_OK);
  check_equal(chttp_jwt_hs256_token_create(&claims, valid_key, sizeof(valid_key) - 1u, &token),
              SALTS_OK);
  check_equal(chttp_jwt_bearer_header(token, authorization, sizeof(authorization), &header),
              SALTS_OK);
  request.headers = &header;
  request.header_count = 1u;
  check_equal(chttp_jwt_bearer_request_validate_at(&state, &request, &validator, 199), SALTS_OK);
  chttp_jwt_request_state_reset(&state);
  check_equal(chttp_jwt_bearer_request_validate_at(&state, &request, &validator, 200), SALTS_EPERM);

  chttp_jwt_token_destroy(token);
  chttp_jwt_bearer_validator_destroy(&validator);
}
```

Add three adjacent cases with the same helper:

```c
/* Missing exp is rejected when allow_missing_exp == 0. */
claims = (chttp_jwt_claims){.subject = "alice"};

/* The same token is accepted by a validator initialized with .allow_missing_exp = 1. */

/* With exp=200 and skew=5: now=204 accepts, now=205 rejects. */

/* With nbf=200 and skew=0: now=199 rejects, now=200 accepts. */
```

For each case, create a fresh token/validator, call the deterministic helper, reset request state after any successful decode, and destroy owned token/validator objects.

- [ ] **Step 5: Run the deterministic policy tests to prove RED**

```bash
cmake --build --preset win-release-user --target chttp_jwt_test
build\Msvc-Release\bin\chttp_jwt_test.exe --filter "requires exp|expiration|nbf"
```

Expected: compilation first fails because `allow_missing_exp` and `chttp_jwt_bearer_request_validate_at()` do not exist; after declarations are present but before policy implementation, missing-exp and exact-exp assertions fail.

- [ ] **Step 6: Implement deterministic validation and strict expiration without weakening CJWT checks**

Copy `allow_missing_exp` into the immutable validator impl and make wall-clock validation delegate:

```c
int chttp_jwt_bearer_request_validate(chttp_server_request_state *state,
                                      const chttp_server_request_view *request,
                                      chttp_jwt_bearer_validator *handle) {
  const time_t now = time(NULL);
  if (now == (time_t)-1) return SALTS_EPERM;
  return chttp_jwt_bearer_request_validate_at(state, request, handle, (int64_t)now);
}
```

After successful CJWT decode and the existing explicit `jwt->header.alg == alg_hs256` check, add the wrapper policy:

```c
if ((!validator->allow_missing_exp && jwt->exp == NULL) ||
    (jwt->exp != NULL && now_seconds >= *jwt->exp + validator->clock_skew_seconds)) {
  cjwt_destroy(jwt);
  return SALTS_EPERM;
}
```

Do not use the addition literally if it can overflow. Implement the equivalent comparison with checked/saturating arithmetic so `INT64_MAX` timestamps/skew cannot wrap. Leave CJWT's existing `nbf`/`exp` decode checks enabled; this wrapper only tightens missing-exp and equality behavior.

- [ ] **Step 7: Add Bearer grammar and algorithm RED tests**

Use an issued HS256 token to check scheme parsing:

```c
/* Accept mixed-case scheme and multiple literal spaces. */
snprintf(authorization, sizeof(authorization), "bEaReR   %s", token);

/* Reject HTAB as the separator. */
snprintf(authorization, sizeof(authorization), "Bearer\t%s", token);

/* Reject duplicate Authorization headers. */
chttp_header duplicates[2] = {{"Authorization", authorization_one},
                              {"Authorization", authorization_two}};
```

Also add a fixed HS384 token signed by the same 32-byte key; it must still be rejected by the HS256 public validator even though private CJWT can parse HMAC-family algorithms:

```c
static const char hs384_token[] =
    "eyJhbGciOiJIUzM4NCIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJpc3N1ZXIuZXhhbXBsZSIsInN1YiI6ImFsaWNlIiwiYXVkIjoiYXBpLmV4YW1wbGUiLCJpYXQiOjE3MDAwMDAwMDAsIm5iZiI6MTcwMDAwMDAwMCwiZXhwIjozMDAwMDAwMDAwfQ."
    "8J-7L1cH7qM0uSf_ZX6TTWfhaC8DVxAnWrPQRUN8_h_0Wy6HyW_R42W1kPfZ_5hO";
```

Expected result for the HS384 request: `SALTS_EPERM`.

- [ ] **Step 8: Implement the RFC 6750 scheme parser and run the complete JWT target**

Replace exact-prefix parsing with this shape:

```c
if (authorization == NULL) return NULL;
for (index = 0u; index < 6u; ++index)
  if (chttp_jwt_ascii_lower((unsigned char)authorization[index]) !=
      chttp_jwt_ascii_lower((unsigned char)"Bearer"[index]))
    return NULL;
if (authorization[6] != ' ') return NULL;
index = 6u;
while (authorization[index] == ' ') ++index;
return authorization[index] == '\0' ? NULL : authorization + index;
```

Guard short strings before indexing `authorization[0..6]`.

Run:

```bash
cmake --build --preset win-release-user --target chttp_jwt_test
build\Msvc-Release\bin\chttp_jwt_test.exe
```

Expected: PASS.

- [ ] **Step 9: Commit the JWT primitive hardening**

```bash
git add chttp/include/chttp/chttp.h chttp/src/chttp_jwt.c chttp/src/chttp_jwt_internal.h chttp/tests/chttp_jwt_test.c
git commit -m "fix(chttp): harden HS256 bearer validation"
```

---

### Task 2: Add a Headers-Complete Parser Admission Seam

**Files:**
- Modify: `chttp/src/chttp_server_internal.h`
- Modify: `chttp/src/chttp_server_parser.c`
- Modify: `chttp/tests/chttp_server_parser_test.c`

**Interfaces:**
- Consumes: current parser syntactic validation, `on_continue`, `on_upgrade`, `on_body_open`, and existing `return 2` stop-at-headers behavior used by accepted Upgrade.
- Produces:

```c
typedef enum chttp_server_parser_headers_action {
  CHTTP_SERVER_HEADERS_CONTINUE = 0,
  CHTTP_SERVER_HEADERS_STOP
} chttp_server_parser_headers_action;

typedef int (*chttp_server_parser_headers_fn)(
    void *user, const chttp_server_request_view *request,
    chttp_server_parser_headers_action *out_action);
```

with `on_headers` added to `chttp_server_parser_config` before `on_continue`.

- [ ] **Step 1: Write a parser RED proving admission precedes 100/body/request**

Extend the parser probe:

```c
typedef struct chttp_server_parser_probe {
  int headers;
  int continues;
  int body_opens;
  int requests;
  int stop_at_headers;
  /* existing fields remain */
} chttp_server_parser_probe;
```

Add callbacks:

```c
static int chttp_server_parser_test_headers(void *user,
                                            const chttp_server_request_view *request,
                                            chttp_server_parser_headers_action *out_action) {
  chttp_server_parser_probe *probe = user;
  (void)request;
  ++probe->headers;
  *out_action = probe->stop_at_headers ? CHTTP_SERVER_HEADERS_STOP
                                       : CHTTP_SERVER_HEADERS_CONTINUE;
  return SALTS_OK;
}

static int chttp_server_parser_test_body_open(void *user,
                                              const chttp_server_request_view *request,
                                              chttp_body_sink *out_sink) {
  chttp_server_parser_probe *probe = user;
  (void)request;
  ++probe->body_opens;
  *out_sink = (chttp_body_sink){0};
  return SALTS_OK;
}
```

Then add:

```c
it("stops at header admission before 100 continue and body bytes") {
  static const char headers[] = "POST /protected HTTP/1.1\r\n"
                                "Host: example.test\r\n"
                                "Expect: 100-continue\r\n"
                                "Content-Length: 4\r\n\r\n";
  static const char body[] = "data";
  char wire[sizeof(headers) - 1u + sizeof(body) - 1u];
  chttp_server_parser_probe probe = {.stop_at_headers = 1};
  chttp_server_parser_config config = chttp_server_parser_test_config(&probe);
  chttp_server_parser parser = {0};
  size_t consumed = 0u;
  unsigned int http_status = 0u;

  memcpy(wire, headers, sizeof(headers) - 1u);
  memcpy(wire + sizeof(headers) - 1u, body, sizeof(body) - 1u);
  config.on_headers = chttp_server_parser_test_headers;
  config.on_body_open = chttp_server_parser_test_body_open;
  check_equal(chttp_server_parser_init(&parser, &config), SALTS_OK);
  check_equal(chttp_server_parser_execute_consumed(&parser, wire, sizeof(wire), &consumed,
                                                   &http_status), SALTS_OK);
  check_equal(consumed, sizeof(headers) - 1u);
  check_equal(probe.headers, 1);
  check_equal(probe.continues, 0);
  check_equal(probe.body_opens, 0);
  check_equal(probe.requests, 0);
  chttp_server_parser_destroy(&parser);
}
```

- [ ] **Step 2: Run the parser test to prove RED**

```bash
cmake --build --preset win-release-user --target chttp_server_parser_test
build\Msvc-Release\bin\chttp_server_parser_test.exe --filter "header admission"
```

Expected: compilation fails because the headers action/callback do not exist.

- [ ] **Step 3: Add the parser callback types and state**

Add `on_headers` to config/impl and one stop flag:

```c
chttp_server_parser_headers_fn on_headers;
bool headers_stopped;
```

Copy the callback during parser init and clear `headers_stopped` from `chttp_server_parser_reset()`.

- [ ] **Step 4: Separate Expect syntax validation from sending 100 Continue**

Change the private header validation helper to report intent without invoking callbacks:

```c
static int chttp_server_parser_validate_headers(chttp_server_parser_impl *parser,
                                                bool *out_wants_continue) {
  /* existing Host / Transfer-Encoding / Expect syntax checks */
  *out_wants_continue = expect != NULL;
  return 0;
}
```

In `chttp_server_parser_on_headers_complete()`, enforce this order:

```c
bool wants_continue = false;
status = chttp_server_parser_validate_headers(parser, &wants_continue);
if (status == 0 && parser->on_headers != NULL) {
  chttp_server_parser_headers_action action = CHTTP_SERVER_HEADERS_CONTINUE;
  status = parser->on_headers(parser->user, &parser->request, &action);
  if (status != SALTS_OK) return chttp_server_parser_callback_fail(parser, status);
  if (action == CHTTP_SERVER_HEADERS_STOP) {
    parser->headers_stopped = true;
    return 2;
  }
  if (action != CHTTP_SERVER_HEADERS_CONTINUE)
    return chttp_server_parser_callback_fail(parser, SALTS_EPROTO);
}
if (status == 0 && wants_continue) {
  if (parser->on_continue == NULL) return chttp_server_parser_fail(parser, 417u);
  status = parser->on_continue(parser->user);
  if (status != SALTS_OK) return chttp_server_parser_callback_fail(parser, status);
}
/* existing on_upgrade follows, then on_body_open */
```

Generalize the parser's existing accepted-Upgrade stop condition:

```c
if (status == HPE_PAUSED_UPGRADE && (parser->upgrade_stopped || parser->headers_stopped))
  return SALTS_OK;
```

and reject subsequent parser execution with `SALTS_ESHUTDOWN` when either stop flag is set.

- [ ] **Step 5: Preserve existing parser behavior when no admission callback is installed**

Run both the new stop test and the existing continue/upgrade tests:

```bash
build\Msvc-Release\bin\chttp_server_parser_test.exe --filter "header admission|100-continue|Upgrade"
```

Expected: PASS; the existing ordinary request path still emits exactly one 100 Continue and accepted WebSocket Upgrade still preserves coalesced frame bytes.

- [ ] **Step 6: Run the whole parser target and commit**

```bash
build\Msvc-Release\bin\chttp_server_parser_test.exe
git add chttp/src/chttp_server_internal.h chttp/src/chttp_server_parser.c chttp/tests/chttp_server_parser_test.c
git commit -m "feat(chttp): add request header admission seam"
```

Expected: PASS, then one parser-only commit.

---

### Task 3: Make HTTP/1.1 Route Resolution and JWT Admission a Single Pre-Body State Transition

**Files:**
- Modify: `chttp/include/chttp/chttp.h`
- Modify: `chttp/src/chttp_server_runtime.h`
- Modify: `chttp/src/chttp_server_route.c`
- Modify: `chttp/src/chttp_server.c`
- Modify: `chttp/src/chttp_jwt.c`
- Modify: `chttp/src/chttp_jwt_internal.h`
- Modify: `chttp/tests/chttp_server_test.c`

**Interfaces:**
- Consumes: Task 1 validator behavior and Task 2 `on_headers` parser seam.
- Produces:

```c
int chttp_server_use_jwt_bearer(chttp_server *server,
                                chttp_jwt_bearer_validator *validator);

int chttp_server_request_admit(chttp_server_request_state *state,
                               const chttp_server_request_view *request,
                               chttp_method route_method);
```

`chttp_server_request_state` retains `admitted_route`, `admitted_allowed_methods`, `admitted_fallback_status`, `admission_complete`, and `admission_rejected`. `chttp_server_impl` retains the optional server-wide validator pointer.

- [ ] **Step 1: Add HTTP/1.1 RED for middleware ordering and route middleware capacity**

Add a route middleware probe that requires claims:

```c
static int chttp_server_test_jwt_observer(void *user,
                                          const chttp_server_request_view *request,
                                          chttp_server_response *response,
                                          chttp_server_next *next) {
  chttp_server_test_jwt_probe *probe = user;
  (void)response;
  if (request->jwt_claims == NULL || request->jwt_claims->subject == NULL ||
      strcmp(request->jwt_claims->subject, "alice") != 0)
    return SALTS_EPERM;
  ++probe->middleware_calls;
  return chttp_server_next_call(next);
}
```

Create a protected route whose `middleware_count` is already exactly `config.max_route_middleware_count`; registering it through `chttp_server_route_with_jwt_bearer()` must return `SALTS_OK`, proving JWT no longer consumes a route-middleware slot. Send a valid token and require middleware + handler to see `alice`.

- [ ] **Step 2: Add server-wide streaming and 100-continue RED**

Register a streaming POST through ordinary `chttp_server_route_with()`, then install:

```c
check_equal(chttp_server_use_jwt_bearer(&server, &validator), SALTS_OK);
```

For an anonymous request with a body, assert:

```c
check_not_null(strstr(response, "HTTP/1.1 401 Unauthorized"));
check_not_null(strstr(response, "WWW-Authenticate: Bearer"));
check_equal(upload_probe.opens, (size_t)0u);
check_equal(upload_probe.writes, (size_t)0u);
check_equal(upload_probe.handler_called, 0);
check_equal(global_probe.calls, (size_t)0u);
```

For an anonymous `Expect: 100-continue` request, read the final server response and assert:

```c
check_null(strstr(response, "100 Continue"));
check_not_null(strstr(response, "401 Unauthorized"));
check_equal(upload_probe.opens, (size_t)0u);
```

- [ ] **Step 3: Run the HTTP/1.1 RED slice**

```bash
cmake --build --preset win-release-user --target chttp_server_test
build\Msvc-Release\bin\chttp_server_test.exe --filter "JWT|jwt|100-continue"
```

Expected: compilation fails for `chttp_server_use_jwt_bearer`; after adding only the declaration, ordering/capacity/server-wide streaming assertions still fail.

- [ ] **Step 4: Replace request-state JWT body special casing with admitted route state**

In `chttp_server_runtime.h`, remove `jwt_body_rejected` and add:

```c
chttp_server_route_record *admitted_route;
unsigned int admitted_allowed_methods;
unsigned int admitted_fallback_status;
bool admission_complete;
bool admission_rejected;
```

Add to `chttp_server_impl`:

```c
chttp_jwt_bearer_validator *jwt_bearer_validator;
```

Reset all admitted fields from `chttp_server_request_state_reset()` in the same place that destroys `jwt_owner`/claims.

- [ ] **Step 5: Implement the single internal route/admission transition**

Use the explicit `route_method` parameter so RFC 8441 can later resolve CONNECT as a GET WebSocket route:

```c
int chttp_server_request_admit(chttp_server_request_state *state,
                               const chttp_server_request_view *request,
                               chttp_method route_method) {
  chttp_server_route_record *route;
  chttp_jwt_bearer_validator *validator;
  unsigned int allowed_methods = 0u;
  int route_status = SALTS_OK;
  int status;

  if (state == NULL || state->server == NULL || request == NULL || state->admission_complete)
    return SALTS_EINVAL;
  chttp_server_stats_request(state->server);
  route = chttp_server_route_find(state, route_method, request->path, &allowed_methods,
                                  &route_status);
  state->admitted_route = route;
  state->admitted_allowed_methods = allowed_methods;
  state->admitted_fallback_status = allowed_methods != 0u ? 405u : 404u;
  if (route_status == SALTS_ENOBUFS)
    state->admitted_fallback_status = 414u;
  else if (route_status != SALTS_OK)
    return route_status;

  state->admission_complete = true;
  validator = route != NULL && route->jwt_bearer_validator != NULL
                  ? route->jwt_bearer_validator
                  : state->server->jwt_bearer_validator;
  if (validator == NULL) return SALTS_OK;
  status = chttp_jwt_bearer_request_validate(state, request, validator);
  if (status != SALTS_OK) state->admission_rejected = true;
  return status;
}
```

If the existing route lookup mutates parameter state on `SALTS_ENOBUFS`, preserve current fallback behavior exactly; do not add a second lookup.

- [ ] **Step 6: Make route/global JWT bindings control-plane metadata, not middleware**

Change `chttp_server_route_with_jwt_bearer()` so it registers the route normally and sets only:

```c
route->jwt_bearer_validator = validator;
```

Remove the spare middleware-capacity check and do not append `chttp_jwt_bearer_middleware`.

Add:

```c
int chttp_server_use_jwt_bearer(chttp_server *server,
                                chttp_jwt_bearer_validator *validator) {
  chttp_server_impl *impl;
  if (server == NULL || server->impl == NULL || validator == NULL || validator->impl == NULL)
    return SALTS_EINVAL;
  impl = server->impl;
  if (impl->start_called) return SALTS_EBUSY;
  if (impl->jwt_bearer_validator != NULL) return SALTS_EALREADY;
  impl->jwt_bearer_validator = validator;
  return SALTS_OK;
}
```

Remove the public `chttp_jwt_bearer_middleware` declaration and its production implementation. Keep only private validation and unauthorized-response helpers.

- [ ] **Step 7: Wire HTTP/1.1 headers admission before Continue/body-open**

Add `chttp_server_on_headers()` to the parser config:

```c
.on_headers = chttp_server_on_headers,
```

The callback enriches the request, calls `chttp_server_request_admit(state, request, request->method)`, and handles `SALTS_EPERM` by building the existing uniform JWT 401 in the request response builder, serializing that final response into the connection output, setting `close_after_write = true`, and returning `CHTTP_SERVER_HEADERS_STOP` with `SALTS_OK`.

For any non-authentication failure, return the error so the existing parser/server failure path handles it.

When `chttp_server_h1_input()` observes `consumed < size` after a header stop and `connection->close_after_write` is already true, discard the untrusted remainder and return `SALTS_OK`; do not buffer it as deferred/WebSocket input and do not parse body bytes after the final 401 has been chosen.

- [ ] **Step 8: Make body-open and dispatch reuse the admitted route and claims**

`chttp_server_request_body_open()` must require admission and stop doing route lookup/JWT validation:

```c
if (!state->admission_complete || state->admission_rejected) return SALTS_EPERM;
route = state->admitted_route;
if (route == NULL || route->body_open == NULL) return SALTS_OK;
```

The routed body-open request includes claims:

```c
routed_request.jwt_claims = state->jwt_owner != NULL ? &state->jwt_claims : NULL;
```

`chttp_server_dispatch_request()` likewise consumes `state->admitted_route`, `admitted_allowed_methods`, and `admitted_fallback_status`; remove its route lookup and its `chttp_server_stats_request()` call. Build the callback request with:

```c
routed_request.jwt_claims = state->jwt_owner != NULL ? &state->jwt_claims : NULL;
```

Keep the existing global-middleware -> route-middleware -> terminal chain unchanged.

- [ ] **Step 9: Prove server-wide built-in fallback ordering and request reuse**

Add two HTTP/1.1 integration assertions:

```c
/* Anonymous unknown path under server-wide JWT: 401 and global middleware not called. */
/* Authorized unknown path under server-wide JWT: global middleware runs, then normal 404. */
```

Then use one keep-alive connection for two sequential requests: a valid protected request followed by a public/request without a route-specific validator in a server configured without global JWT. The second callback must observe `request->jwt_claims == NULL`, proving request reset cleared prior identity.

- [ ] **Step 10: Run the complete HTTP/1.1 server slice**

```bash
cmake --build --preset win-release-user --target chttp_jwt_test chttp_server_parser_test chttp_server_test
ctest --preset win-release-user -R "^(chttp_jwt_test|chttp_server_parser_test|chttp_server_test)$" --output-on-failure
```

Expected: PASS.

- [ ] **Step 11: Commit the HTTP/1.1 admission architecture**

```bash
git add chttp/include/chttp/chttp.h chttp/src/chttp_server_runtime.h chttp/src/chttp_server_route.c chttp/src/chttp_server.c chttp/src/chttp_jwt.c chttp/src/chttp_jwt_internal.h chttp/tests/chttp_server_test.c
git commit -m "feat(chttp): authenticate JWT before request body admission"
```

---

### Task 4: Apply the Same Admission Contract to HTTP/1.1 WebSocket Upgrade

**Files:**
- Modify: `chttp/include/chttp/chttp.h`
- Modify: `chttp/src/chttp_server_route.c`
- Modify: `chttp/src/chttp_websocket_server.c`
- Modify: `chttp/tests/chttp_websocket_test.c`

**Interfaces:**
- Consumes: Task 3 admitted-route state, JWT claims, and route-specific validator metadata.
- Produces:

```c
int chttp_server_websocket_with_jwt_bearer(
    chttp_server *server,
    const chttp_server_websocket_options *options,
    chttp_jwt_bearer_validator *validator);
```

- [ ] **Step 1: Add protected HTTP/1.1 WebSocket RED coverage**

Extend the WebSocket server probe with `open_calls` and a copied subject. In the opening callback:

```c
if (request->jwt_claims == NULL || request->jwt_claims->subject == NULL)
  return SALTS_EPERM;
++probe->open_calls;
strncpy(probe->subject, request->jwt_claims->subject, sizeof(probe->subject) - 1u);
```

Register through the new API and test two handshakes:

```c
check_equal(chttp_server_websocket_with_jwt_bearer(&server, &options, &validator), SALTS_OK);

/* Valid Authorization => 101 and probe.open_calls == 1, subject == "alice". */
/* No Authorization => 401 + WWW-Authenticate: Bearer and open_calls remains 1. */
```

- [ ] **Step 2: Run the WebSocket RED**

```bash
cmake --build --preset win-release-user --target chttp_websocket_test
build\Msvc-Release\bin\chttp_websocket_test.exe --filter "JWT|jwt|Bearer"
```

Expected: compilation fails because the protected WebSocket registration API does not exist.

- [ ] **Step 3: Add WebSocket JWT registration metadata**

Implement the convenience by registering the WebSocket route first, then setting the new route record's validator:

```c
int chttp_server_websocket_with_jwt_bearer(chttp_server *server,
                                            const chttp_server_websocket_options *options,
                                            chttp_jwt_bearer_validator *validator) {
  chttp_server_impl *impl;
  chttp_server_route_record *route;
  int status;
  if (server == NULL || server->impl == NULL || options == NULL || validator == NULL ||
      validator->impl == NULL)
    return SALTS_EINVAL;
  impl = server->impl;
  status = chttp_server_websocket_route_register(impl, options);
  if (status != SALTS_OK) return status;
  route = &impl->routes[impl->route_count - 1u];
  route->jwt_bearer_validator = validator;
  return SALTS_OK;
}
```

- [ ] **Step 4: Reuse admitted route/claims in the H1 Upgrade path**

`chttp_server_websocket_upgrade()` must not call `chttp_server_route_find()` again. Require `state->admission_complete`, use `state->admitted_route`, and ignore non-WebSocket admitted routes as before.

`chttp_server_websocket_route_open()` publishes the already-verified view:

```c
routed_request.jwt_claims = state->jwt_owner != NULL ? &state->jwt_claims : NULL;
```

Do not perform another JWT decode in the WebSocket code.

- [ ] **Step 5: Run WebSocket plus HTTP server regressions**

```bash
cmake --build --preset win-release-user --target chttp_websocket_test chttp_server_test
ctest --preset win-release-user -R "^(chttp_websocket_test|chttp_server_test)$" --output-on-failure
```

Expected: PASS.

- [ ] **Step 6: Commit H1 WebSocket admission**

```bash
git add chttp/include/chttp/chttp.h chttp/src/chttp_server_route.c chttp/src/chttp_websocket_server.c chttp/tests/chttp_websocket_test.c
git commit -m "feat(chttp): protect websocket admission with JWT"
```

---

### Task 5: Move HTTP/2 Regular Requests to Per-Stream JWT Admission

**Files:**
- Modify: `chttp/src/chttp_h2_server.c`
- Modify: `chttp/tests/chttp_h2_server_test.c`

**Interfaces:**
- Consumes: `chttp_server_request_admit()` from Task 3 and existing H2 response-builder serialization.
- Produces: every initial H2 request header block is admitted once before `chttp_server_request_body_open()` or application DATA delivery.

- [ ] **Step 1: Add HTTP/2 streaming RED coverage**

Create one protected POST route with a body sink and one validator. Send initial H2 headers without Authorization and then DATA. Assert the response is 401 and:

```c
check_equal(stream_probe.opens, (size_t)0u);
check_equal(stream_probe.writes, (size_t)0u);
check_equal(stream_probe.handler_called, 0);
```

Add a valid Authorization request and assert the body-open callback sees:

```c
check_not_null(request->jwt_claims);
check_equal(request->jwt_claims->subject, "alice");
```

- [ ] **Step 2: Run the H2 RED**

```bash
cmake --build --preset win-release-user --target chttp_h2_server_test
build\Msvc-Release\bin\chttp_h2_server_test.exe --filter "JWT|jwt|protected"
```

Expected: unauthenticated H2 body-open currently occurs before JWT middleware/final dispatch, so the zero-open assertion fails.

- [ ] **Step 3: Admit the initial H2 header block before body-open**

At the end of the initial non-trailer header block, after pseudo-header/header validation and after `request = chttp_h2_server_request_view(stream)`, call:

```c
status = chttp_server_request_admit(&stream->request_state, &request, request.method);
```

If it returns `SALTS_EPERM`:

```c
chttp_server_response_builder_reset(&stream->request_state.response_builder);
status = chttp_jwt_bearer_unauthorized_response(&stream->request_state.response);
if (status == SALTS_OK) status = chttp_h2_server_submit_response(stream);
```

Return without opening a body sink. The submitted H2 response must end the response stream; later DATA must not reach application state.

Other admission errors retain the existing stream-error/internal-error mapping.

- [ ] **Step 4: Remove H2 duplicate route/auth work from final dispatch**

Because `chttp_server_dispatch_request()` now requires admitted state and reuses it, do not add any new H2 route lookup before regular dispatch. Ensure `chttp_server_request_body_open()` is invoked only after successful admission.

- [ ] **Step 5: Add two-stream identity isolation proof**

On one H2 connection, send a valid JWT request on stream A and a request to a non-protected route on stream B in a server without global JWT. The stream-B handler asserts `request->jwt_claims == NULL`. Close/reuse stream state and assert a later stream also begins without claims.

- [ ] **Step 6: Run H2 and shared server regressions**

```bash
cmake --build --preset win-release-user --target chttp_h2_server_test chttp_server_test chttp_jwt_test
ctest --preset win-release-user -R "^(chttp_h2_server_test|chttp_server_test|chttp_jwt_test)$" --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit regular H2 admission**

```bash
git add chttp/src/chttp_h2_server.c chttp/tests/chttp_h2_server_test.c
git commit -m "feat(chttp): authenticate HTTP2 streams before body delivery"
```

---

### Task 6: Reuse Admission for RFC 8441 Extended CONNECT

**Files:**
- Modify: `chttp/src/chttp_h2_server.c`
- Modify: `chttp/src/chttp_websocket_server.c`
- Modify: `chttp/tests/chttp_h2_server_test.c`

**Interfaces:**
- Consumes: Task 4 protected WebSocket route metadata and Task 5 H2 per-stream admission.
- Produces: RFC 8441 routes resolve/admit against the GET WebSocket route exactly once and opening callbacks receive verified claims.

- [ ] **Step 1: Add RFC 8441 protected-opening RED coverage**

For an H2 WebSocket route registered with `chttp_server_websocket_with_jwt_bearer()`:

```c
/* Valid Extended CONNECT + Authorization => :status 200 and on_open sees subject alice. */
/* Same Extended CONNECT without Authorization => :status 401 and on_open is not called. */
```

Keep the request's wire method as CONNECT, but the route binding remains the existing GET-based WebSocket route model.

- [ ] **Step 2: Run the RFC 8441 RED**

```bash
cmake --build --preset win-release-user --target chttp_h2_server_test
build\Msvc-Release\bin\chttp_h2_server_test.exe --filter "8441|Extended CONNECT|WebSocket JWT"
```

Expected: the protected H2 WebSocket path does not yet use the admitted JWT route/claims.

- [ ] **Step 3: Admit Extended CONNECT using the GET route method**

When the initial H2 request is a syntactically valid Extended CONNECT candidate, call:

```c
status = chttp_server_request_admit(&stream->request_state, &request, CHTTP_METHOD_GET);
```

Do not first admit it as `CHTTP_METHOD_CONNECT` and do not perform a second `chttp_server_route_find()` inside the WebSocket path.

Use:

```c
route = stream->request_state.admitted_route;
```

for the existing WebSocket-route check/handshake. On JWT rejection, submit the same uniform H2 401 response used by regular protected requests and do not initialize/open the WebSocket peer.

- [ ] **Step 4: Publish claims to the RFC 8441 opening chain**

The H2 WebSocket route-open path must reach `chttp_server_websocket_route_open()` with the admitted request state from the stream. That helper already publishes Task 4 claims; do not decode or copy JWT a second time.

- [ ] **Step 5: Run all WebSocket/H2 tests**

```bash
cmake --build --preset win-release-user --target chttp_h2_server_test chttp_websocket_test
ctest --preset win-release-user -R "^(chttp_h2_server_test|chttp_websocket_test)$" --output-on-failure
```

Expected: PASS for HTTP/1.1 WebSocket and RFC 8441.

- [ ] **Step 6: Commit RFC 8441 parity**

```bash
git add chttp/src/chttp_h2_server.c chttp/src/chttp_websocket_server.c chttp/tests/chttp_h2_server_test.c
git commit -m "feat(chttp): authenticate RFC8441 websocket streams"
```

---

### Task 7: Prove the Uniform Rejection Matrix and Document the New Contract

**Files:**
- Modify: `chttp/tests/chttp_server_test.c`
- Modify: `chttp/tests/chttp_h2_server_test.c`
- Modify: `chttp/tests/chttp_jwt_test.c`
- Modify: `chttp/README.md`

**Interfaces:**
- Consumes: completed H1/H2/WebSocket admission path.
- Produces: one documented public security contract and regression coverage for malformed/wrong tokens without exposing private CJWT details.

- [ ] **Step 1: Add the real-server 401 equivalence matrix**

Against one protected HTTP/1.1 route, send each of these independently:

```text
missing Authorization
malformed compact token
valid token signed with a different 32-byte HS256 key
tampered final signature byte
wrong issuer
wrong audience
duplicate Authorization headers
valid HS384 token signed with the configured secret
expired token
future nbf token
```

For every case assert exactly the public boundary:

```c
check_not_null(strstr(response, "HTTP/1.1 401 Unauthorized"));
check_not_null(strstr(response, "WWW-Authenticate: Bearer"));
check_not_null(strstr(response, "Unauthorized"));
check_equal(protected_probe.calls, expected_success_calls);
```

Do not assert CJWT numeric error codes or different response bodies.

- [ ] **Step 2: Add Bearer success variants at the server boundary**

Take one valid issued token and send:

```text
Authorization: bearer <token>
Authorization: BEARER   <token>
```

Both must reach the route. `Authorization: Bearer\t<token>` must remain 401.

- [ ] **Step 3: Run the rejection matrix**

```bash
cmake --build --preset win-release-user --target chttp_jwt_test chttp_server_test
ctest --preset win-release-user -R "^(chttp_jwt_test|chttp_server_test)$" --output-on-failure
```

Expected: PASS.

- [ ] **Step 4: Rewrite the README JWT section around admission, not middleware**

Document the supported sequence with a concrete public example:

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
chttp_server_use_jwt_bearer(&server, &validator);              /* all routes */
/* or chttp_server_route_with_jwt_bearer(..., &validator); */ /* selected HTTP route */
/* or chttp_server_websocket_with_jwt_bearer(..., &validator); */
```

State explicitly:

```text
- HS256 secrets are at least 32 bytes.
- `exp` is required unless `allow_missing_exp` is explicitly non-zero.
- JWT admission finishes before `100 Continue`, body_open/body DATA, middleware, and handlers.
- Ordinary middleware may read verified request->jwt_claims but is not used to perform JWT admission.
- Claims are callback-scoped and cannot be retained.
- Stop every server using a validator before destroying it.
```

Remove the old recommendation to register `chttp_jwt_bearer_middleware` through `chttp_server_use()`.

- [ ] **Step 5: Run the narrow Windows release checkpoint**

```bash
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target salts_chttp chttp_jwt_test chttp_server_parser_test chttp_server_test chttp_websocket_test chttp_h2_server_test
ctest --preset win-release-user -R "^(chttp_jwt_test|chttp_server_parser_test|chttp_server_test|chttp_websocket_test|chttp_h2_server_test|chttp_requests_test|chttp_tls_test|chttp_cjwt_.*)$" --output-on-failure
```

Expected: all selected tests PASS.

- [ ] **Step 6: Run the equivalent Linux release checkpoint**

```bash
cmake --fresh --preset linux-release-user
cmake --build --preset linux-release-user --target salts_chttp chttp_jwt_test chttp_server_parser_test chttp_server_test chttp_websocket_test chttp_h2_server_test
ctest --preset linux-release-user -R "^(chttp_jwt_test|chttp_server_parser_test|chttp_server_test|chttp_websocket_test|chttp_h2_server_test|chttp_requests_test|chttp_tls_test|chttp_cjwt_.*)$" --output-on-failure
```

Expected: all selected tests PASS under the repository's Linux release preset.

- [ ] **Step 7: Run full CTest on both locally available release presets**

```bash
ctest --preset win-release-user --output-on-failure
ctest --preset linux-release-user --output-on-failure
```

Run the preset(s) available on the executing host; the exact-head GitHub workflow remains responsible for the repository's macOS platform proof. Do not substitute hand-written compiler flags for a missing platform.

- [ ] **Step 8: Commit docs and final regression matrix**

```bash
git add chttp/tests/chttp_jwt_test.c chttp/tests/chttp_server_test.c chttp/tests/chttp_h2_server_test.c chttp/README.md
git commit -m "test(chttp): prove JWT admission security boundary"
```

---

### Task 8: Exact-Head Verification and Scope Audit

**Files:**
- Inspect only: all files changed by Tasks 1-7
- No production change is allowed in this task unless a failing exact-head test proves a defect; any such fix must use a new RED test and a separate commit.

**Interfaces:**
- Consumes: all prior task commits.
- Produces: one exact-head evidence checkpoint suitable for review/PR creation.

- [ ] **Step 1: Audit the diff against the fixed implementation base**

Do not use `HEAD~N`; implementation may legitimately contain extra RED/fix commits. Run:

```bash
git diff --stat 78335820a601f7b024b26e0870a3485921016475..HEAD -- \
  chttp/include/chttp/chttp.h chttp/src chttp/tests chttp/README.md

git diff 78335820a601f7b024b26e0870a3485921016475..HEAD -- \
  chttp/include/chttp/chttp.h chttp/src chttp/tests chttp/README.md
```

Confirm the diff contains no public `kid`, JWK/JWKS, RS/PS/ES/EdDSA, scope/role, OAuth `at+jwt`, revocation, refresh-token, JWE, or generic Authenticator API.

- [ ] **Step 2: Audit all JWT admission call sites**

Run:

```bash
git grep -n "chttp_jwt_bearer_request_validate"
git grep -n "chttp_server_request_admit"
git grep -n "chttp_server_route_find" -- chttp/src/chttp_server.c chttp/src/chttp_h2_server.c chttp/src/chttp_websocket_server.c
```

Expected architectural result:

```text
- wall-clock JWT validation is reached from request admission, not ordinary middleware/body-open/WebSocket callbacks;
- HTTP/1.1 admission is invoked from the headers-complete server callback;
- HTTP/2 admission is invoked once per initial stream header block;
- final body-open/dispatch/WebSocket paths reuse admitted_route instead of selecting a second route for the same request.
```

- [ ] **Step 3: Prove the public unsafe middleware contract is gone**

```bash
git grep -n "chttp_jwt_bearer_middleware" -- chttp/include chttp/src chttp/README.md
```

Expected: no matches.

- [ ] **Step 4: Re-run exact-head narrow tests**

Windows:

```bash
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target salts_chttp chttp_jwt_test chttp_server_parser_test chttp_server_test chttp_websocket_test chttp_h2_server_test
ctest --preset win-release-user -R "^(chttp_jwt_test|chttp_server_parser_test|chttp_server_test|chttp_websocket_test|chttp_h2_server_test|chttp_requests_test|chttp_tls_test|chttp_cjwt_.*)$" --output-on-failure
```

Linux:

```bash
cmake --fresh --preset linux-release-user
cmake --build --preset linux-release-user --target salts_chttp chttp_jwt_test chttp_server_parser_test chttp_server_test chttp_websocket_test chttp_h2_server_test
ctest --preset linux-release-user -R "^(chttp_jwt_test|chttp_server_parser_test|chttp_server_test|chttp_websocket_test|chttp_h2_server_test|chttp_requests_test|chttp_tls_test|chttp_cjwt_.*)$" --output-on-failure
```

Expected: PASS on each locally available platform.

- [ ] **Step 5: Run exact-head full test suite**

```bash
ctest --preset win-release-user --output-on-failure
ctest --preset linux-release-user --output-on-failure
```

Expected: PASS on each locally available platform. Record the exact commit SHA used for the checkpoint; GitHub CI/macOS must run from that same SHA before merge readiness is claimed.

- [ ] **Step 6: Stop at the implementation review gate**

Do not merge automatically. Report:

```text
exact head SHA
narrow CHTTP/JWT test result
full local CTest result
GitHub Linux/macOS/Windows status for the same SHA when available
changed-file scope audit
any remaining review blocker
```

The next capability project after this gate is the separately designed immutable `kid`/key-set + RS256/JWKS layer, not an extension of this implementation plan.