import CMetaCFlowCalculus.CMeta.Effects
import CMetaCFlowCalculus.CMeta.Ownership

namespace CMetaCFlowCalculus.CFlow

open CMetaCFlowCalculus.CMeta

/-- Capabilities supplied by an execution environment `K`. -/
inductive KernelCapability where
  | syncExecution
  | waitable
  | timer
  | parallelExecutor
  | splittableSource
  | batchExecution
  | contiguousAccess
  | processSharedWait
  deriving Repr, DecidableEq

/-- `K` describes capabilities available in the current execution environment. -/
abbrev KernelCapabilities := KernelCapability → Prop

/-- A Plan states requirements independently from the capabilities in `K`. -/
abbrev CapabilityRequirements := KernelCapability → Prop

/-- Every capability required by a Plan is supplied by `K`. -/
def Capabilities (K : KernelCapabilities)
    (requirements : CapabilityRequirements) : Prop :=
  ∀ capability, requirements capability → K capability

/-- User-observable events; Kernel scheduling identities are intentionally absent. -/
inductive Observation (outputTy : Ty) where
  | value (output : Value outputTy)
  | effect (effect : Effect)
  | error (message : String)
  | done
  | cancelled

/-- Ordered observations plus the ownership-safety result of the execution. -/
structure Trace (outputTy : Ty) where
  events : List (Observation outputTy)
  ownershipSafe : Bool

/-- Typed inputs supplied to the denotation of an architecture artifact. -/
abbrev Input (inputTy : Ty) := List (Value inputTy)

/-- Denotation of a Surface, Graph, or Plan under an execution environment. -/
abbrev Semantics (inputTy outputTy : Ty) :=
  KernelCapabilities → Input inputTy → Trace outputTy

/-- Denotation of a concrete execution after its Kernel environment is fixed. -/
abbrev ExecutionSemantics (inputTy outputTy : Ty) :=
  Input inputTy → Trace outputTy

/-- Observational equivalence at one fixed Kernel capability environment. -/
def ObsEqAt {inputTy outputTy : Ty} (K : KernelCapabilities)
    (left right : Semantics inputTy outputTy) : Prop :=
  ∀ input, left K input = right K input

/-- Observational equivalence for every supported Kernel environment. -/
def ObsEq {inputTy outputTy : Ty}
    (left right : Semantics inputTy outputTy) : Prop :=
  ∀ K, ObsEqAt K left right

/-- Compare a Plan denotation with an execution whose `K` is already fixed. -/
def ExecutionObsEqAt {inputTy outputTy : Ty} (K : KernelCapabilities)
    (plan : Semantics inputTy outputTy)
    (execution : ExecutionSemantics inputTy outputTy) : Prop :=
  ∀ input, plan K input = execution input

theorem ObsEqAt.refl {inputTy outputTy : Ty} (K : KernelCapabilities)
    (semantics : Semantics inputTy outputTy) :
    ObsEqAt K semantics semantics := by
  intro input
  rfl

theorem ObsEqAt.trans {inputTy outputTy : Ty} {K : KernelCapabilities}
    {first second third : Semantics inputTy outputTy}
    (firstSecond : ObsEqAt K first second)
    (secondThird : ObsEqAt K second third) :
    ObsEqAt K first third := by
  intro input
  exact Eq.trans (firstSecond input) (secondThird input)

end CMetaCFlowCalculus.CFlow
