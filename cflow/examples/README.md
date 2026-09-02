# CFlow native I/O examples

These examples exercise the public bounded native I/O contracts with real host
resources. They are small lifecycle references, not production protocol,
copying, process-management, or benchmarking frameworks.

- `cflow_native_socket_example.c` sends and receives one byte over a loopback
  TCP pair through IOCP on Windows and the explicit poll backend on POSIX.
- `cflow_native_pipe_example.c` transfers one byte through an overlapped named
  pipe pair on Windows and a nonblocking anonymous pipe on POSIX.
- `cflow_native_file_example.c` writes and reads at an explicit offset through
  IOCP on Windows or io_uring on Linux.

All examples use capacity two, bounded driver steps, and a five-second terminal
deadline. They return 77 when the requested native capability is not declared or
cannot be initialized under the host kernel or policy. That result is a CTest
skip with an actionable diagnostic; no example changes backend or starts a
blocking worker as a fallback.

## Build and run

Examples are enabled by the repository `BUILD_EXAMPLES` option and registered as
CTest tests:

```powershell
cmake --preset win-release-user
cmake --build --preset win-release-user --target cflow_native_socket_example cflow_native_pipe_example cflow_native_file_example
ctest --preset win-release-user -R "^cflow_native_.*_example$" --output-on-failure
```

```sh
cmake --preset linux-release-user
cmake --build --preset linux-release-user --target \
  cflow_native_socket_example cflow_native_pipe_example cflow_native_file_example
ctest --preset linux-release-user -R '^cflow_native_.*_example$' --output-on-failure
```

On macOS use `release-mac-ninja`, `build-default-mac`, and
`test-release-mac`. The socket and pipe examples run through explicit poll; the
regular-file example reports that no asynchronous regular-file backend is
declared and exits 77.

`verify_installed_package` compiles these exact three source files against the
installed `Rocida::CFlow` target. It does not substitute separate smoke-test
programs.

## Lifecycle and ownership

The socket and typed-pipe examples make the raw Actor assembly visible:

```text
backend init -> manual Executor init -> I/O Actor init
             -> bounded submit -> drive Executor and Actor
             -> terminal callback -> acknowledge -> release exactly once
             -> close caller-owned handle -> forget backend identity
             -> close and drain Actor -> destroy Actor/backend/Executor
```

The caller owns each handle and buffer through terminal callback return. A
successful submit owns one Actor request until one terminal completion is
acknowledged. The examples require `accepted == acknowledged`, zero active
requests, and one release per operation before closing resources. Once an
endpoint is quiescent, its caller closes it before asking the backend to forget
the retained native identity.

The file example uses the owning `cflow_io_file` facade:

```text
open facade -> bounded write/read-at -> drive -> terminal callbacks
            -> facade acknowledgement and slot release
            -> close -> drain to quiescence -> destroy -> remove temp file
```

It requires two accepted and acknowledged operations, zero active requests, and
zero facade operation slots before close. `destroy` owns the final handle close,
backend identity forget, and execution teardown.

## Host and backend capability evidence

“Example” means the public program runs on that host. “Contract test” means the
platform-specific test exercises the backend directly. A dash is an explicit
unsupported contract, not missing fallback work.

| Host/backend | Socket | Typed byte pipe | Regular file | Hosted evidence |
|---|---|---|---|---|
| Windows / IOCP | Example and contract test | Example and contract test using overlapped named pipes | Example and contract tests for `READ_AT`/`WRITE_AT`; flush is unsupported | Windows release job, full CTest, installed-package consumer |
| Linux / epoll | Contract test | Contract test with nonblocking descriptors | — | Linux release and ASan lifecycle jobs |
| Linux / poll | Example and contract test | Example and contract test | — | Linux release and ASan lifecycle jobs |
| Linux / io_uring | Contract test when execution initialization is permitted | Contract test when execution initialization is permitted | Example and contract test when execution initialization is permitted, including native flush coverage in the contract test | Linux release and ASan lifecycle jobs; unavailable kernel/policy is reported without fallback |
| macOS / kqueue | Contract test | Contract test with nonblocking descriptors | — | macOS 15 release job |
| macOS / poll | Example and contract test | Example and contract test | — | macOS 15 release job and installed-package consumer |

Named-pipe/FIFO rendezvous remains the separate `cflow_io_pipe` control plane.
`cflow_io_pipe_test` covers Windows named-pipe accept/connect and POSIX FIFO
open. The data examples intentionally start from already connected endpoints.

## Resource and failure evidence

| Invariant or failure | Public observation | Authoritative coverage |
|---|---|---|
| Bounded admission and slot reuse | submit result plus fixed request/operation capacity | `cflow_io_native_test`, `cflow_io_file_test`, and the capacity-two examples |
| Exactly-once terminal settle | completion count, acknowledgement count, and release callback count | all three examples and native/file/pipe contract tests |
| Cancellation is authoritative | cancel requests do not reclaim until the native terminal result is observed | `cflow_io_native_test`, `cflow_io_file_test`, and `cflow_io_pipe_test` |
| Close stops admission and drains | quiescence plus zero active requests/slots before destroy | all three examples and facade tests |
| Retained identities are bounded and forgotten | successful `forget_socket`, `forget_pipe`, or facade-owned file forget after handle close | native identity capacity/scope tests and socket/pipe examples |
| Init and malformed-input failure publish no partial object | null/unpublished facade and unchanged output checks | `cflow_io_file_init_failure_test` and native validation tests |
| Process adapter releases OS resources | Windows process handle count or Linux `/proc/self/fd` count returns to baseline across failure and repeated lifecycle cycles | `cflow_process_test` |
| Heap lifetime safety on Linux | AddressSanitizer executes examples plus native, facade, pipe-control, process, and minicoro lifecycle tests | Linux ASan lifecycle job |

These checks establish bounded ownership and cleanup behavior. They are not
throughput or latency measurements, so this document makes no performance claim.
