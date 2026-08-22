import CMetaCFlowCalculus.CFlow.Rewrite

namespace CMetaCFlowCalculus.CFlow

open CMetaCFlowCalculus.CMeta

/-- The ten-dimensional symbolic cost vector from Calculus v1. -/
structure Cost where
  allocations : Nat
  allocatedBytes : Nat
  copies : Nat
  copiedBytes : Nat
  callbackDispatches : Nat
  atomicOps : Nat
  schedulerHops : Nat
  wakeups : Nat
  syscalls : Nat
  memoryPasses : Nat
  deriving Repr, DecidableEq

/-- Stable names for every symbolic cost coordinate. -/
inductive CostDimension where
  | allocations
  | allocatedBytes
  | copies
  | copiedBytes
  | callbackDispatches
  | atomicOps
  | schedulerHops
  | wakeups
  | syscalls
  | memoryPasses
  deriving Repr, DecidableEq

/-- Profile-derived non-negative weights remain distinct from symbolic Cost. -/
structure CostWeights where
  allocations : Nat
  allocatedBytes : Nat
  copies : Nat
  copiedBytes : Nat
  callbackDispatches : Nat
  atomicOps : Nat
  schedulerHops : Nat
  wakeups : Nat
  syscalls : Nat
  memoryPasses : Nat
  deriving Repr, DecidableEq

namespace Cost

def zero : Cost where
  allocations := 0
  allocatedBytes := 0
  copies := 0
  copiedBytes := 0
  callbackDispatches := 0
  atomicOps := 0
  schedulerHops := 0
  wakeups := 0
  syscalls := 0
  memoryPasses := 0

def get (cost : Cost) : CostDimension → Nat
  | .allocations => cost.allocations
  | .allocatedBytes => cost.allocatedBytes
  | .copies => cost.copies
  | .copiedBytes => cost.copiedBytes
  | .callbackDispatches => cost.callbackDispatches
  | .atomicOps => cost.atomicOps
  | .schedulerHops => cost.schedulerHops
  | .wakeups => cost.wakeups
  | .syscalls => cost.syscalls
  | .memoryPasses => cost.memoryPasses

def add (left right : Cost) : Cost where
  allocations := left.allocations + right.allocations
  allocatedBytes := left.allocatedBytes + right.allocatedBytes
  copies := left.copies + right.copies
  copiedBytes := left.copiedBytes + right.copiedBytes
  callbackDispatches := left.callbackDispatches + right.callbackDispatches
  atomicOps := left.atomicOps + right.atomicOps
  schedulerHops := left.schedulerHops + right.schedulerHops
  wakeups := left.wakeups + right.wakeups
  syscalls := left.syscalls + right.syscalls
  memoryPasses := left.memoryPasses + right.memoryPasses

/-- Component-wise non-strict comparison. -/
def pointwiseLE (better worse : Cost) : Prop :=
  ∀ dimension, better.get dimension ≤ worse.get dimension

/-- Strict Pareto dominance: no dimension worsens and one improves. -/
def ParetoDominates (better worse : Cost) : Prop :=
  pointwiseLE better worse ∧
    ∃ dimension, better.get dimension < worse.get dimension

/-- Profile-weighted extraction is a selector, not a semantic proof. -/
def weighted (weights : CostWeights) (cost : Cost) : Nat :=
  weights.allocations * cost.allocations +
  weights.allocatedBytes * cost.allocatedBytes +
  weights.copies * cost.copies +
  weights.copiedBytes * cost.copiedBytes +
  weights.callbackDispatches * cost.callbackDispatches +
  weights.atomicOps * cost.atomicOps +
  weights.schedulerHops * cost.schedulerHops +
  weights.wakeups * cost.wakeups +
  weights.syscalls * cost.syscalls +
  weights.memoryPasses * cost.memoryPasses

end Cost

/-- Structural and typing facts used to derive one execution path. -/
structure ExecutionFacts where
  wellTyped : Bool
  staticShape : Bool
  noDynamicWait : Bool
  segment : SegmentFacts
  deriving Repr, DecidableEq

inductive EfficiencyPath where
  | direct
  | plan
  | kernel
  deriving Repr, DecidableEq

def DirectEligible (K : KernelCapabilities) (facts : ExecutionFacts) : Prop :=
  facts.wellTyped = true ∧
    SyncClosed facts.segment ∧
    facts.segment.linear = true ∧
    K .syncExecution

/-- Async structure requires the full Kernel path independently of support. -/
def KernelTrigger (facts : ExecutionFacts) : Prop :=
  facts.segment.mayWait = true ∨
    facts.segment.hasExecutorBoundary = true ∨
    facts.segment.hasAsyncBuffer = true

def PlanEligible (K : KernelCapabilities) (facts : ExecutionFacts) : Prop :=
  facts.wellTyped = true ∧
    facts.staticShape = true ∧
    facts.noDynamicWait = true ∧
    K .syncExecution ∧
    ¬KernelTrigger facts ∧
    ¬DirectEligible K facts

/-- Each async trigger has a matching capability in `K`. -/
def KernelSupports (K : KernelCapabilities) (facts : ExecutionFacts) : Prop :=
  (facts.segment.mayWait = true → K .waitable) ∧
    (facts.segment.hasExecutorBoundary = true → K .parallelExecutor) ∧
    (facts.segment.hasAsyncBuffer = true → K .waitable)

def KernelRequired (K : KernelCapabilities) (facts : ExecutionFacts) : Prop :=
  facts.wellTyped = true ∧ KernelTrigger facts ∧ KernelSupports K facts

/-- The `Γ ; K` execution-class judgment; typing evidence is carried in facts. -/
inductive PathDerivation (_Γ : Env) (K : KernelCapabilities)
    (facts : ExecutionFacts) : EfficiencyPath → Prop where
  | direct : DirectEligible K facts → PathDerivation _Γ K facts .direct
  | plan : PlanEligible K facts → PathDerivation _Γ K facts .plan
  | kernel : KernelRequired K facts → PathDerivation _Γ K facts .kernel

/-- Symbolic workload inputs; no field is a wall-clock measurement. -/
structure Workload where
  items : Nat
  itemBytes : Nat
  operators : Nat
  deriving Repr, DecidableEq

def Workload.memoryPasses (workload : Workload) : Nat :=
  if workload.items = 0 then 0 else 1

/-- Reference cost for a fused native loop with inlined operators. -/
def directCost (workload : Workload) : Cost :=
  { Cost.zero with memoryPasses := workload.memoryPasses }

/-- Reference cost for a compact Plan dispatching each operator per item. -/
def planCost (workload : Workload) : Cost :=
  { directCost workload with
    callbackDispatches := workload.items * workload.operators }

/-- Reference Kernel cost adds state-machine coordination to Plan work. -/
def kernelCost (workload : Workload) : Cost :=
  { planCost workload with
    atomicOps := workload.items
    schedulerHops := 1
    wakeups := 1 }

def pathCost : EfficiencyPath → Workload → Cost
  | .direct => directCost
  | .plan => planCost
  | .kernel => kernelCost

/-- Scalar dispatch count for a non-empty one-pass candidate. -/
def scalarCost (items : Nat) : Cost :=
  { Cost.zero with
    callbackDispatches := items
    memoryPasses := if items = 0 then 0 else 1 }

/-- Batch dispatch count is supplied by a separately justified batching plan. -/
def batchCost (items batches : Nat) : Cost :=
  { Cost.zero with
    callbackDispatches := batches
    memoryPasses := if items = 0 then 0 else 1 }

/-- Sequential Reduce cost records critical-path reducer callbacks. -/
def sequentialReduceCost (criticalCallbacks : Nat) : Cost :=
  { Cost.zero with
    callbackDispatches := criticalCallbacks
    memoryPasses := 1 }

/-- Parallel Reduce may trade fewer critical callbacks for scheduler hops. -/
def parallelReduceCost (criticalCallbacks schedulerHops : Nat) : Cost :=
  { Cost.zero with
    callbackDispatches := criticalCallbacks
    schedulerHops := schedulerHops
    memoryPasses := 1 }

/-- Cost selection remains separate from semantic-refinement evidence. -/
inductive CostPreference (before after : Cost) : Prop where
  | pareto : Cost.ParetoDominates after before → CostPreference before after
  | profileWeighted (weights : CostWeights) :
      Cost.weighted weights after < Cost.weighted weights before →
        CostPreference before after

/-- A Phase D semantic refinement paired with an explicit cost justification. -/
structure CostedRefinement (Γ : Env) (K : KernelCapabilities) {α : Type}
    (before after : ExecutionVariant α) where
  refinement : ExecutionRefines Γ K before after
  beforeCost : Cost
  afterCost : Cost
  preference : CostPreference beforeCost afterCost

end CMetaCFlowCalculus.CFlow
