import CMetaCFlowCalculus.CFlow.Observation
import CMetaCFlowCalculus.CFlow.Syntax

namespace CMetaCFlowCalculus.CFlow

open CMetaCFlowCalculus.CMeta

/-- Stable identifiers for the v1 R1–R15 catalogue. -/
inductive RuleId where
  | r1 | r2 | r3 | r4 | r5
  | r6 | r7 | r8 | r9 | r10
  | r11 | r12 | r13 | r14 | r15
  deriving Repr, DecidableEq

/-- A rule's proof obligation, not its expected performance effect. -/
inductive RuleClass where
  | semanticRewrite
  | executionRefinement
  | wellFormedness
  deriving Repr, DecidableEq

def ruleClass : RuleId → RuleClass
  | .r1 | .r2 | .r3 | .r4 | .r5
  | .r6 | .r7 | .r8 | .r9 | .r10 => .semanticRewrite
  | .r11 | .r12 | .r13 | .r14 => .executionRefinement
  | .r15 => .wellFormedness

/-- Only rules with denotational-equality theorems inhabit this type. -/
inductive SemanticRuleId where
  | r1 | r2 | r3 | r4 | r5
  | r6 | r7 | r8 | r9 | r10
  deriving Repr, DecidableEq

/-- Execution choices remain separate from semantic rewrite admission. -/
inductive ExecutionRefinementRuleId where
  | r11 | r12 | r13 | r14
  deriving Repr, DecidableEq

/-- R15 is an admission condition rather than an optimizer rewrite. -/
inductive WellFormednessRuleId where
  | r15
  deriving Repr, DecidableEq

/-- Completion/error/cancellation result retained by pure stream rewrites. -/
inductive StreamOutcome where
  | done
  | error (message : String)
  | cancelled
  deriving Repr, DecidableEq

/--
Denotation used by R1–R9. Construction effects are distinct from element
traversal so R5 can retain source construction while removing traversal.
-/
structure StreamResult (α : Type) where
  constructionEffects : List Effect
  values : List α
  outcome : StreamOutcome
  ownershipSafe : Bool

namespace StreamResult

def map (f : α → β) (stream : StreamResult α) : StreamResult β :=
  { constructionEffects := stream.constructionEffects
    values := stream.values.map f
    outcome := stream.outcome
    ownershipSafe := stream.ownershipSafe }

def filter (predicate : α → Bool) (stream : StreamResult α) : StreamResult α :=
  { stream with values := stream.values.filter predicate }

def limit (count : Nat) (stream : StreamResult α) : StreamResult α :=
  { stream with values := stream.values.take count }

def skip (count : Nat) (stream : StreamResult α) : StreamResult α :=
  { stream with values := stream.values.drop count }

def emptyTraversal (stream : StreamResult α) : StreamResult α :=
  { stream with values := [] }

end StreamResult

/-- A declared unary callable paired with its mathematical interpretation. -/
structure UnaryMeaning (Γ : Env) (inputTy outputTy : Ty) where
  callable : UnaryCallable Γ inputTy outputTy
  apply : Value inputTy → Value outputTy

/-- Semantic law required to justify duplicate elimination for an endomap. -/
def IdempotentLaw {Γ : Env} {ty : Ty}
    (meaning : UnaryMeaning Γ ty ty) : Prop :=
  ∀ value, meaning.apply (meaning.apply value) = meaning.apply value

/-- Complete premises for optimizer duplicate-idempotent Map elimination. -/
structure IdempotentEndomapPremises (Γ : Env) (ty : Ty) where
  meaning : UnaryMeaning Γ ty ty
  pure : meaning.callable.effect = .pure
  total : meaning.callable.properties .total
  declaredIdempotent : meaning.callable.properties .idempotent
  idempotentLaw : IdempotentLaw meaning

/--
Proof-carrying semantic rewrites. Unlike the stable R1–R15 catalogue, this
relation models optimizer implementation certificates and composes individual
steps without treating a property bit as its mathematical law.
-/
inductive CertifiedRewrite :
    {α : Type} → StreamResult α → StreamResult α → Prop where
  | refl {α : Type} (stream : StreamResult α) : CertifiedRewrite stream stream
  | idempotentMap {Γ : Env} {ty : Ty}
      (premises : IdempotentEndomapPremises Γ ty)
      (stream : StreamResult (Value ty)) :
      CertifiedRewrite
        ((stream.map premises.meaning.apply).map premises.meaning.apply)
        (stream.map premises.meaning.apply)
  | trans {α : Type} {first second third : StreamResult α}
      (firstSecond : CertifiedRewrite first second)
      (secondThird : CertifiedRewrite second third) :
      CertifiedRewrite first third

/-- A declared predicate paired with its mathematical interpretation. -/
structure PredicateMeaning (Γ : Env) (inputTy : Ty) where
  callable : UnaryCallable Γ inputTy .bool
  apply : Value inputTy → Bool

/-- A declared reducer paired with its mathematical interpretation. -/
structure BinaryMeaning (Γ : Env) (ty : Ty) where
  callable : BinaryCallable Γ ty ty ty
  apply : Value ty → Value ty → Value ty

/-- Overflow-free addition for a configured machine-count maximum. -/
def satAdd (max left right : Nat) : Nat :=
  min max (left + right)

/-- A parenthesization that retains encounter order through `flatten`. -/
inductive ReductionTree (α : Type) where
  | leaf (value : α)
  | node (left right : ReductionTree α)

namespace ReductionTree

def flatten : ReductionTree α → List α
  | .leaf value => [value]
  | .node left right => left.flatten ++ right.flatten

def eval (combine : α → α → α) : ReductionTree α → α
  | .leaf value => value
  | .node left right => combine (left.eval combine) (right.eval combine)

end ReductionTree

/-- The tail of a non-empty reduction is either empty or tree-parenthesized. -/
inductive ReductionForest (α : Type) where
  | empty
  | tree (tree : ReductionTree α)

namespace ReductionForest

def flatten : ReductionForest α → List α
  | .empty => []
  | .tree reduction => reduction.flatten

def evalFrom (combine : α → α → α) (initial : α) : ReductionForest α → α
  | .empty => initial
  | .tree reduction => combine initial (reduction.eval combine)

end ReductionForest

/-- Whether a parallel implementation preserves order or may permute inputs. -/
inductive ParallelOrder where
  | preservesEncounterOrder
  | mayPermute
  deriving Repr, DecidableEq

/-- Permutation requires an explicit order-independence law. -/
def OrderSafe (combine : α → α → α) : ParallelOrder → Prop
  | .preservesEncounterOrder => True
  | .mayPermute => ∀ left right, combine left right = combine right left

/-- Complete R11 premises, including the semantic interpretation of ASSOC. -/
structure ParallelReducePremises (Γ : Env) (K : KernelCapabilities)
    (ty : Ty) where
  meaning : BinaryMeaning Γ ty
  pure : meaning.callable.effect = .pure
  total : meaning.callable.properties .total
  associative : meaning.callable.properties .associative
  associativeLaw : ∀ first second third,
    meaning.apply (meaning.apply first second) third =
      meaning.apply first (meaning.apply second third)
  noAlias : Γ.hasCapability ty .noAlias
  parallelExecutor : K .parallelExecutor
  splittableSource : K .splittableSource
  order : ParallelOrder
  orderSafe : OrderSafe meaning.apply order

/-- Source facts consumed by reserve and batch execution refinements. -/
structure SourceShape where
  size : Option Nat
  contiguous : Bool
  deriving Repr, DecidableEq

/-- Collector execution capabilities stay separate from semantic declarations. -/
structure CollectorExecutionCapabilities where
  reserve : Bool
  deriving Repr, DecidableEq

def SupportsReserve (capabilities : CollectorExecutionCapabilities) : Prop :=
  capabilities.reserve = true

/-- Segment facts used by direct and batch admission. -/
structure SegmentFacts where
  mayWait : Bool
  hasExecutorBoundary : Bool
  hasAsyncBuffer : Bool
  linear : Bool
  deriving Repr, DecidableEq

def SyncClosed (segment : SegmentFacts) : Prop :=
  segment.mayWait = false ∧
    segment.hasExecutorBoundary = false ∧
    segment.hasAsyncBuffer = false

/-- R15 applies uniformly to every boundary that can outlive a callback frame. -/
inductive AsyncBoundary where
  | wait
  | buffer
  | continuation
  | executorBoundary
  deriving Repr, DecidableEq

/-- Live roots crossing an async boundary must already be suspension-safe. -/
def AsyncPromotionWellFormed (_boundary : AsyncBoundary)
    (context : OwnershipContext) (liveRoots : List PackedValue) : Prop :=
  SuspendSafe context liveRoots

/-- Physical execution form selected after semantic rewrite admission. -/
inductive ExecutionForm where
  | reduce
  | parallelReduce
  | collect
  | reserveCollect (capacity : Nat)
  | scheduled
  | direct
  | scalar
  | batch
  deriving Repr, DecidableEq

/-- An execution form paired with its user-observable semantic result. -/
structure ExecutionVariant (α : Type) where
  form : ExecutionForm
  result : StreamResult α

/-- Execution refinements are not members of the semantic rewrite relation. -/
inductive ExecutionRefines (Γ : Env) (K : KernelCapabilities) :
    {α : Type} → ExecutionVariant α → ExecutionVariant α → Prop where
  | r11 {ty : Ty} (premises : ParallelReducePremises Γ K ty)
      (result : StreamResult (Value ty)) :
      ExecutionRefines Γ K
        { form := .reduce, result := result }
        { form := .parallelReduce, result := result }
  | r12 {inputTy resultTy : Ty} {size : Nat}
      (collector : CollectorDecl Γ inputTy resultTy)
      (source : SourceShape)
      (collectorCapabilities : CollectorExecutionCapabilities)
      (sized : source.size = some size)
      (reserve : SupportsReserve collectorCapabilities)
      (result : StreamResult (Value resultTy)) :
      ExecutionRefines Γ K
        { form := .collect, result := result }
        { form := .reserveCollect size, result := result }
  | r13 {α : Type} (segment : SegmentFacts)
      (syncClosed : SyncClosed segment) (result : StreamResult α) :
      ExecutionRefines Γ K
        { form := .scheduled, result := result }
        { form := .direct, result := result }
  | r14 {ty : Ty} (segment : SegmentFacts) (source : SourceShape)
      (linear : segment.linear = true)
      (contiguous : source.contiguous = true)
      (trivialCopy : Γ.hasCapability ty .trivialCopy)
      (noWait : segment.mayWait = false)
      (batchExecution : K .batchExecution)
      (result : StreamResult (Value ty)) :
      ExecutionRefines Γ K
        { form := .scalar, result := result }
        { form := .batch, result := result }

end CMetaCFlowCalculus.CFlow
