import CMetaCFlowCalculus.CFlow.Cost
import CMetaCFlowCalculus.Proofs.Cost
import PhaseATests.PhaseD

open CMetaCFlowCalculus.CMeta
open CMetaCFlowCalculus.CFlow

namespace CMetaCFlowCalculus.Tests.PhaseE

def schedulerCost : Cost :=
  { Cost.zero with schedulerHops := 1 }

example : Cost.ParetoDominates Cost.zero schedulerCost := by
  exact Cost.zero_pareto_dominates_of_positive
    schedulerCost .schedulerHops (by decide)

example : ¬Cost.ParetoDominates Cost.zero Cost.zero :=
  Cost.pareto_irrefl Cost.zero

def literalCost : Cost where
  allocations := 1
  allocatedBytes := 2
  copies := 3
  copiedBytes := 4
  callbackDispatches := 5
  atomicOps := 6
  schedulerHops := 7
  wakeups := 8
  syscalls := 9
  memoryPasses := 10

def literalWeights : CostWeights where
  allocations := 10
  allocatedBytes := 0
  copies := 0
  copiedBytes := 0
  callbackDispatches := 2
  atomicOps := 0
  schedulerHops := 3
  wakeups := 0
  syscalls := 0
  memoryPasses := 1

example : Cost.weighted literalWeights literalCost = 51 := rfl

def pathEnv : Env where
  hasCapability := fun _ _ => True
  declaresUnary := fun _ _ _ _ _ => True
  declaresBinary := fun _ _ _ _ _ _ => True
  declaresSource := fun _ _ => True
  declaresCollector := fun _ _ _ => True

def syncK : KernelCapabilities := fun capability =>
  capability = .syncExecution

def waitK : KernelCapabilities := fun capability =>
  capability = .waitable

def noK : KernelCapabilities := fun _ => False

def directSegment : SegmentFacts where
  mayWait := false
  hasExecutorBoundary := false
  hasAsyncBuffer := false
  linear := true

def directFacts : ExecutionFacts where
  wellTyped := true
  staticShape := true
  noDynamicWait := true
  segment := directSegment

example : DirectEligible syncK directFacts := by
  exact ⟨rfl, ⟨rfl, rfl, rfl⟩, rfl, rfl⟩

example : PathDerivation pathEnv syncK directFacts .direct := by
  exact .direct ⟨rfl, ⟨rfl, rfl, rfl⟩, rfl, rfl⟩

def planSegment : SegmentFacts where
  mayWait := false
  hasExecutorBoundary := false
  hasAsyncBuffer := false
  linear := false

def planFacts : ExecutionFacts where
  wellTyped := true
  staticShape := true
  noDynamicWait := true
  segment := planSegment

example : PlanEligible syncK planFacts := by
  refine ⟨rfl, rfl, rfl, rfl,
    (by simp [KernelTrigger, planFacts, planSegment]), ?_⟩
  intro direct
  exact Bool.noConfusion direct.2.2.1

example : PathDerivation pathEnv syncK planFacts .plan := by
  exact .plan (by
    refine ⟨rfl, rfl, rfl, rfl,
      (by simp [KernelTrigger, planFacts, planSegment]), ?_⟩
    intro direct
    exact Bool.noConfusion direct.2.2.1)

def waitingSegment : SegmentFacts where
  mayWait := true
  hasExecutorBoundary := false
  hasAsyncBuffer := false
  linear := true

def waitingFacts : ExecutionFacts where
  wellTyped := true
  staticShape := false
  noDynamicWait := false
  segment := waitingSegment

example : KernelRequired waitK waitingFacts := by
  exact ⟨rfl, Or.inl rfl, ⟨(fun _ => rfl),
    (fun impossible => Bool.noConfusion impossible),
    (fun impossible => Bool.noConfusion impossible)⟩⟩

example : PathDerivation pathEnv waitK waitingFacts .kernel := by
  exact .kernel ⟨rfl, Or.inl rfl,
    ⟨(fun _ => rfl),
      (fun impossible => Bool.noConfusion impossible),
      (fun impossible => Bool.noConfusion impossible)⟩⟩

example : ¬DirectEligible syncK waitingFacts := by
  intro direct
  exact Bool.noConfusion direct.2.1.1

example : ¬KernelRequired noK waitingFacts := by
  intro required
  exact required.2.2.1 rfl

example (K : KernelCapabilities) (facts : ExecutionFacts)
    (direct : DirectEligible K facts) : ¬KernelTrigger facts :=
  direct_excludes_kernel_trigger direct

def executorSegment : SegmentFacts where
  mayWait := false
  hasExecutorBoundary := true
  hasAsyncBuffer := false
  linear := false

def executorFacts : ExecutionFacts where
  wellTyped := true
  staticShape := true
  noDynamicWait := true
  segment := executorSegment

def executorK : KernelCapabilities := fun capability =>
  capability = .syncExecution ∨ capability = .parallelExecutor

example : KernelRequired executorK executorFacts := by
  exact ⟨rfl, Or.inr (Or.inl rfl),
    ⟨(fun impossible => Bool.noConfusion impossible),
      (fun _ => Or.inr rfl),
      (fun impossible => Bool.noConfusion impossible)⟩⟩

example : ¬PlanEligible executorK executorFacts := by
  intro plan
  exact plan.2.2.2.2.1 (Or.inr (Or.inl rfl))

def threeByTwo : Workload where
  items := 3
  itemBytes := 8
  operators := 2

def expectedDirect : Cost :=
  { Cost.zero with memoryPasses := 1 }

def expectedPlan : Cost :=
  { Cost.zero with callbackDispatches := 6, memoryPasses := 1 }

def expectedKernel : Cost :=
  { Cost.zero with
    callbackDispatches := 6
    atomicOps := 3
    schedulerHops := 1
    wakeups := 1
    memoryPasses := 1 }

example : directCost threeByTwo = expectedDirect := rfl
example : planCost threeByTwo = expectedPlan := rfl
example : kernelCost threeByTwo = expectedKernel := rfl

example : pathCost .direct threeByTwo = expectedDirect := rfl
example : pathCost .plan threeByTwo = expectedPlan := rfl
example : pathCost .kernel threeByTwo = expectedKernel := rfl

example : Cost.ParetoDominates (directCost threeByTwo)
    (planCost threeByTwo) :=
  direct_cost_dominates_plan threeByTwo (by decide)

example : Cost.ParetoDominates (planCost threeByTwo)
    (kernelCost threeByTwo) :=
  plan_cost_dominates_kernel threeByTwo

example : Cost.ParetoDominates (batchCost 8 2) (scalarCost 8) :=
  batch_cost_dominates_scalar 8 2 (by decide)

def sequentialSeven : Cost := sequentialReduceCost 7
def parallelThreeTwo : Cost := parallelReduceCost 3 2

example :
    ¬Cost.ParetoDominates parallelThreeTwo sequentialSeven ∧
      ¬Cost.ParetoDominates sequentialSeven parallelThreeTwo :=
  parallel_reduce_costs_incomparable 7 3 2 (by decide) (by decide)

def parallelWeights : CostWeights where
  allocations := 0
  allocatedBytes := 0
  copies := 0
  copiedBytes := 0
  callbackDispatches := 10
  atomicOps := 0
  schedulerHops := 1
  wakeups := 0
  syscalls := 0
  memoryPasses := 0

example : Cost.weighted parallelWeights sequentialSeven = 70 := rfl
example : Cost.weighted parallelWeights parallelThreeTwo = 32 := rfl
example : Cost.weighted parallelWeights parallelThreeTwo <
    Cost.weighted parallelWeights sequentialSeven := by decide

def batchCostedRefinement : CostedRefinement
    PhaseD.permissiveEnv PhaseD.batchK
    { form := .scalar, result := PhaseD.valueStream }
    { form := .batch, result := PhaseD.valueStream } where
  refinement := PhaseD.r14Witness
  beforeCost := scalarCost 8
  afterCost := batchCost 8 2
  preference := .pareto (batch_cost_dominates_scalar 8 2 (by decide))

def parallelCostedRefinement : CostedRefinement
    PhaseD.permissiveEnv PhaseD.parallelK
    { form := .reduce, result := PhaseD.valueStream }
    { form := .parallelReduce, result := PhaseD.valueStream } where
  refinement := PhaseD.r11Witness
  beforeCost := sequentialSeven
  afterCost := parallelThreeTwo
  preference := .profileWeighted parallelWeights (by decide)

example :
    ({ form := .scalar, result := PhaseD.valueStream } :
      ExecutionVariant _).result =
    ({ form := .batch, result := PhaseD.valueStream } :
      ExecutionVariant _).result :=
  costed_refinement_preserves_semantics batchCostedRefinement

example :
    ({ form := .reduce, result := PhaseD.valueStream } :
      ExecutionVariant _).result =
    ({ form := .parallelReduce, result := PhaseD.valueStream } :
      ExecutionVariant _).result :=
  costed_refinement_preserves_semantics parallelCostedRefinement

end CMetaCFlowCalculus.Tests.PhaseE
