import CMetaCFlowCalculus.CFlow.Cost

namespace CMetaCFlowCalculus.CFlow

/-- Source-level workload for the current fused Filter -> Map -> Map Plan path. -/
structure FusedFilterMapMap where
  inputItems : Nat
  selectedItems : Nat
  intermediateItemBytes : Nat
  resultItemBytes : Nat
  deriving Repr, DecidableEq

/-- Preconditions supplied by Graph typing and the actual filter result. -/
def FusedFilterMapMap.Valid (workload : FusedFilterMapMap) : Prop :=
  workload.selectedItems ≤ workload.inputItems ∧
    0 < workload.intermediateItemBytes ∧
    0 < workload.resultItemBytes

/-- Bytes required by the C selection bitmap, including a partial final byte. -/
def selectionByteCount (items : Nat) : Nat :=
  items / 8 + if items % 8 = 0 then 0 else 1

def allocationIfNonempty (bytes : Nat) : Nat :=
  if bytes = 0 then 0 else 1

/--
Source-level events of Plan evaluation. These coordinates refine, but do not
replace, the stable ten-dimensional `Cost` vector. They are not time units.
-/
structure PlanEvalMetrics where
  graphQueries : Nat
  instructionVisits : Nat
  rawBatchStageCalls : Nat
  adapterItemCalls : Nat
  userCalls : Nat
  elementVisits : Nat
  allocations : Nat
  allocatedBytes : Nat
  peakLiveBytes : Nat
  selectionBytes : Nat
  intermediateBytes : Nat
  resultBytes : Nat
  memoryPasses : Nat
  deriving Repr, DecidableEq

namespace PlanEvalMetrics

def zero : PlanEvalMetrics where
  graphQueries := 0
  instructionVisits := 0
  rawBatchStageCalls := 0
  adapterItemCalls := 0
  userCalls := 0
  elementVisits := 0
  allocations := 0
  allocatedBytes := 0
  peakLiveBytes := 0
  selectionBytes := 0
  intermediateBytes := 0
  resultBytes := 0
  memoryPasses := 0

/--
Projection into Calculus v1 Cost. A raw batch stage dispatch and every raw
function-pointer application are distinct source-level callback dispatches.
-/
def toCost (metrics : PlanEvalMetrics) : Cost :=
  { Cost.zero with
    allocations := metrics.allocations
    allocatedBytes := metrics.allocatedBytes
    callbackDispatches := metrics.rawBatchStageCalls + metrics.userCalls
    memoryPasses := metrics.memoryPasses }

end PlanEvalMetrics

/--
Exact profile of the canonical raw fused-value executor. Graph traversal is a
compile-time concern; evaluation consumes only predecoded Plan instructions.
-/
def rawPlanMetrics (workload : FusedFilterMapMap) : PlanEvalMetrics :=
  if workload.inputItems = 0 then
    PlanEvalMetrics.zero
  else
    let selectionBytes := selectionByteCount workload.inputItems
    let intermediateBytes :=
      workload.selectedItems * workload.intermediateItemBytes
    let resultBytes := workload.selectedItems * workload.resultItemBytes
    { graphQueries := 0
      instructionVisits := 2
      rawBatchStageCalls := 3
      adapterItemCalls := 0
      userCalls := workload.inputItems + 2 * workload.selectedItems
      elementVisits := 2 * workload.inputItems + workload.selectedItems
      allocations :=
        1 + allocationIfNonempty intermediateBytes +
          allocationIfNonempty resultBytes
      allocatedBytes := selectionBytes + intermediateBytes + resultBytes
      peakLiveBytes := intermediateBytes + max selectionBytes resultBytes
      selectionBytes := selectionBytes
      intermediateBytes := intermediateBytes
      resultBytes := resultBytes
      memoryPasses := 2 + if workload.selectedItems = 0 then 0 else 1 }

/-- Canonical logical stages are independent of their physical storage. -/
inductive PlanStage where
  | filter
  | map
  deriving Repr, DecidableEq

/-- Candidate control-plane encodings; the current C Plan uses `linearTape`. -/
inductive PlanStorage where
  | linearTape
  | tree
  | hashIndexed
  deriving Repr, DecidableEq

/--
A representation exposes the schedule and observation whose preservation must
be proved by its compiler. The storage tag itself has no execution semantics.
-/
structure PlanEncoding (α : Type) where
  storage : PlanStorage
  schedule : List PlanStage
  observation : List α
  workload : FusedFilterMapMap

def PlanEncoding.metrics (encoding : PlanEncoding α) : PlanEvalMetrics :=
  rawPlanMetrics encoding.workload

/--
Tree or hash storage is equivalent to a tape only after it supplies the same
ordered stages, observable output, and workload witness.
-/
def RepresentationEquivalent (left right : PlanEncoding α) : Prop :=
  left.schedule = right.schedule ∧
    left.observation = right.observation ∧
    left.workload = right.workload

end CMetaCFlowCalculus.CFlow
