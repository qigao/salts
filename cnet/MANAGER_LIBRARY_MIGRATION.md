# CNet manager and handoff library consolidation

Tracking: [umbrella #1050](https://github.com/qigao/salts/issues/1050).
This is a **breaking native linking change** and is not claimed as a released SDK until
exact-head native, installed consumer and downstream validation succeeds.

## Target and headers

```cmake
find_package(Salts CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE Salts::CNet)
```

```c
#include <cnet/cnet.h>
#include <cnet/manager.h>
#include <cnet/handoff.h>
```

Manager and handoff are **opt-in runtime instances** within the single production
`Salts::CNet` DSO. Ordinary raw CNet usage allocates no manager/handoff records and
adds no data-path callback indirection. The separate exported library and CMake
target `Salts::CNetManager` are removed; no compatibility alias/DLL is supplied.

Rebuild all native consumers that linked the old shared library. Clear old SDK
prefixes; installing the new version over an old SDK may leave stale binaries.
The API names and public structs of manager/handoff are unchanged.

## Ownership invariants

- One owner per CNet client; manager borrows that client and never polls/stops it.
- Manager record/connection credits are distinct from CNet transport credits.
- Handoff reserves/publishes a detached stream and transfers ownership only on
  successful publication; a failed adopt consumes the stream per its documented
  contract.
- `hold_context` is separate from transport lifetime. CNet terminal never
  fabricates application completion.
- Incarnation counters are owned by one production DSO, shared across consumers
  and independent of diagnostics/profile static archives.
- No Actor, CFlow, worker, retry or Client pool is introduced by this change.

## Qualification

Run in-tree `cnet_manager_test`, `cnet_handoff_test`,
`cnet_manager_link_test`, `cnet_manager_link_cpp_test` and native CNet
regressions. Install a fresh SDK, verify it exports only `Salts::CNet` for the
manager/handoff symbols, and build C11/C++17 out-of-tree consumers. Follow with
CHttp and FlowMQ rebuilt against the **same actual SDK candidate**. Do not
claim build-only Android/iOS as device execution.
