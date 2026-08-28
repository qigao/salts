# CFlowProcess

`TurboUtils::CFlowProcess` combines the existing Core process owner with the
CFlow native byte-pipe Actor. It is a separate target because Core already uses
CFlow internally; placing this adapter in CFlow would create a dependency cycle.

The adapter owns one `turbo_process_t`, three parent-side asynchronous pipe
endpoints, one fixed-capacity native backend, one manual Executor, one IO Actor,
and exactly `request_capacity` operation slots. It never exposes raw endpoints,
captures output into an unbounded buffer, or creates a second process state
machine.

## API contract

`cflow_process_start(process, options, config)` borrows `options` and `config`
only for the call. It rejects Core capture/pipe flags because the adapter must
remain the only standard-stream consumer. `backend_kind`, `request_capacity`,
`command_capacity`, `completion_batch_capacity`, and `completion` are required;
unsupported native pipe backends return `TURBO_ENOTSUP` without fallback.

`try_write_stdin`, `try_read_stdout`, and `try_read_stderr` borrow each buffer
until its terminal callback returns. Accepted operations receive a nonzero
request ID and exactly one `OK`, `EOF`, `CANCELLED`, or `FAILED` completion.
Reads and writes may complete with fewer bytes than requested. Full request or
command capacity is returned as a typed submit/cancel result; storage never
grows after start.

`cflow_process_close_stdin()` returns `TURBO_EBUSY` while an admitted stdin
write remains live. After it succeeds, the child observes EOF and later stdin
submissions return `CLOSED`. `cflow_process_get_stats()` exposes the Actor's
bounded admission counters together with endpoint ownership and close state.

Submission and cancellation inherit the IO Actor's MPSC contract. Exactly one
driver calls `cflow_process_run_ready()`, and callbacks execute on that driver.
Stop and join producers before lifecycle calls. `close()` closes admission,
requests cancellation, and terminates a live child; continue driving until
`is_quiescent()`, then call `destroy()`. No endpoint is closed before its
authoritative operation completion has been delivered and acknowledged.

## Complete lifecycle example

```c
#include <cflow/process.h>

#include <turbo/clock.h>
#include <turbo/error_codes.h>
#include <turbo/thread.h>

#include <stdint.h>

static void completed(void *user, cflow_io_request_id request_id,
                      cflow_io_lease_id lease_id,
                      cflow_process_stream stream,
                      const cflow_io_completion *completion) {
    (void)user;
    (void)request_id;
    (void)lease_id;
    (void)stream;
    (void)completion;
}

int main(void) {
    cflow_process process = {0};
    cflow_process_config config = {0};
    turbo_process_options_t options;
    const uint64_t deadline_ns = UINT64_C(5000000000);
    uint64_t started;
    int status;

    turbo_process_options_init(&options);
#if defined(_WIN32)
    {
        static const char *args[] = {
            "-NoProfile", "-Command", "exit 0", NULL};
        options.program = "powershell.exe";
        options.args = args;
        config.backend_kind = CFLOW_IO_NATIVE_IOCP;
    }
#else
    {
        static const char *args[] = {NULL};
        options.program = "/usr/bin/true";
        options.args = args;
        config.backend_kind = CFLOW_IO_NATIVE_POLL;
    }
#endif
    options.flags = 0u;
    config.request_capacity = 4u;
    config.command_capacity = 4u;
    config.completion_batch_capacity = 4u;
    config.completion = completed;

    status = cflow_process_start(&process, &options, &config);
    if (status != TURBO_OK) return 1;
    status = cflow_process_close(&process);
    if (status != TURBO_OK) return 2;

    started = turbo_hrtime();
    while (!cflow_process_is_quiescent(&process)) {
        size_t progressed = 0u;
        status = cflow_process_run_ready(&process, 32u, &progressed);
        if (status != TURBO_OK) return 3;
        if (turbo_hrtime() - started > deadline_ns) return 4;
        if (progressed == 0u) turbo_sleep_ms(1u);
    }
    return cflow_process_destroy(&process) == TURBO_OK ? 0 : 5;
}
```

Build consumers with:

```cmake
find_package(TurboUtils CONFIG REQUIRED)
target_link_libraries(app PRIVATE TurboUtils::CFlowProcess)
```

The cross-platform capability matrix, state machines, ownership proofs, and
rollback boundary are recorded in
[`../docs/superpowers/specs/2026-08-28-cflow-pipe-rendezvous-subprocess-design.md`](../docs/superpowers/specs/2026-08-28-cflow-pipe-rendezvous-subprocess-design.md).
