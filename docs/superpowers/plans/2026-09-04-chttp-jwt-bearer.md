# CHTTP JWT Bearer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide public CHTTP helpers to create HS256 Bearer tokens and verify them in server middleware without exposing CJWT types or ownership.

**Architecture:** Keep CJWT and turbo_crypto private to CHTTP. A public claims view is borrowed only during a server callback; its private CJWT owner is held by the per-request state and released during request reset, including per-stream HTTP/2 state. The immutable validator copies its key and expected issuer/audience at initialization, then may be shared by server callback threads until the caller destroys it after servers stop.

**Tech Stack:** C11, Salts CHTTP, private CJWT, Salts JsonParser, TinyTest, CMake presets.

**Spec:** User request to invoke CJWT for server and client HTTP requests.

## Global Constraints

- CJWT and turbo_crypto remain uninstalled and unexported.
- Only HS256 is exposed in this first public API; `alg=none` and asymmetric/symmetric algorithm confusion remain rejected.
- Token/header inputs are copied by CHTTP request submission; JWT claims views are borrowed and invalid after the server callback returns.
- Authentication failure returns a uniform HTTP 401 with `WWW-Authenticate: Bearer`; it never calls the downstream handler.
- The validator is immutable after init; destroy requires all servers that registered it to have stopped.

---

### Task 1: Define and prove the public client JWT contract

**Files:**
- Modify: `chttp/include/chttp/chttp.h`
- Create: `chttp/tests/chttp_jwt_test.c`
- Modify: `chttp/tests/CMakeLists.txt`

**Interfaces:**
- Produces: `chttp_jwt_hs256_token_create`, `chttp_jwt_token_destroy`, `chttp_jwt_bearer_header` and public input claims.
- Consumed by: private CHTTP JWT implementation and request callers.

- [ ] **Step 1: Write failing tests**

```c
it("creates an HS256 token and formats a copied Bearer header") {
  char *token = NULL;
  char header[512];
  chttp_header value;
  check_equal(chttp_jwt_hs256_token_create(&claims, key, sizeof(key), &token), SALTS_OK);
  check_equal(chttp_jwt_bearer_header(token, header, sizeof(header), &value), SALTS_OK);
  check_equal(value.name, "Authorization");
  check_str_starts_with(value.value, "Bearer ");
  chttp_jwt_token_destroy(token);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset win-release-user --target chttp_jwt_test && build\Msvc-Release\bin\chttp_jwt_test.exe --filter "creates an HS256"`

Expected: compilation fails because the public API does not exist.

- [ ] **Step 3: Add declarations and minimal private token implementation**

```c
int chttp_jwt_hs256_token_create(const chttp_jwt_claims *claims,
                                  const void *key, size_t key_size, char **out_token);
void chttp_jwt_token_destroy(char *token);
int chttp_jwt_bearer_header(const char *token, char *buffer, size_t buffer_size,
                             chttp_header *out_header);
```

- [ ] **Step 4: Run test to verify it passes**

Run: `build\Msvc-Release\bin\chttp_jwt_test.exe --filter "creates an HS256"`

Expected: PASS.

### Task 2: Define and prove server-side Bearer validation

**Files:**
- Modify: `chttp/include/chttp/chttp.h`
- Modify: `chttp/src/chttp_server_runtime.h`
- Modify: `chttp/src/chttp_server.c`
- Create: `chttp/src/chttp_jwt.c`
- Create: `chttp/src/chttp_jwt_internal.h`
- Modify: `chttp/CMakeLists.txt`
- Modify: `chttp/tests/chttp_jwt_test.c`

**Interfaces:**
- Consumes: HS256 token helper and private CJWT target.
- Produces: `chttp_jwt_bearer_validator_init`, `chttp_jwt_bearer_validator_destroy`, `chttp_jwt_bearer_middleware`, and `request->jwt_claims`.

- [ ] **Step 1: Write failing middleware tests**

```c
it("passes verified claims to the terminal route exactly once") {
  /* Send Authorization: Bearer <valid token> through a real CHTTP server. */
  check_equal(observed_status, 200u);
  check_equal(observed_subject, "alice");
}

it("rejects a malformed or wrong-key Bearer token before the route") {
  check_equal(observed_status, 401u);
  check_equal(route_calls, 0u);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `build\Msvc-Release\bin\chttp_jwt_test.exe --filter "verified claims|wrong-key"`

Expected: fail because validator/middleware do not exist.

- [ ] **Step 3: Implement immutable validator and request-owned claims**

```c
typedef struct chttp_jwt_bearer_validator { void *impl; } chttp_jwt_bearer_validator;
int chttp_jwt_bearer_validator_init(chttp_jwt_bearer_validator *validator,
                                     const chttp_jwt_bearer_validator_options *options);
int chttp_jwt_bearer_middleware(void *user, const chttp_server_request_view *request,
                                 chttp_server_response *response, chttp_server_next *next);
```

The validator copies key/issuer/audience. The request state owns the decoded CJWT object; its public claims view points into that object and is cleared before connection/stream reuse.

- [ ] **Step 4: Run middleware tests to verify they pass**

Run: `build\Msvc-Release\bin\chttp_jwt_test.exe --filter "verified claims|wrong-key"`

Expected: PASS.

### Task 3: Verify the integration boundary

**Files:**
- Modify: `chttp/tests/CMakeLists.txt`
- Modify: `vendor/turbo_crypto/README.md`

**Interfaces:**
- Consumes: public CHTTP JWT APIs.
- Produces: a documented private CJWT boundary and CTest coverage.

- [ ] **Step 1: Add the JWT test target and document public/private boundaries**

```cmake
cmake_add_test(chttp_jwt_test
  SOURCES chttp_jwt_test.c
  LIBS salts_chttp Salts::TinyTest
  INCLUDES ../src
  FOLDER "chttp/tests")
```

- [ ] **Step 2: Configure and build the narrow target**

Run: `cmake --fresh --preset win-release-user && cmake --build --preset win-release-user --target salts_chttp chttp_jwt_test`

Expected: configure and link succeed without an exported CJWT target.

- [ ] **Step 3: Run JWT and adjacent CHTTP tests**

Run: `ctest --preset win-release-user -R "^(chttp_jwt_test|chttp_server_test|chttp_requests_test|chttp_cjwt_)" --output-on-failure`

Expected: all selected tests pass.
