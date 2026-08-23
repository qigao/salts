import CMetaCFlowCalculus.CFlow.Certificate
import CMetaCFlowCalculus.Proofs.Certificate
import PhaseATests.PhaseD

open CMetaCFlowCalculus.CMeta
open CMetaCFlowCalculus.CFlow

namespace CMetaCFlowCalculus.Tests.PhaseG

def mapRow : CertificateRow where
  opcode := .map
  instructionIndex := 0
  callableIndex := 0
  inputTy := PhaseD.scalarTy
  outputTy := PhaseD.scalarTy
  callableIdentity := "increment"
  effects := .pure
  properties := PhaseD.pureTotal

def reduceRow : CertificateRow where
  opcode := .reduce
  instructionIndex := 1
  callableIndex := 0
  inputTy := PhaseD.scalarTy
  outputTy := PhaseD.scalarTy
  callableIdentity := "sum"
  effects := .pure
  properties := PropertySet.ofList [.total, .associative]

def sequentialCertificate : PlanCertificate where
  version := .v1
  graphVersion := 7
  graphFingerprint := 11
  path := .sequential
  order := .notApplicable
  requiredCapabilities := 0
  rows := [mapRow, reduceRow]

def orderedParallelCertificate : PlanCertificate :=
  { sequentialCertificate with
    path := .orderedParallelReduce
    order := .encounter
    requiredCapabilities := concurrentExecutorCapabilityMask }

def traceSemantics : Semantics PhaseD.scalarTy PhaseD.scalarTy :=
  fun _ input =>
    { events := input.map Observation.value ++ [.done]
      ownershipSafe := true }

theorem validSequential : CertificateValid sequentialCertificate 7 11
    [mapRow, reduceRow] [mapRow, reduceRow]
    traceSemantics traceSemantics where
  version := rfl
  graphIdentity := ⟨rfl, rfl⟩
  graphRowsAgreement := rfl
  planRowsAgreement := rfl
  typeChain := by simp [RowsTypeChain, sequentialCertificate, mapRow, reduceRow]
  path := by simp [CertificatePathWellFormed, sequentialCertificate]
  refinement := certificateRowsRefine_refl _ _

example : ObsEq traceSemantics traceSemantics :=
  certificate_preserves_observation validSequential rfl

def orderedChunks : ContiguousNonemptyChunks (Value PhaseD.scalarTy) where
  source := [PhaseD.valueTwo, PhaseD.valueThree, PhaseD.valueFour]
  reduction := PhaseD.valueReductionTree
  preservesEncounterOrder := rfl

example :
    orderedChunks.source.foldl PhaseD.sumMeaning.apply PhaseD.valueOne =
      PhaseD.sumMeaning.apply PhaseD.valueOne
        (orderedChunks.reduction.eval PhaseD.sumMeaning.apply) :=
  ordered_chunks_reduce_eq PhaseD.parallelPremises PhaseD.valueOne orderedChunks

theorem validParallel : CertificateValid orderedParallelCertificate 7 11
    [mapRow, reduceRow] [mapRow, reduceRow]
    traceSemantics traceSemantics where
  version := rfl
  graphIdentity := ⟨rfl, rfl⟩
  graphRowsAgreement := rfl
  planRowsAgreement := rfl
  typeChain := by simp [RowsTypeChain, orderedParallelCertificate,
    sequentialCertificate, mapRow, reduceRow]
  path := by
    simp [CertificatePathWellFormed, ParallelTerminal,
      orderedParallelCertificate, sequentialCertificate, mapRow, reduceRow,
      PhaseD.pureTotal, PropertySet.ofList]
  refinement := certificateRowsRefine_refl _ _

example :
    ObsEq traceSemantics traceSemantics ∧
      ({ form := .reduce, result := PhaseD.valueStream } :
          ExecutionVariant _).result =
        ({ form := .parallelReduce, result := PhaseD.valueStream } :
          ExecutionVariant _).result :=
  parallel_certificate_preserves_observation validParallel
    rfl PhaseD.parallelPremises PhaseD.valueStream

def missingAssociativityRow : CertificateRow :=
  { reduceRow with properties := PropertySet.ofList [.total] }

def missingAssociativityCertificate : PlanCertificate :=
  { orderedParallelCertificate with rows := [mapRow, missingAssociativityRow] }

example : ¬CertificatePathWellFormed missingAssociativityCertificate := by
  simp [CertificatePathWellFormed, ParallelTerminal,
    missingAssociativityCertificate, orderedParallelCertificate,
    sequentialCertificate, missingAssociativityRow, reduceRow,
    PropertySet.ofList]

def reversedReduction : ReductionTree (Value PhaseD.scalarTy) :=
  .node (.leaf PhaseD.valueTwo) (.leaf PhaseD.valueOne)

example : ¬PreservesEncounterOrder
    [PhaseD.valueOne, PhaseD.valueTwo] reversedReduction := by
  intro reversed
  have tokens := congrArg (List.map Value.token) reversed
  simp [reversedReduction, ReductionTree.flatten,
    PhaseD.valueOne, PhaseD.valueTwo] at tokens

example : ¬CertificateIdentity sequentialCertificate 8 11 := by
  simp [CertificateIdentity, sequentialCertificate]

/-- error: Unknown constant -/
#guard_msgs(error, substring := true) in
example : CertifiedOpcode := .wait

end CMetaCFlowCalculus.Tests.PhaseG
