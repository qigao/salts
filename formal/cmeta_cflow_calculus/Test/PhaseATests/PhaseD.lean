import CMetaCFlowCalculus.CFlow.Rewrite
import CMetaCFlowCalculus.Proofs.Rewrite
import CMetaCFlowCalculus.Proofs.Ownership

open CMetaCFlowCalculus.CMeta
open CMetaCFlowCalculus.CFlow

namespace CMetaCFlowCalculus.Tests.PhaseD

example : ruleClass .r1 = .semanticRewrite := rfl
example : ruleClass .r10 = .semanticRewrite := rfl
example : ruleClass .r11 = .executionRefinement := rfl
example : ruleClass .r14 = .executionRefinement := rfl
example : ruleClass .r15 = .wellFormedness := rfl

example : SemanticRuleId := .r1
example : SemanticRuleId := .r10
example : ExecutionRefinementRuleId := .r11
example : ExecutionRefinementRuleId := .r14
example : WellFormednessRuleId := .r15

def baseStream : StreamResult Nat where
  constructionEffects := [.external]
  values := [1, 2, 3]
  outcome := .done
  ownershipSafe := true

example : baseStream.limit 2 =
    { baseStream with values := [1, 2] } := rfl

example : baseStream.skip 1 =
    { baseStream with values := [2, 3] } := rfl

def scalarTy : Ty := .named "Scalar"

def permissiveEnv : Env where
  hasCapability := fun _ _ => True
  declaresUnary := fun _ _ _ _ _ => True
  declaresBinary := fun _ _ _ _ _ _ => True
  declaresSource := fun _ _ => True
  declaresCollector := fun _ _ _ => True

def pureTotal : PropertySet := PropertySet.ofList [.total]

def incrementCallable : UnaryCallable permissiveEnv scalarTy scalarTy where
  name := "increment"
  effect := .pure
  properties := pureTotal
  declared := trivial

def positiveCallable : UnaryCallable permissiveEnv scalarTy .bool where
  name := "positive"
  effect := .pure
  properties := pureTotal
  declared := trivial

def sumCallable : BinaryCallable permissiveEnv scalarTy scalarTy scalarTy where
  name := "sum"
  effect := .pure
  properties := PropertySet.ofList [.total, .associative]
  declared := trivial

def incrementMeaning : UnaryMeaning permissiveEnv scalarTy scalarTy where
  callable := incrementCallable
  apply := fun value => { token := value.token + 1 }

def positiveMeaning : PredicateMeaning permissiveEnv scalarTy where
  callable := positiveCallable
  apply := fun value => decide (0 < value.token)

def sumMeaning : BinaryMeaning permissiveEnv scalarTy where
  callable := sumCallable
  apply := fun left right => { token := left.token + right.token }

example : incrementMeaning.callable.effect = .pure := rfl
example : positiveMeaning.apply { token := 0 } = false := rfl
example : (sumMeaning.apply { token := 2 } { token := 3 }).token = 5 := rfl

example : satAdd 5 3 4 = 5 := rfl

def orderedTree : ReductionTree Nat :=
  .node (.node (.leaf 1) (.leaf 2)) (.node (.leaf 3) (.leaf 4))

example : orderedTree.flatten = [1, 2, 3, 4] := rfl
example : orderedTree.eval (· + ·) = 10 := rfl

def reducedVariant : ExecutionVariant Nat where
  form := .reduce
  result := baseStream

def identityCallable : UnaryCallable permissiveEnv scalarTy scalarTy where
  name := "identity"
  effect := .pure
  properties := pureTotal
  declared := trivial

def identityMeaning : UnaryMeaning permissiveEnv scalarTy scalarTy where
  callable := identityCallable
  apply := fun value => value

def doubleCallable : UnaryCallable permissiveEnv scalarTy scalarTy where
  name := "double"
  effect := .pure
  properties := pureTotal
  declared := trivial

def doubleMeaning : UnaryMeaning permissiveEnv scalarTy scalarTy where
  callable := doubleCallable
  apply := fun value => { token := value.token * 2 }

def alwaysCallable : UnaryCallable permissiveEnv scalarTy .bool where
  name := "always"
  effect := .pure
  properties := pureTotal
  declared := trivial

def alwaysMeaning : PredicateMeaning permissiveEnv scalarTy where
  callable := alwaysCallable
  apply := fun _ => true

def evenCallable : UnaryCallable permissiveEnv scalarTy .bool where
  name := "even"
  effect := .pure
  properties := pureTotal
  declared := trivial

def evenMeaning : PredicateMeaning permissiveEnv scalarTy where
  callable := evenCallable
  apply := fun value => decide (value.token % 2 = 0)

def valueOne : Value scalarTy := { token := 1 }
def valueTwo : Value scalarTy := { token := 2 }
def valueThree : Value scalarTy := { token := 3 }

def valueStream : StreamResult (Value scalarTy) where
  constructionEffects := [.external]
  values := [valueOne, valueTwo, valueThree]
  outcome := .done
  ownershipSafe := true

theorem pureTotalHasTotal : pureTotal .total := by
  simp [pureTotal, PropertySet.ofList]

example : valueStream.map identityMeaning.apply = valueStream :=
  r1_map_identity identityMeaning rfl pureTotalHasTotal (by intro; rfl)
    valueStream

example : (valueStream.map incrementMeaning.apply).map doubleMeaning.apply =
    valueStream.map (fun value => doubleMeaning.apply (incrementMeaning.apply value)) :=
  r2_map_fusion incrementMeaning doubleMeaning rfl pureTotalHasTotal rfl
    pureTotalHasTotal valueStream

example : valueStream.filter alwaysMeaning.apply = valueStream :=
  r3_filter_true alwaysMeaning rfl pureTotalHasTotal (by intro; rfl)
    valueStream

example : (valueStream.filter positiveMeaning.apply).filter evenMeaning.apply =
    valueStream.filter
      (fun value => positiveMeaning.apply value && evenMeaning.apply value) :=
  r4_filter_fusion positiveMeaning evenMeaning rfl pureTotalHasTotal rfl
    pureTotalHasTotal valueStream

example : valueStream.limit 0 = valueStream.emptyTraversal :=
  r5_limit_zero valueStream

example : (valueStream.limit 2).limit 1 = valueStream.limit (min 1 2) :=
  r6_nested_limit 1 2 valueStream

example : valueStream.skip 0 = valueStream :=
  r7_skip_zero valueStream

example : (valueStream.skip 2).skip 2 = valueStream.skip (satAdd 3 2 2) :=
  r8_nested_skip 3 2 2 valueStream (by decide)

example : (valueStream.map incrementMeaning.apply).limit 2 =
    (valueStream.limit 2).map incrementMeaning.apply :=
  r9_limit_map incrementMeaning rfl pureTotalHasTotal 2 valueStream

def valueFour : Value scalarTy := { token := 4 }

def valueReductionTree : ReductionTree (Value scalarTy) :=
  .node (.node (.leaf valueTwo) (.leaf valueThree)) (.leaf valueFour)

def valueReductionForest : ReductionForest (Value scalarTy) :=
  .tree valueReductionTree

theorem sumAssociative :
    ∀ first second third,
      sumMeaning.apply (sumMeaning.apply first second) third =
        sumMeaning.apply first (sumMeaning.apply second third) := by
  intro first second third
  cases first
  cases second
  cases third
  simp [sumMeaning, Nat.add_assoc]

theorem sumDeclaredAssociative : sumMeaning.callable.properties .associative := by
  simp [sumMeaning, sumCallable, PropertySet.ofList]

theorem sumDeclaredTotal : sumMeaning.callable.properties .total := by
  simp [sumMeaning, sumCallable, PropertySet.ofList]

example : List.foldl sumMeaning.apply valueOne valueReductionForest.flatten =
    valueReductionForest.evalFrom sumMeaning.apply valueOne :=
  r10_reduce_reassociation sumMeaning rfl sumDeclaredTotal
    sumDeclaredAssociative sumAssociative valueOne valueReductionForest

example :
    (valueReductionForest.evalFrom sumMeaning.apply valueOne).token = 10 := rfl

def parallelK : KernelCapabilities := fun capability =>
  capability = .parallelExecutor ∨ capability = .splittableSource

def parallelPremises : ParallelReducePremises permissiveEnv parallelK scalarTy where
  meaning := sumMeaning
  pure := rfl
  total := sumDeclaredTotal
  associative := sumDeclaredAssociative
  associativeLaw := sumAssociative
  noAlias := trivial
  parallelExecutor := Or.inl rfl
  splittableSource := Or.inr rfl
  order := .preservesEncounterOrder
  orderSafe := trivial

theorem r11Witness : ExecutionRefines permissiveEnv parallelK
    { form := .reduce, result := valueStream }
    { form := .parallelReduce, result := valueStream } :=
  .r11 parallelPremises valueStream

example :
    ({ form := .reduce, result := valueStream } : ExecutionVariant _).result =
      ({ form := .parallelReduce, result := valueStream } :
        ExecutionVariant _).result :=
  execution_refinement_preserves_semantics r11Witness

def noParallelK : KernelCapabilities := fun _ => False

example : ¬Nonempty (ParallelReducePremises permissiveEnv noParallelK scalarTy) := by
  intro existsPremises
  obtain ⟨premises⟩ := existsPremises
  exact premises.parallelExecutor

def keepLeftCallable : BinaryCallable permissiveEnv scalarTy scalarTy scalarTy where
  name := "keepLeft"
  effect := .pure
  properties := PropertySet.ofList [.total, .associative]
  declared := trivial

def keepLeftMeaning : BinaryMeaning permissiveEnv scalarTy where
  callable := keepLeftCallable
  apply := fun left _ => left

example : ¬OrderSafe keepLeftMeaning.apply .mayPermute := by
  intro safe
  have equalTokens := congrArg Value.token (safe valueOne valueTwo)
  simp [keepLeftMeaning, valueOne, valueTwo] at equalTokens

def scalarCollector : CollectorDecl permissiveEnv scalarTy scalarTy where
  name := "scalars"
  declared := trivial

def sizedContiguousSource : SourceShape where
  size := some 3
  contiguous := true

def reservingCollector : CollectorExecutionCapabilities where
  reserve := true

theorem r12Witness : ExecutionRefines permissiveEnv parallelK
    { form := .collect, result := valueStream }
    { form := .reserveCollect 3, result := valueStream } :=
  .r12 scalarCollector sizedContiguousSource reservingCollector rfl rfl valueStream

def syncLinearSegment : SegmentFacts where
  mayWait := false
  hasExecutorBoundary := false
  hasAsyncBuffer := false
  linear := true

theorem syncSegmentClosed : SyncClosed syncLinearSegment := by
  exact ⟨rfl, rfl, rfl⟩

theorem r13Witness : ExecutionRefines permissiveEnv parallelK
    { form := .scheduled, result := baseStream }
    { form := .direct, result := baseStream } :=
  .r13 syncLinearSegment syncSegmentClosed baseStream

def batchK : KernelCapabilities := fun capability =>
  capability = .batchExecution

theorem r14Witness : ExecutionRefines permissiveEnv batchK
    { form := .scalar, result := valueStream }
    { form := .batch, result := valueStream } :=
  ExecutionRefines.r14 (Γ := permissiveEnv) (K := batchK)
    syncLinearSegment sizedContiguousSource rfl rfl trivial rfl rfl valueStream

example :
    ({ form := .collect, result := valueStream } : ExecutionVariant _).result =
      ({ form := .reserveCollect 3, result := valueStream } :
        ExecutionVariant _).result :=
  execution_refinement_preserves_semantics r12Witness

example :
    ({ form := .scheduled, result := baseStream } : ExecutionVariant _).result =
      ({ form := .direct, result := baseStream } : ExecutionVariant _).result :=
  execution_refinement_preserves_semantics r13Witness

example :
    ({ form := .scalar, result := valueStream } : ExecutionVariant _).result =
      ({ form := .batch, result := valueStream } : ExecutionVariant _).result :=
  execution_refinement_preserves_semantics r14Witness

def noReserveCollector : CollectorExecutionCapabilities where
  reserve := false

example : ¬SupportsReserve noReserveCollector := by
  simp [SupportsReserve, noReserveCollector]

def waitingSegment : SegmentFacts where
  mayWait := true
  hasExecutorBoundary := false
  hasAsyncBuffer := false
  linear := true

example : ¬SyncClosed waitingSegment := by
  simp [SyncClosed, waitingSegment]

example : ¬noParallelK .batchExecution := by simp [noParallelK]

def liveValue : Value scalarTy := { token := 77 }

def ownedContext : OwnershipContext := fun token =>
  if token = liveValue.token then
    some { ty := scalarTy, ownership := .owned }
  else none

def borrowedContext : OwnershipContext := fun token =>
  if token = liveValue.token then
    some { ty := scalarTy, ownership := .borrowed }
  else none

theorem ownedLiveSafe : SuspendSafe ownedContext [liveValue.pack] := by
  intro value member
  simp only [List.mem_cons, List.not_mem_nil, or_false] at member
  subst value
  simp [ownedContext, liveValue, Value.pack]

example (boundary : AsyncBoundary) :
    AsyncPromotionWellFormed boundary ownedContext [liveValue.pack] :=
  ownedLiveSafe

example (boundary : AsyncBoundary) :
    ¬AsyncPromotionWellFormed boundary borrowedContext [liveValue.pack] := by
  intro wellFormed
  have safe := r15_async_promotion_requires_owned wellFormed
  have owned := safe liveValue.pack (by simp)
  simp [borrowedContext, liveValue, Value.pack] at owned

theorem borrowedLive : HasOwnership borrowedContext liveValue .borrowed := by
  rfl

def retainedContext : OwnershipContext :=
  retain (Γ := permissiveEnv) borrowedContext liveValue borrowedLive trivial

example (boundary : AsyncBoundary) :
    AsyncPromotionWellFormed boundary retainedContext [liveValue.pack] :=
  retain_suspend_safe (Γ := permissiveEnv) borrowedContext liveValue
    borrowedLive trivial

end CMetaCFlowCalculus.Tests.PhaseD
