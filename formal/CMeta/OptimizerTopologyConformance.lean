import CMeta.OptimizerGatingConformance
import CMeta.OptimizerTopologyGeneratedC

/-!
# Dead-subgraph / topology optimizer conformance

This module models the finite subgraph-reference topology exercised by the real
C optimizer witness.  The C witness contains one root relation, one nested
reachable branch and one detached subgraph.  `CMETA_OPT_DEAD_SUBGRAPHS` must
retain exactly the root-reachable closure while leaving root execution
unchanged.
-/

namespace CMeta

inductive TopologyNode where
  | root
  | branch
  | dead
  deriving Repr, DecidableEq

private def allTopologyNodes : List TopologyNode :=
  [.root, .branch, .dead]

private def topologyEdges : TopologyNode → List TopologyNode
  | .root => [.branch]
  | .branch => []
  | .dead => []

/-- Fuel-bounded reachability for the finite witness graph.  Fuel bounds path
    length and makes the executable checker structurally terminating. -/
private def reachableWithin : Nat → TopologyNode → TopologyNode → Bool
  | 0, from, target => from == target
  | fuel + 1, from, target =>
      from == target ||
        (topologyEdges from).any (fun next => reachableWithin fuel next target)

private def reachableFromRoot (node : TopologyNode) : Bool :=
  reachableWithin allTopologyNodes.length .root node

private def liveTopologyNodes : List TopologyNode :=
  allTopologyNodes.filter reachableFromRoot

/-- The nested relation branch belongs to the root-reachable closure. -/
theorem OptimizerTopologyConformance.branch_reachable :
    reachableFromRoot .branch = true := by
  native_decide

/-- The detached subgraph is not root-reachable. -/
theorem OptimizerTopologyConformance.dead_unreachable :
    reachableFromRoot .dead = false := by
  native_decide

/-- The formal reachable closure contains exactly root and nested branch. -/
theorem OptimizerTopologyConformance.live_set_exact :
    liveTopologyNodes = [.root, .branch] := by
  native_decide

private def reachableBranch : Callable1 CType.int CType.long :=
  ⟨fun (x : Int) => x * 2⟩

private def deadBranch : Callable1 CType.int CType.long :=
  ⟨fun (x : Int) => x + 100⟩

private structure TopologyExec where
  live : Callable1 CType.int CType.long
  dead : Callable1 CType.int CType.long

private def topologyExec : TopologyExec :=
  ⟨reachableBranch, deadBranch⟩

private def runRoot (exec : TopologyExec) (xs : List Int) : List Int :=
  xs.map exec.live.run

/-- Root execution depends only on the reachable branch.  Replacing arbitrary
    dead-subgraph code cannot affect the root denotation. -/
theorem OptimizerTopologyConformance.dead_code_irrelevant
    (replacement : Callable1 CType.int CType.long) (xs : List Int) :
    runRoot { topologyExec with dead := replacement } xs =
      runRoot topologyExec xs := by
  rfl

private structure TopologyModelResult where
  beforeSubgraphs : Nat
  beforeReachable : Nat
  deadOnSubgraphs : Nat
  deadOnReachable : Nat
  deadOnRemoved : Nat
  deadOffSubgraphs : Nat
  deadOffReachable : Nat
  deadOffRemoved : Nat
  output : List Int

private def topologyModel (input : List Int) : TopologyModelResult :=
  { beforeSubgraphs := allTopologyNodes.length,
    beforeReachable := liveTopologyNodes.length,
    deadOnSubgraphs := liveTopologyNodes.length,
    deadOnReachable := liveTopologyNodes.length,
    deadOnRemoved := allTopologyNodes.length - liveTopologyNodes.length,
    deadOffSubgraphs := allTopologyNodes.length,
    deadOffReachable := liveTopologyNodes.length,
    deadOffRemoved := 0,
    output := runRoot topologyExec input }

private def topologyWitnessConforms
    (w : COptimizerTopologyGenerated.TopologyWitness) : Bool :=
  let expected := topologyModel w.input
  w.name == "dead_subgraph_reachability_i_l" &&
  w.beforeSubgraphs == expected.beforeSubgraphs &&
  w.beforeReachable == expected.beforeReachable &&
  w.deadOnSubgraphs == expected.deadOnSubgraphs &&
  w.deadOnReachable == expected.deadOnReachable &&
  w.deadOnRemoved == expected.deadOnRemoved &&
  w.deadOffSubgraphs == expected.deadOffSubgraphs &&
  w.deadOffReachable == expected.deadOffReachable &&
  w.deadOffRemoved == expected.deadOffRemoved &&
  w.reachableBranchRetainedOn &&
  w.reachableBranchRetainedOff &&
  w.beforeOutput == expected.output &&
  w.deadOnOutput == expected.output &&
  w.deadOffOutput == expected.output

/-- CI keeps the concrete root/reachable/dead topology witness present. -/
theorem OptimizerTopologyConformance.coverage :
    COptimizerTopologyGenerated.topologyWitnesses.map (fun w => w.name) =
      ["dead_subgraph_reachability_i_l"] := by
  native_decide

/-- Main implementation-refinement theorem: the real optimizer removes exactly
    the unreachable subgraph when the pass is enabled, retains the reachable
    nested branch, preserves detached subgraphs when the pass is disabled, and
    preserves real root runtime values in both optimized graphs. -/
theorem OptimizerTopologyConformance.runtime_and_reachability_match :
    COptimizerTopologyGenerated.topologyWitnesses.all
      topologyWitnessConforms = true := by
  native_decide

/-- Public topology optimizer gate. -/
theorem CImplementationConformance.optimizer_topology :
    COptimizerTopologyGenerated.topologyWitnesses.all
      topologyWitnessConforms = true := by
  exact OptimizerTopologyConformance.runtime_and_reachability_match

end CMeta
