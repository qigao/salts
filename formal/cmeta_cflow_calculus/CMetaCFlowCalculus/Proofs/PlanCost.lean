import CMetaCFlowCalculus.CFlow.PlanCost

namespace CMetaCFlowCalculus.CFlow

theorem raw_plan_nonempty_exact (workload : FusedFilterMapMap)
    (nonempty : 0 < workload.inputItems) :
    rawPlanMetrics workload =
      { graphQueries := 0
        instructionVisits := 2
        rawBatchStageCalls := 3
        adapterItemCalls := 0
        userCalls := workload.inputItems + 2 * workload.selectedItems
        elementVisits := 2 * workload.inputItems + workload.selectedItems
        allocations :=
          1 + allocationIfNonempty
              (workload.selectedItems * workload.intermediateItemBytes) +
            allocationIfNonempty
              (workload.selectedItems * workload.resultItemBytes)
        allocatedBytes :=
          selectionByteCount workload.inputItems +
            workload.selectedItems * workload.intermediateItemBytes +
            workload.selectedItems * workload.resultItemBytes
        peakLiveBytes :=
          workload.selectedItems * workload.intermediateItemBytes +
            max (selectionByteCount workload.inputItems)
              (workload.selectedItems * workload.resultItemBytes)
        selectionBytes := selectionByteCount workload.inputItems
        intermediateBytes :=
          workload.selectedItems * workload.intermediateItemBytes
        resultBytes := workload.selectedItems * workload.resultItemBytes
        memoryPasses :=
          2 + if workload.selectedItems = 0 then 0 else 1 } := by
  simp [rawPlanMetrics, Nat.ne_of_gt nonempty]

theorem raw_plan_graph_queries_zero (workload : FusedFilterMapMap) :
    (rawPlanMetrics workload).graphQueries = 0 := by
  unfold rawPlanMetrics
  split <;> rfl

theorem raw_plan_element_visits (workload : FusedFilterMapMap)
    (nonempty : 0 < workload.inputItems) :
    (rawPlanMetrics workload).elementVisits =
      2 * workload.inputItems + workload.selectedItems := by
  simp [rawPlanMetrics, Nat.ne_of_gt nonempty]

theorem raw_plan_user_calls (workload : FusedFilterMapMap)
    (nonempty : 0 < workload.inputItems) :
    (rawPlanMetrics workload).userCalls =
      workload.inputItems + 2 * workload.selectedItems := by
  simp [rawPlanMetrics, Nat.ne_of_gt nonempty]

theorem raw_plan_three_allocations (workload : FusedFilterMapMap)
    (inputNonempty : 0 < workload.inputItems)
    (selectionNonempty : 0 < workload.selectedItems)
    (intermediatePositive : 0 < workload.intermediateItemBytes)
    (resultPositive : 0 < workload.resultItemBytes) :
    (rawPlanMetrics workload).allocations = 3 := by
  have intermediateNonzero :
      workload.selectedItems * workload.intermediateItemBytes ≠ 0 :=
    Nat.ne_of_gt (Nat.mul_pos selectionNonempty intermediatePositive)
  have resultNonzero :
      workload.selectedItems * workload.resultItemBytes ≠ 0 :=
    Nat.ne_of_gt (Nat.mul_pos selectionNonempty resultPositive)
  simp [rawPlanMetrics, Nat.ne_of_gt inputNonempty, allocationIfNonempty,
    intermediateNonzero, resultNonzero]

theorem raw_plan_three_passes (workload : FusedFilterMapMap)
    (inputNonempty : 0 < workload.inputItems)
    (selectionNonempty : 0 < workload.selectedItems) :
    (rawPlanMetrics workload).memoryPasses = 3 := by
  simp [rawPlanMetrics, Nat.ne_of_gt inputNonempty,
    Nat.ne_of_gt selectionNonempty]

theorem raw_plan_callback_dispatches (workload : FusedFilterMapMap)
    (nonempty : 0 < workload.inputItems) :
    (rawPlanMetrics workload).toCost.callbackDispatches =
      3 + (workload.inputItems + 2 * workload.selectedItems) := by
  simp [PlanEvalMetrics.toCost, rawPlanMetrics, Nat.ne_of_gt nonempty]

theorem representation_equivalent_same_observation
    {left right : PlanEncoding α}
    (equivalent : RepresentationEquivalent left right) :
    left.observation = right.observation :=
  equivalent.2.1

theorem representation_equivalent_same_metrics
    {left right : PlanEncoding α}
    (equivalent : RepresentationEquivalent left right) :
    left.metrics = right.metrics := by
  exact congrArg rawPlanMetrics equivalent.2.2

end CMetaCFlowCalculus.CFlow
