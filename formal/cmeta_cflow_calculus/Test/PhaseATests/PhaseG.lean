import CMetaCFlowCalculus.CFlow.PlanCost
import CMetaCFlowCalculus.Proofs.PlanCost

open CMetaCFlowCalculus.CFlow

namespace CMetaCFlowCalculus.Tests.PhaseG

def releaseHostWorkload : FusedFilterMapMap where
  inputItems := 1024
  selectedItems := 512
  intermediateItemBytes := 8
  resultItemBytes := 8

def expectedReleaseHostMetrics : PlanEvalMetrics where
  graphQueries := 0
  instructionVisits := 2
  rawBatchStageCalls := 3
  adapterItemCalls := 0
  userCalls := 2048
  elementVisits := 2560
  allocations := 3
  allocatedBytes := 8320
  peakLiveBytes := 8192
  selectionBytes := 128
  intermediateBytes := 4096
  resultBytes := 4096
  memoryPasses := 3

example : releaseHostWorkload.Valid := by
  simp [FusedFilterMapMap.Valid, releaseHostWorkload]

example : rawPlanMetrics releaseHostWorkload = expectedReleaseHostMetrics := rfl

example : rawPlanMetrics releaseHostWorkload = expectedReleaseHostMetrics := by
  simpa [expectedReleaseHostMetrics, releaseHostWorkload,
    allocationIfNonempty, selectionByteCount] using
    raw_plan_nonempty_exact releaseHostWorkload (by decide)

example : (rawPlanMetrics releaseHostWorkload).toCost =
    { Cost.zero with
      allocations := 3
      allocatedBytes := 8320
      callbackDispatches := 2051
      memoryPasses := 3 } := rfl

example : (rawPlanMetrics releaseHostWorkload).graphQueries = 0 :=
  raw_plan_graph_queries_zero releaseHostWorkload

example : (rawPlanMetrics releaseHostWorkload).elementVisits = 2560 := by
  exact raw_plan_element_visits releaseHostWorkload (by decide)

example : (rawPlanMetrics releaseHostWorkload).allocations = 3 := by
  exact raw_plan_three_allocations releaseHostWorkload
    (by decide) (by decide) (by decide) (by decide)

def emptyInputWorkload : FusedFilterMapMap where
  inputItems := 0
  selectedItems := 0
  intermediateItemBytes := 8
  resultItemBytes := 8

example : rawPlanMetrics emptyInputWorkload = PlanEvalMetrics.zero := rfl

def emptySelectionWorkload : FusedFilterMapMap where
  inputItems := 6
  selectedItems := 0
  intermediateItemBytes := 8
  resultItemBytes := 8

def expectedEmptySelectionMetrics : PlanEvalMetrics where
  graphQueries := 0
  instructionVisits := 2
  rawBatchStageCalls := 3
  adapterItemCalls := 0
  userCalls := 6
  elementVisits := 12
  allocations := 1
  allocatedBytes := 1
  peakLiveBytes := 1
  selectionBytes := 1
  intermediateBytes := 0
  resultBytes := 0
  memoryPasses := 2

example : emptySelectionWorkload.Valid := by
  simp [FusedFilterMapMap.Valid, emptySelectionWorkload]

example : rawPlanMetrics emptySelectionWorkload =
    expectedEmptySelectionMetrics := rfl

def canonicalStages : List PlanStage := [.filter, .map, .map]

def linearEncoding : PlanEncoding Nat where
  storage := .linearTape
  schedule := canonicalStages
  observation := [2, 8, 18]
  workload := releaseHostWorkload

def treeEncoding : PlanEncoding Nat where
  storage := .tree
  schedule := canonicalStages
  observation := [2, 8, 18]
  workload := releaseHostWorkload

def hashEncoding : PlanEncoding Nat where
  storage := .hashIndexed
  schedule := canonicalStages
  observation := [2, 8, 18]
  workload := releaseHostWorkload

example : RepresentationEquivalent linearEncoding treeEncoding := by
  simp [RepresentationEquivalent, linearEncoding, treeEncoding]

example : RepresentationEquivalent linearEncoding hashEncoding := by
  simp [RepresentationEquivalent, linearEncoding, hashEncoding]

example : linearEncoding.metrics = treeEncoding.metrics :=
  representation_equivalent_same_metrics (by
    simp [RepresentationEquivalent, linearEncoding, treeEncoding])

example : linearEncoding.metrics = hashEncoding.metrics :=
  representation_equivalent_same_metrics (by
    simp [RepresentationEquivalent, linearEncoding, hashEncoding])

end CMetaCFlowCalculus.Tests.PhaseG
