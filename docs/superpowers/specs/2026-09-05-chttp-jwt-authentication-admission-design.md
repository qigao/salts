# CHTTP JWT Authentication Admission and Hardening Design

## Decision

CHTTP moves JWT Bearer verification out of the ordinary middleware chain and
into a request-admission phase that runs after request headers and route
resolution, but before `100 Continue`, application body admission, ordinary
middleware, and terminal handler/WebSocket dispatch.

This correction keeps the existing small public JWT boundary and does not yet
introduce a generic public Authenticator abstraction. JWT is the first concrete
authentication mechanism that proves the phase. A future second mechanism such
as mTLS principal mapping or API-key authentication may justify extracting a
generic public authentication interface, but this change does not speculate on
that API.

The first hardening batch also makes HS256 reject secrets shorter than 32 bytes,
requires `exp` by default for JWT Bearer validation, preserves strict HS256
algorithm selection, and accepts the RFC 6750 `Bearer` scheme with one or more
SP characters between scheme and credentials.

## Problem and evidence

The current JWT implementation is functionally complete for a minimal HS256
happy path: CHTTP can issue an HS256 JWT, build an Authorization header, copy an
immutable validator, validate signature/time/issuer/audience, and expose a
callback-scoped verified claims view.

The problem is where authentication currently sits in the server lifecycle.
`chttp_server_route_with_jwt_bearer()` registers the caller's route middleware
first and then appends `chttp_jwt_bearer_middleware`. Therefore route middleware
can execute before authentication and cannot rely on `request->jwt_claims`.

Streaming requests expose a stronger violation. The current body-open path has
a JWT pre-check only when a route was registered through
`chttp_server_route_with_jwt_bearer()`. A caller may also register
`chttp_jwt_bearer_middleware` globally through `chttp_server_use()`, as the
README currently documents, but ordinary global middleware executes during
final dispatch, after the parser may already have opened an application body
sink and delivered body bytes.

HTTP/1.1 `Expect: 100-continue` makes the lifecycle boundary explicit. The
parser currently validates the Expect header and invokes `on_continue` before
`on_body_open`. Authentication performed only from body-open or later is too
late: an unauthenticated request can receive `100 Continue` before the server
has decided whether the request is admitted.

The invariant must therefore be stronger than "JWT middleware runs first":

> A request that requires authentication is authenticated exactly once after
> headers and route selection, before the server invites or admits request-body
> bytes to application code, and before any ordinary middleware or terminal
> callback observes the request.

## Candidate comparison

### Keep JWT as middleware and add more special pre-checks

Rejected. CHTTP could special-case global JWT middleware in body-open, preserve
the current route pre-check, and add another special case around
`100 Continue`. This duplicates verification across parser/body/dispatch paths,
requires identifying one callback value as semantically special, and leaves no
single authentication source of truth. HTTP/2 and WebSocket admission would
need parallel exceptions.

### Require callers to place JWT first in every middleware list

Rejected. Ordering documentation does not solve streaming or
`Expect: 100-continue`, because ordinary middleware still runs after body
admission. It also makes a security invariant depend on caller discipline.

### Add a JWT-backed request-admission phase

Selected. Route resolution and effective JWT policy selection happen once when
headers are complete. JWT verification populates request state before the
server permits `100 Continue`, calls a body sink opener, enters ordinary
middleware, or opens a WebSocket. HTTP/1.1 connections and HTTP/2 streams use
the same request-state contract.

This is deliberately narrower than a generic authentication framework. The
phase is architectural; the only public authentication binding in this change
is JWT Bearer.

## Request admission lifecycle

For each HTTP request or HTTP/2 stream, CHTTP performs these logical phases:

1. Parse and syntactically validate request line/pseudo-headers, headers, body
   framing, target, and protocol requirements.
2. Resolve the route and method outcome once, including named parameters and
   the built-in 404/405 outcome.
3. Select the effective JWT Bearer validator, if any.
4. Validate the Authorization header and JWT exactly once.
5. Publish verified `request->jwt_claims` into request-scoped state.
6. If the request uses `Expect: 100-continue`, send `100 Continue` only after
   admission succeeds.
7. Open the application body sink, if the admitted matched route has one, and
   deliver body bytes subject to existing configured bounds.
8. Run ordinary global middleware and then ordinary route middleware.
9. Invoke the terminal HTTP handler or WebSocket opening callback.
10. Clear the admitted route, route parameters, JWT owner/claims, body state,
    and response state before request/stream reuse.

Authentication failure short-circuits steps 6 through 9. Application
`body_open`, body sink callbacks, ordinary middleware, HTTP handlers, and
WebSocket opening callbacks are never called for a request rejected by JWT
admission.

For an HTTP/1.1 request carrying `Expect: 100-continue`, a JWT rejection must
produce the final 401 without first sending an interim 100 response. The
connection may be closed after that final response rather than draining an
untrusted body that the client has not yet been invited to send.

For a rejected HTTP/1.1 request whose body bytes are already in flight, CHTTP
may parse/discard bounded bytes as required for protocol cleanup, but no
application sink may observe them. For HTTP/2, rejected DATA is stream-scoped;
the implementation may end or reset that stream after the 401 without
impacting unrelated streams. These transport details must not weaken the
application-level admission invariant.

## Route resolution and request state

Header admission becomes the single route-resolution point. The request state
retains the resolved route pointer (or built-in 404/405 outcome) and its copied
named parameters through body admission and final dispatch. Body-open and final
dispatch reuse that admitted result rather than performing an independent route
lookup.

This is important for both correctness and security: the route whose JWT policy
was evaluated is exactly the route whose body sink, middleware, and handler are
later invoked.

The per-request/per-stream state remains the owner of the decoded private CJWT
object. `chttp_jwt_claims_view` continues to borrow pointers from that owner and
is valid only for callbacks belonging to the admitted request. Reset destroys
the private CJWT object before slot or HTTP/2 stream reuse.

## JWT binding model

CHTTP provides dedicated JWT registration surfaces rather than asking callers
to install JWT as ordinary middleware:

- `chttp_server_use_jwt_bearer(server, validator)` installs one server-wide
  default JWT Bearer policy before start.
- `chttp_server_route_with_jwt_bearer(server, options, validator)` protects one
  HTTP route with a route-specific JWT Bearer policy.
- `chttp_server_websocket_with_jwt_bearer(server, options, validator)` protects
  one HTTP/1.1 or RFC 8441 WebSocket opening with a route-specific JWT Bearer
  policy.

A route-specific validator overrides the server-wide default for that matched
route. A route with no route-specific validator inherits the server-wide
default. If no validator is selected, the request is unauthenticated and the
ordinary route lifecycle is unchanged.

A server-wide JWT policy intentionally has no per-route public opt-out in this
change. Applications that mix public and protected routes should omit the
server-wide policy and bind validators only to protected routes. This keeps the
security model explicit and avoids an accidental "public" escape hatch.

`chttp_jwt_bearer_middleware` is no longer part of the public application
contract. It may be removed or made private during implementation because an
ordinary middleware callback cannot uphold the pre-body admission invariant.
The README must stop recommending `chttp_server_use(...,
chttp_jwt_bearer_middleware, ...)`.

## Middleware semantics

Ordinary middleware remains an application dispatch mechanism, not an
authentication admission mechanism. Existing ordering remains:

1. admitted identity/claims are already established;
2. global middleware runs in registration order;
3. matched route middleware runs in registration order;
4. the terminal handler or WebSocket opening callback runs.

Therefore every ordinary middleware callback for an authenticated request may
rely on `request->jwt_claims` already being non-NULL and verified. This enables
later authorization middleware for scope/role/policy checks without allowing
that middleware to execute before identity establishment.

Built-in 404/405 handling remains wrapped by ordinary global middleware. When a
server-wide JWT policy is configured, unmatched/405 requests are authenticated
before those global middleware callbacks, preserving the existing "global wraps
built-ins" behavior while preventing body admission before authentication.
Route-specific JWT policy applies only to a successfully matched route.

## HS256 key policy

HS256 issuance and validation require a secret of at least 32 bytes. Both
`chttp_jwt_hs256_token_create()` and
`chttp_jwt_bearer_validator_init()` reject shorter secrets with
`SALTS_EINVAL`.

The current ownership rule remains: validator initialization copies key bytes,
validator destruction cleanses the copied key before release, and the caller
must stop all servers using a validator before destroying it.

CHTTP continues to select HS256 explicitly after CJWT decoding. It does not
accept `alg=none`, HS384/HS512 through the HS256 public API, or asymmetric
algorithms through a symmetric-key validator.

## Bearer token and claim policy

The Authorization parser continues to require exactly one Authorization header
and compares the `Bearer` scheme case-insensitively. It accepts one or more SP
characters after the scheme and requires a non-empty token after those spaces.
Tabs do not substitute for the RFC 6750 SP separator. Duplicate headers,
missing credentials, malformed compact JWTs, wrong signatures, or algorithm
mismatches uniformly fail authentication.

`chttp_jwt_bearer_validator_options` gains an explicit secure-default escape
for legacy/non-expiring JWTs:

- `allow_missing_exp == 0`: `exp` is required and the token is rejected when
  missing.
- `allow_missing_exp != 0`: absence of `exp` is permitted, while a present
  `exp` is still validated normally.

Existing zero-initialized options therefore choose the secure Bearer default.
Generic token creation may continue to omit `exp`; the stricter rule belongs to
Bearer validation, not to JWT serialization itself.

Expiration boundary semantics are strict: after allowed clock skew is applied,
an access token is invalid when current time is equal to or later than its
expiration instant. `nbf` remains valid at its exact boundary. The wrapper must
not weaken these semantics merely because the private CJWT implementation uses
a looser equality check.

Issuer and audience expectations remain optional in this correction. Audience
validation continues to accept any member of a JWT audience array that equals
the configured expected audience.

## Error and protocol behavior

Authentication failures remain intentionally uniform to callers and clients:

- server application callbacks receive no partial authentication reason;
- downstream application callbacks are not invoked;
- the HTTP response is `401 Unauthorized`;
- `WWW-Authenticate: Bearer` is present;
- malformed, expired, wrong-key, wrong-issuer, and wrong-audience tokens do not
  produce distinguishable response bodies.

OAuth-specific `invalid_token`, `insufficient_scope`, realm, and scope response
parameters are outside this correction. They can be added when CHTTP declares
an OAuth Bearer profile rather than a generic JWT Bearer mechanism.

## HTTP/1.1, HTTP/2, and WebSocket parity

The same logical admission contract applies to every server transport:

- HTTP/1.1: admission occurs from complete validated headers before
  `100 Continue` and before body-open.
- HTTP/2: admission is per stream after a complete initial header block and
  before a stream body sink is opened or DATA is delivered to application
  code.
- HTTP/1.1 WebSocket Upgrade: JWT admission occurs before ordinary middleware
  and before the opening callback or 101 response.
- RFC 8441 Extended CONNECT: JWT admission occurs on the stream before ordinary
  middleware, the opening callback, or successful 200 handshake response.

A connection or HTTP/2 stream cannot retain `jwt_claims` from a previous
request/stream generation.

## Compatibility and migration

The client helpers remain source-compatible:

- `chttp_jwt_hs256_token_create()`
- `chttp_jwt_token_destroy()`
- `chttp_jwt_bearer_header()`

The validator handle and route convenience remain, with the stronger key and
claim rules documented above. Server applications currently installing
`chttp_jwt_bearer_middleware` directly migrate to the dedicated server-wide or
route/WebSocket JWT registration surface.

Because the JWT public API was introduced immediately before this correction,
correcting the unsafe middleware contract now is preferred over preserving a
newly introduced public shape that cannot satisfy streaming security.

## Verification

Implementation begins with deterministic RED coverage. At minimum the final
suite proves:

- a 31-byte HS256 secret is rejected for issuance and validator init;
- a 32-byte HS256 secret is accepted;
- a valid token reaches body-open, ordinary middleware, and the handler with
  `request->jwt_claims` already populated;
- route middleware registered before the JWT route convenience still observes
  verified claims because JWT is no longer appended as ordinary middleware;
- a server-wide JWT policy rejects an unauthenticated streaming POST before the
  application body-open callback;
- an unauthenticated `Expect: 100-continue` request receives no interim 100 and
  never opens an application body sink;
- HTTP/2 rejects an unauthenticated stream before any application body sink or
  DATA delivery;
- HTTP/1.1 and RFC 8441 WebSocket opening callbacks see verified claims on
  success and are not called on failure;
- missing `exp` is rejected by default and accepted only when
  `allow_missing_exp` is explicit;
- exact expiration boundary, future `nbf`, and configured skew boundaries are
  deterministic;
- wrong key, tampered signature, malformed compact token, wrong issuer, wrong
  audience, duplicate Authorization, and wrong algorithm all produce the same
  401 boundary;
- `Bearer` scheme matching is case-insensitive and one-or-more SP separators are
  accepted;
- request/stream reuse clears claims and cannot expose identity from the prior
  generation.

The narrow JWT/server tests must pass on Linux, macOS, and Windows, followed by
the existing CHTTP HTTP/1.1, HTTP/2, WebSocket, requests, TLS, and CJWT tests.

## Non-goals and follow-up boundary

This correction does not add RS256, PS256, ES256, EdDSA, `kid` key selection,
JWK/JWKS loading or refresh, custom-claim accessors, scope/role authorization,
OAuth `typ=at+jwt`, token revocation/replay storage, refresh tokens, or JWE.

Those remain the next capability layer after the admission invariant is proven.
The preferred next step is an immutable key-set abstraction with `kid`
selection and RS256/JWKS verification. A generic public Authenticator interface
should be considered only when at least one non-JWT authentication mechanism
needs to share this admission phase.