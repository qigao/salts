# CFlow Surface-to-Plan Dense Successor Index Design

## Background

The normalized Graph-to-Plan compiler now derives a private dense successor
view and performs linear topology work. `cflow_plan_compile_surface`, however,
first calls `cflow_graph_normalize` and `cflow_graph_optimize`. Both passes walk
each valid single-path DATA subgraph with `cflow_subgraph_single_successor`,
which scans the complete flat edge array for every lookup. The optimizer repeats
that lookup while discovering, copying and accounting for fused MAP chains.

For a linear subgraph with `V` nodes and `E = V - 1` edges, these traversals add
`Theta(V * E)` topology work before the already-linear Plan compiler. This is a
control-plane compile cost; it does not change compiled Plan execution.

## Decision

Reuse the CFlow-private `cflow_dense_successor_index` in normalization and
optimization. Each recursive subgraph pass builds one immutable view after the
public Graph validation succeeds, uses constant-time successor lookup for every
walk over that source subgraph, and destroys the view before returning.

Normalization passes its view through the source subgraph traversal. The
optimizer additionally passes the same view into MAP-chain discovery, callable
copying and canonicalization statistics, so one source subgraph is indexed once
per optimization pass rather than once per helper.

The flat edge array remains the sole topology fact source. No index is retained
in `cflow_graph`, `cflow_subgraph`, normalized output, optimized output or Plan.
No public signature or layout changes.

## Valid Topology and CSR Boundary

Graph validation forbids naked DATA fan-out. Branching semantics are represented
by `RELATION` nodes that own nested subgraphs, and every valid nested subgraph is
itself a single DATA path. A one-successor-per-node view therefore represents
all topology consumed by normalization and optimization today.

General adjacency and CSR remain outside this change. Issue #46 tracks a
separate branching benchmark and representation decision; it must preserve
outgoing-edge order and prove a measured branching benefit before adoption.

## Ownership, Failure and State

The source Graph is borrowed and immutable. The destination Graph remains the
transactional output owned by the caller. Each active recursive pass owns its
index buffer; nested recursion may temporarily own one additional buffer per
active subgraph depth. Every success and failure path destroys the local view.

Index construction performs checked allocation and returns a distinct status.
Allocation failure is reported as a normalization or optimizer index-allocation
error. Invalid edges or fan-out after successful source validation indicate an
internal invariant failure and are rejected explicitly. There is no flat-scan
fallback and no partially trusted continuation.

Existing recursive-subgraph detection, Graph versioning, optimizer trace
commit, stats, destination cleanup and user-visible semantic errors remain in
their current layers.

## Complexity and Memory

For each visited source subgraph, index construction is `Theta(V + E)` time and
`V * sizeof(cflow_node_id)` payload. Normalization then traverses each source
node once, excluding the existing cost of constructing rewritten nodes and
nested Graphs.

Optimization performs a constant number of indexed path walks plus one visit
per copied callable. Its topology work becomes `Theta(V + E)` per subgraph;
MAP-chain storage and copying remain proportional to the number of retained
callables. Peak temporary index payload is the sum of node-id arrays for active
recursive subgraphs, released on unwind.

On the benchmark's single root with 4096 operators, `V = 4097` and
`sizeof(cflow_node_id) = 4`, so each normalize or optimizer view owns
`4097 * 4 = 16,388` bytes (about 16.0 KiB) while that phase is active. The two
phases run sequentially in `compile_surface`; their index buffers do not overlap.

`cflow_plan_compile_surface` still allocates normalized and optimized Graph
snapshots by contract. This patch removes repeated topology scans; it does not
claim zero allocation or eliminate those phase-owned snapshots.

## Benchmark Contract

Extend the existing MSVC Release Graph-path benchmark with separately timed,
complete lifecycles for:

- Surface Graph to normalized Graph (`normalize + destroy`).
- Normalized Graph to optimized Graph (`optimize + destroy`).
- Surface Graph through normalize, optimize and Plan compile (`compile_surface
  + destroy`).
- Already-normalized Graph to Plan, retained as the downstream reference.

Graph fixture construction stays outside timing. Timed samples include only the
phase output allocation and destruction that callers pay. Correctness checks
outside timing require successful validation, expected node/instruction counts
and equivalent Plan compilation. The benchmark covers 1, 16, 256 and 4096
operators and uses no wall-clock threshold on shared runners.

The pre-change Release result is the performance RED evidence. Post-change
results are compared on the same host and executable using medians across five
runs. Scaling and full-pipeline contribution are reported separately so Plan
compiler gains are not attributed to normalization or optimization.

## Compatibility and Verification

- Preserve normalization output, optimizer rewrite/stats/trace behavior and
  Plan instructions using existing structural, idempotence and differential
  pipeline tests.
- Add an out-of-edge-storage-order pipeline characterization that normalizes,
  optimizes and compiles the same valid path.
- Run dense-index unit tests and all CFlow tests in MSVC Release.
- Run the staged Release benchmark before and after the production change.
- Inspect public headers and ABI-sensitive structs; expected public diff is
  empty.

## Local Release Evidence

The staged benchmark was run five times before and five times after the
production change on the same Windows host with MSVC 19.44 Release. Each value
is the median of the five run averages in microseconds. Speedup is calculated
as `baseline / dense-index`; values below `1.00x` are regressions.

| Stage | Operators | Baseline (us) | Dense index (us) | Speedup |
| --- | ---: | ---: | ---: | ---: |
| Normalize | 1 | 0.470 | 0.497 | 0.95x |
| Normalize | 16 | 2.726 | 2.773 | 0.98x |
| Normalize | 256 | 104.180 | 85.730 | 1.22x |
| Normalize | 4096 | 9,130.750 | 1,388.650 | 6.58x |
| Optimize | 1 | 0.533 | 0.566 | 0.94x |
| Optimize | 16 | 3.611 | 3.644 | 0.99x |
| Optimize | 256 | 150.684 | 99.445 | 1.52x |
| Optimize | 4096 | 13,258.050 | 1,545.825 | 8.58x |
| Normalized to Plan | 1 | 0.327 | 0.313 | 1.04x |
| Normalized to Plan | 16 | 1.761 | 1.683 | 1.05x |
| Normalized to Plan | 256 | 62.205 | 53.416 | 1.16x |
| Normalized to Plan | 4096 | 623.400 | 660.575 | 0.94x |
| Surface to Plan | 1 | 1.308 | 1.362 | 0.96x |
| Surface to Plan | 16 | 8.657 | 8.505 | 1.02x |
| Surface to Plan | 256 | 303.439 | 215.914 | 1.41x |
| Surface to Plan | 4096 | 23,142.275 | 3,658.400 | 6.33x |

The boundary and typical rows are dominated by fixed allocation and timer
variance; this change makes no small-Graph improvement claim. At 4096 operators,
the complete Surface-to-Plan lifecycle falls from about 23.14 ms to 3.66 ms.
The already-normalized Plan compiler remains within same-host run variance,
which isolates the material gain to normalization and optimization.
The remaining time includes two validated Graph snapshots, their allocations
and destruction, plus Plan compilation; dense topology lookup does not remove
those contractual costs.

## Rollback

Restore `cflow_subgraph_single_successor` calls in `lower.c` and `opt.c`, remove
the staged benchmark rows and characterization test, and retain the private
index for validation and normalized Plan compilation. No persisted data,
consumer migration or public rollback is required.
