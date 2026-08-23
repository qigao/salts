# CFlow Plan Dense Successor Index Design

## Background

PR #44 measured equivalent representations of one normalized linear CFlow
Graph. The current Graph APIs resolve `out_degree` and `single_successor` by
scanning the complete flat edge array for every visited node. Graph validation
also repeats those scans and performs a recursive DFS that scans all edges at
each node. Plan compilation repeats the topology walk and grows its instruction
tape one `realloc` at a time.

For a linear Graph with `V` nodes and `E = V - 1` edges, compiling an already
normalized Graph therefore includes `Theta(V * E)` topology work before
callback preparation. This control-plane cost is separate from Plan execution:
an already compiled Plan does not query Graph topology.

## Decision

Introduce a CFlow-private dense successor index derived from one immutable
`cflow_subgraph`. It stores one `cflow_node_id` per node, initialized to
`CMETA_INVALID_ID`, and records the lowest node id with more than one outgoing
edge. Construction scans the edge array once after a checked allocation.

The index is used in two places:

- Graph validation derives one index per subgraph, preserves the existing
  validation order, rejects naked fan-out, and checks the single reachable path
  iteratively. This removes repeated edge scans and recursive stack growth.
- Plan capability checking and compilation derive one index for the normalized
  root. The capability walk also counts instructions. The compiler allocates
  the exact instruction tape once and fills it in traversal order.

The index is not added to `cflow_graph` or `cflow_subgraph`. Each caller owns a
short-lived immutable derived view, destroys it before returning, and never
retains it across Graph mutation, normalization, cloning or optimization. The
Graph remains the sole topology fact source.

## Complexity and Memory

For each subgraph, index construction is `Theta(V + E)` time and
`V * sizeof(cflow_node_id)` payload. Validation then walks nodes and the
reachable path in `Theta(V)`. Compiling an already normalized single-path Graph
uses `Theta(V + E)` topology work and an exact `I * sizeof(cflow_plan_inst)`
instruction allocation, where `I` is the number of non-source Plan operations.

All size multiplications are checked before allocation. Index construction is
transactional: failure leaves no owned buffer. An allocation failure remains a
hard error; no flat-scan fallback is introduced.

## Error and Compatibility Contract

Existing public CFlow structures, function signatures, Plan execution behavior,
normalization rules and optimizer semantics do not change. Existing validation
diagnostics and their ordering are preserved for ordinary malformed graphs:
edge endpoint/type errors precede tail, node-contract, fan-out and
cycle/reachability errors. Plan capability remains a boolean query, while Plan
compile reports allocation failure through `plan.error`.

The private index validates edge endpoints when used independently. A malformed
subgraph exceeding the `cflow_node_id` address space is rejected rather than
truncated. The index is single-threaded and read-only after construction; no
new shared mutable state or synchronization is introduced.

## Benchmark Contract

Extend the existing Graph-path Release benchmark with a separate normalized
Graph-to-Plan compilation case at boundary, typical and peak operator counts.
Graph construction and normalization remain outside timing. Each timed sample
creates and destroys a Plan because compilation and its allocations are the
measured behavior. Every sample must succeed and report the expected instruction
count; no wall-clock threshold is used on shared runners.

The benchmark measures only `cflow_plan_compile` for an already normalized
Graph. It does not claim that `cflow_plan_compile_surface` is linear: lowering
and optimization still use the public flat-edge lookup APIs and are outside this
patch.

## Verification

- Unit-test dense index ordering, terminal lookup, fan-out detection, invalid
  endpoints and transactional cleanup.
- Run Graph validation tests, Plan pipeline tests and the complete CFlow test
  set in MSVC Release.
- Run the Release benchmark and compare normalized Graph-to-Plan compilation at
  boundary, typical and peak sizes with PR #44's pre-change path.
- Inspect the public-header and ABI diff to confirm no public Graph or Plan
  layout change.

## Local Release Evidence

The same benchmark-only compile case was run five times at base commit
`24cc405` and five times with this change, on the same Windows host with MSVC
19.44 Release. Values below are medians of each run's average complete Plan
lifecycle in microseconds:

| Operators | Base | Dense index | Speedup |
| ---: | ---: | ---: | ---: |
| 1 | 0.382 | 0.337 | 1.13x |
| 16 | 3.632 | 2.138 | 1.70x |
| 256 | 266.184 | 58.405 | 4.56x |
| 4096 | 57,135.525 | 623.275 | 91.67x |

The 4096-operator median falls from about 57.14 ms to 0.62 ms. Small graphs
remain dominated by fixed allocation and callback-preparation costs. These are
local measurements, not cross-host performance gates; the inherited PR #44
Release matrix supplies the multi-host evidence after push.

## Rollback

Remove the private index source/header, restore the previous validation and Plan
walks, and remove the compile benchmark rows. No persisted data, public format
or consumer migration is involved.
