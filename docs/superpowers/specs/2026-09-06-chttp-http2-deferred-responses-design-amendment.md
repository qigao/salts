# CHTTP HTTP/2 Deferred Response Design Amendment

## Status

This amendment is normative for
`docs/superpowers/specs/2026-09-06-chttp-http2-deferred-responses-design.md` at
commit `7919744358c7f01f44491c5be6b9e952a5dd3da0`.

It corrects only the downstream asynchronous-work admission ordering. All other
design decisions in the approved specification remain unchanged.

## Problem

The original downstream TurboFlow example ordered ownership as:

```text
copy request fields
  -> attempt graph admission
  -> on graph admission failure, send synchronous 429/503
  -> on graph admission success, call chttp_server_response_defer()
```

That ordering is not ownership-safe once HTTP/2 deferred controls are bounded.
`chttp_server_response_defer()` may legitimately return `SALTS_ENOBUFS` when all
H2 deferred controls for the physical connection are occupied by live or
canceled-but-not-yet-terminalized leases. If graph admission has already
succeeded, a subsequent defer failure leaves accepted graph work without a
CHTTP response lease.

The implementation must not rely on an application being able to roll back an
already-admitted graph job, and #214 does not add such a coupling.

## Correct ownership order

The CHTTP deferred lease is the HTTP-side admission token and must be acquired
before asynchronous application work is admitted:

```text
CHTTP handler
  -> copy graph-owned request fields
  -> chttp_server_response_defer()
       -> failure: no async work was admitted; return an ordinary synchronous
          bounded error while the builder remains usable when the defer call
          did not publish a lease
       -> success: CHTTP now owns one generation-checked deferred lease
  -> attempt graph admission
       -> success: transfer graph-owned request data to the graph and retain
          only owned graph state + chttp_server_deferred
       -> failure: immediately terminalize the deferred lease from the handler
          with chttp_server_deferred_reply() using the chosen 429/503 response
  -> return from the handler
```

The important ownership rule is:

> No asynchronous graph work may become accepted unless the handler already
> owns a valid CHTTP deferred lease that can represent that work's eventual
> terminal HTTP response.

## Defer failure semantics

`chttp_server_response_defer()` publishes the handle only after its complete
lease admission succeeds. Therefore `SALTS_ENOBUFS` or another pre-publication
failure means no deferred lease exists and no asynchronous graph work may be
admitted from that request.

The implementation plan must preserve the existing failure-atomic rule: an
unsuccessful `defer()` does not partially publish a handle or seal the response
builder unless an earlier terminal response operation had already done so.

## Graph-admission failure after successful defer

After a successful `defer()`, the handler response builder is sealed. If graph
admission then rejects the work, the handler must not call
`chttp_server_reply()` on that sealed builder. It instead constructs a bounded
`chttp_server_deferred_response` for the selected 429/503 result and calls:

```c
status = chttp_server_deferred_reply(&deferred, &rejected_response);
```

This call is valid from the handler thread even though the API is thread-safe
for worker use. On success it consumes the handle and transfers the response to
CHTTP; the server owner submits it after the callback unwinds. This is an
immediate terminalization of the async lease, not an HTTP/1.1 fallback and not a
synchronous-execution fallback.

If this immediate terminalization itself returns a retryable bounded-copy error,
the application still owns the deferred handle and must complete that lease via
its normal bounded cleanup/error path before successful server stop. The caller
must not discard the handle.

## No new public API

This correction does not introduce:

- a deferred preflight API;
- a public deferred abandon/cancel API;
- a graph rollback contract;
- extra deferred capacity;
- protocol fallback.

The existing generation-checked `chttp_server_deferred` and
`chttp_server_deferred_reply()` remain sufficient.

## Downstream issue implication

TurboFlow #7 should update its implementation ordering to reserve the CHTTP
lease before graph admission. Its existing requirement that graph admission
failure produce a bounded 429/503 remains valid; only the ownership mechanism
changes from ordinary synchronous reply to immediate completion of the already
reserved deferred lease.

## Verification consequence

The #214 implementation plan must include an integration test that proves:

1. a handler successfully obtains an H2 deferred lease;
2. simulated application/graph admission then rejects;
3. the handler immediately completes that lease with a 503 deferred response;
4. the client receives exactly one 503 response;
5. no deferred control remains occupied afterward;
6. a sibling H2 stream on the same connection remains usable.
