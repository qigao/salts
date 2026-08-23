import CMetaCFlowCalculus.CFlow.Cost
import CMetaCFlowCalculus.Proofs.Rewrite

namespace CMetaCFlowCalculus.CFlow

open CMetaCFlowCalculus.CMeta

namespace Cost

theorem pointwiseLE_refl (cost : Cost) : pointwiseLE cost cost := by
  intro dimension
  exact Nat.le_refl (cost.get dimension)

theorem pointwiseLE_trans {first second third : Cost}
    (firstSecond : pointwiseLE first second)
    (secondThird : pointwiseLE second third) :
    pointwiseLE first third := by
  intro dimension
  exact Nat.le_trans (firstSecond dimension) (secondThird dimension)

theorem add_mono {firstBetter firstWorse secondBetter secondWorse : Cost}
    (firstLE : pointwiseLE firstBetter firstWorse)
    (secondLE : pointwiseLE secondBetter secondWorse) :
    pointwiseLE (firstBetter.add secondBetter)
      (firstWorse.add secondWorse) := by
  intro dimension
  cases dimension with
  | allocations =>
      exact Nat.add_le_add (firstLE .allocations) (secondLE .allocations)
  | allocatedBytes =>
      exact Nat.add_le_add (firstLE .allocatedBytes) (secondLE .allocatedBytes)
  | copies =>
      exact Nat.add_le_add (firstLE .copies) (secondLE .copies)
  | copiedBytes =>
      exact Nat.add_le_add (firstLE .copiedBytes) (secondLE .copiedBytes)
  | callbackDispatches =>
      exact Nat.add_le_add (firstLE .callbackDispatches)
        (secondLE .callbackDispatches)
  | atomicOps =>
      exact Nat.add_le_add (firstLE .atomicOps) (secondLE .atomicOps)
  | schedulerHops =>
      exact Nat.add_le_add (firstLE .schedulerHops) (secondLE .schedulerHops)
  | wakeups =>
      exact Nat.add_le_add (firstLE .wakeups) (secondLE .wakeups)
  | syscalls =>
      exact Nat.add_le_add (firstLE .syscalls) (secondLE .syscalls)
  | memoryPasses =>
      exact Nat.add_le_add (firstLE .memoryPasses) (secondLE .memoryPasses)

theorem pareto_irrefl (cost : Cost) : ¬ParetoDominates cost cost := by
  intro dominates
  obtain ⟨_, dimension, strict⟩ := dominates
  exact (Nat.lt_irrefl (cost.get dimension)) strict

theorem zero_pareto_dominates_of_positive (cost : Cost)
    (dimension : CostDimension) (positive : 0 < cost.get dimension) :
    ParetoDominates zero cost := by
  constructor
  · intro coordinate
    cases coordinate <;> exact Nat.zero_le _
  · refine ⟨dimension, ?_⟩
    cases dimension <;> exact positive

theorem add_pareto_dominates_of_positive (base delta : Cost)
    (dimension : CostDimension) (positive : 0 < delta.get dimension) :
    ParetoDominates base (base.add delta) := by
  constructor
  · intro coordinate
    cases coordinate <;> simp [get, add]
  · refine ⟨dimension, ?_⟩
    cases dimension with
    | allocations =>
        simpa [get, add] using Nat.add_lt_add_left positive base.allocations
    | allocatedBytes =>
        simpa [get, add] using Nat.add_lt_add_left positive base.allocatedBytes
    | copies =>
        simpa [get, add] using Nat.add_lt_add_left positive base.copies
    | copiedBytes =>
        simpa [get, add] using Nat.add_lt_add_left positive base.copiedBytes
    | callbackDispatches =>
        simpa [get, add] using
          Nat.add_lt_add_left positive base.callbackDispatches
    | atomicOps =>
        simpa [get, add] using Nat.add_lt_add_left positive base.atomicOps
    | schedulerHops =>
        simpa [get, add] using Nat.add_lt_add_left positive base.schedulerHops
    | wakeups =>
        simpa [get, add] using Nat.add_lt_add_left positive base.wakeups
    | syscalls =>
        simpa [get, add] using Nat.add_lt_add_left positive base.syscalls
    | memoryPasses =>
        simpa [get, add] using Nat.add_lt_add_left positive base.memoryPasses

end Cost

theorem direct_excludes_kernel_trigger {K : KernelCapabilities}
    {facts : ExecutionFacts} (direct : DirectEligible K facts) :
    ¬KernelTrigger facts := by
  intro trigger
  obtain ⟨_, ⟨noWait, noExecutor, noBuffer⟩, _, _⟩ := direct
  rcases trigger with wait | executor | buffer <;> simp_all

theorem plan_excludes_kernel_trigger {K : KernelCapabilities}
    {facts : ExecutionFacts} (plan : PlanEligible K facts) :
    ¬KernelTrigger facts :=
  plan.2.2.2.2.1

theorem direct_cost_dominates_plan (workload : Workload)
    (positiveCallbacks : 0 < workload.items * workload.operators) :
    Cost.ParetoDominates (directCost workload) (planCost workload) := by
  constructor
  · intro dimension
    cases dimension <;>
      simp [Cost.get, Cost.zero, directCost, planCost]
  · refine ⟨.callbackDispatches, ?_⟩
    simpa [Cost.get, Cost.zero, directCost, planCost] using positiveCallbacks

theorem plan_cost_dominates_kernel (workload : Workload) :
    Cost.ParetoDominates (planCost workload) (kernelCost workload) := by
  constructor
  · intro dimension
    cases dimension <;>
      simp [Cost.get, Cost.zero, directCost, planCost, kernelCost]
  · refine ⟨.schedulerHops, ?_⟩
    simp [Cost.get, Cost.zero, directCost, planCost, kernelCost]

theorem batch_cost_dominates_scalar (items batches : Nat)
    (fewerBatches : batches < items) :
    Cost.ParetoDominates (batchCost items batches) (scalarCost items) := by
  have batchesLE : batches ≤ items := Nat.le_of_lt fewerBatches
  constructor
  · intro dimension
    cases dimension <;>
      simp [Cost.get, Cost.zero, scalarCost, batchCost, batchesLE]
  · refine ⟨.callbackDispatches, ?_⟩
    simpa [Cost.get, Cost.zero, scalarCost, batchCost] using fewerBatches

theorem parallel_reduce_costs_incomparable
    (sequentialCallbacks parallelCallbacks schedulerHops : Nat)
    (fewerCallbacks : parallelCallbacks < sequentialCallbacks)
    (positiveHops : 0 < schedulerHops) :
    ¬Cost.ParetoDominates
        (parallelReduceCost parallelCallbacks schedulerHops)
        (sequentialReduceCost sequentialCallbacks) ∧
      ¬Cost.ParetoDominates
        (sequentialReduceCost sequentialCallbacks)
        (parallelReduceCost parallelCallbacks schedulerHops) := by
  constructor
  · intro dominates
    have hopsLE := dominates.1 .schedulerHops
    have notLE := Nat.not_le_of_gt positiveHops
    apply notLE
    simpa [Cost.get, Cost.zero, sequentialReduceCost, parallelReduceCost]
      using hopsLE
  · intro dominates
    have callbacksLE := dominates.1 .callbackDispatches
    exact (Nat.not_le_of_gt fewerCallbacks) (by
      simpa [Cost.get, Cost.zero, sequentialReduceCost, parallelReduceCost]
        using callbacksLE)

theorem costed_refinement_preserves_semantics {Γ : Env}
    {K : KernelCapabilities} {α : Type}
    {before after : ExecutionVariant α}
    (costed : CostedRefinement Γ K before after) :
    before.result = after.result :=
  execution_refinement_preserves_semantics costed.refinement

end CMetaCFlowCalculus.CFlow
