# CNet WebSocket Engine Implementation Plan

> Execute in this worktree with test-driven development. Each production change
> follows a failing focused test, then the smallest implementation, then focused
> and adjacent regression verification.

**Goal:** Add a bounded RFC 6455 post-handshake session engine to CNet using the
in-repository `tools/wsparser` frame parser without changing existing CNet
transport behavior.

**Architecture:** `tools/wsparser` is a private object target embedded in
`turbo_cnet`. `<cnet/websocket.h>` exposes only CNet value types and callbacks.
`cnet_websocket.c` owns fixed-capacity input, reassembly, and one-frame output
buffers and never parses HTTP.

## Task 0: Reuse the repository URI parser in CNet

**Files:**

- Modify `parser/uri_parser/parser/uri_lexer.re`
- Modify `parser/uri_parser/test/test_uri_parser.c`
- Modify `CMakeLists.txt`, `parser/CMakeLists.txt`, `cnet/CMakeLists.txt`
- Modify `cnet/src/cnet_uri.c`, `cnet/tests/cnet_uri_test.c`

1. Add failing parser tests proving component overflow and empty ports are
   rejected rather than truncated or normalized.
2. Harden the generated grammar actions with pre-copy bounds and strict port
   syntax; run the owning parser tests.
3. Move the independent UriParser target before CNet in build order and link it
   privately from CNet.
4. Replace CNet tokenization with `uri_parse()` plus CNet-only scheme, component,
   capacity, and port policy; preserve existing valid network/IPv6/Pipe tests.
5. Run UriParser, CNet URI, installed export, and CNet API regression tests.

## Task 1: Parser corpus RED/GREEN

**Files:**

- Add `tools/wsparser/CMakeLists.txt`
- Add/harden `tools/wsparser/websocket_frame_parser.[ch]`
- Add `cnet/tests/cnet_websocket_parser_test.c`
- Modify `tools/CMakeLists.txt`, `cnet/tests/CMakeLists.txt`

1. Add tests for NULL arguments, reserved opcodes/RSV, control constraints,
   non-canonical lengths, high-bit/overflow rejection, borrowed payload, mask
   unmask, and checked header construction.
2. Build the focused target and record the expected compile/behavior failure.
3. Import the parser and implement the validation in one shared decode path.
4. Rebuild until the parser corpus passes.

## Task 2: Public session contract RED

**Files:**

- Add `cnet/include/cnet/websocket.h`
- Add `cnet/tests/cnet_websocket_test.c`
- Modify `cnet/tests/cnet_header_cpp_test.cpp`

1. Write tests for explicit-limit initialization, server/client mask direction,
   split and coalesced frames, text/binary delivery, and C++ layout.
2. Add fragmentation with interleaved Ping, automatic Pong, outbound client
   masking, and single pending-output backpressure tests.
3. Add invalid UTF-8, oversize frame/message, invalid Close payload/code,
   Close echo, abrupt transport close, callback reentrancy, and destroy tests.
4. Build and record the expected missing-header/symbol failure.

## Task 3: Session engine GREEN

**Files:**

- Add `cnet/src/cnet_websocket.c`
- Modify `cnet/CMakeLists.txt`

1. Implement fixed-capacity buffers, checked configuration validation, and
   single-owner/reentrancy guards.
2. Implement bounded frame admission, role masking, secure client mask keys,
   parser error mapping, and one-frame output retry.
3. Implement inbound/outbound fragmentation, strict UTF-8 validation, control
   interleaving, ping/pong, Close validation/echo, terminal first-error state,
   and transport-close notification.
4. Run parser/session/header tests after every behavior group.

## Task 4: Documentation and package contract

**Files:**

- Modify `cnet/README.md`
- Modify `docs/CHTTP_CNET_PROTOCOL_TODO.md`
- Modify the current book chapter capability matrix/text
- Modify installed C/C++ consumers if the new header is part of package checks

Document that the engine is available but `ws://`/`wss://` Upgrade and CHTTP
routes remain future adapters. Mark only the completed parser/session checklist
items; retain URI/Upgrade/WSS work as incomplete.

## Task 5: Verification and integration

1. Format changed C/C++ sources and run `git diff --check`.
2. Run focused Release parser/session/header tests and repeat the previously
   observed `cnet_shards_test` baseline race test.
3. Run all `^cnet_`, then adjacent CHTTP/CRPC tests, installed-package consumer,
   and full Release CTest.
4. Run focused ASan tests using the public development preset.
5. Sync CodeGraph, ensure `.codegraph/` is untracked, review the diff, then
   commit/push/open a PR. Merge only after local and CI evidence is green.
