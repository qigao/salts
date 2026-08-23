import CMetaCFlowCalculus.CMeta.Effects
import CMetaCFlowCalculus.CMeta.Properties
import CMetaCFlowCalculus.CMeta.Types
import CMetaCFlowCalculus.CFlow.Observation
import CMetaCFlowCalculus.CFlow.Rewrite

namespace CMetaCFlowCalculus.CFlow

open CMetaCFlowCalculus.CMeta

inductive CertificateVersion where
  | v1
  deriving Repr, DecidableEq

def CertificateVersion.toNat : CertificateVersion → Nat
  | .v1 => 1

inductive CertifiedOpcode where
  | filter | map | flatMap | reduce
  deriving Repr, DecidableEq

def CertifiedOpcode.toNat : CertifiedOpcode → Nat
  | .filter => 0
  | .map => 1
  | .flatMap => 2
  | .reduce => 3

inductive CertifiedPath where
  | sequential | orderedParallelReduce
  deriving Repr, DecidableEq

def CertifiedPath.toNat : CertifiedPath → Nat
  | .sequential => 0
  | .orderedParallelReduce => 1

inductive CertifiedOrder where
  | notApplicable | encounter
  deriving Repr, DecidableEq

def CertifiedOrder.toNat : CertifiedOrder → Nat
  | .notApplicable => 0
  | .encounter => 1

/-- C `CMETA_EXEC_CAP_CONCURRENT`; kept explicit because the C witness stores
capabilities as a stable bit mask. -/
def concurrentExecutorCapabilityMask : Nat := 4

structure CertificateRow where
  opcode : CertifiedOpcode
  instructionIndex : Nat
  callableIndex : Nat
  inputTy : Ty
  outputTy : Ty
  callableIdentity : String
  effects : Effect
  properties : PropertySet

/-- Runtime certificate schema mirrored by C. Fingerprints and versions bind a
certificate to one process-local normalized Graph; this is not a wire format. -/
structure PlanCertificate where
  version : CertificateVersion
  graphVersion : Nat
  graphFingerprint : Nat
  path : CertifiedPath
  order : CertifiedOrder
  requiredCapabilities : Nat
  rows : List CertificateRow

/-- Exact Graph identity carried by the runtime certificate. -/
def CertificateIdentity (certificate : PlanCertificate)
    (graphVersion graphFingerprint : Nat) : Prop :=
  certificate.graphVersion = graphVersion ∧
    certificate.graphFingerprint = graphFingerprint

/-- The ordered rows connect the artifact's declared input and output types. -/
def RowsTypeChain : Ty → Ty → List CertificateRow → Prop
  | inputTy, outputTy, [] => inputTy = outputTy
  | inputTy, outputTy, row :: rows =>
      row.inputTy = inputTy ∧ RowsTypeChain row.outputTy outputTy rows

/-- The final row admitted for ordered parallel reduction. C checks these
declarations; the corresponding mathematical law remains an R11 premise. -/
def ParallelTerminal : List CertificateRow → Prop
  | [] => False
  | [row] =>
      row.opcode = .reduce ∧ row.effects = .pure ∧
        row.properties .total ∧ row.properties .associative
  | _ :: rows => ParallelTerminal rows

/-- Path-local requirements mirrored by the C certificate builder. -/
def CertificatePathWellFormed (certificate : PlanCertificate) : Prop :=
  match certificate.path with
  | .sequential =>
      certificate.order = .notApplicable ∧
        certificate.requiredCapabilities = 0
  | .orderedParallelReduce =>
      certificate.order = .encounter ∧
        certificate.requiredCapabilities = concurrentExecutorCapabilityMask ∧
        ParallelTerminal certificate.rows

/-- A row-labelled chain of observational refinement obligations. Each step
is a proof obligation for compiler conformance, not a claim about arbitrary C
function bodies. -/
inductive CertificateRowsRefine {inputTy outputTy : Ty} :
    List CertificateRow → Semantics inputTy outputTy →
      Semantics inputTy outputTy → Prop where
  | nil (semantics : Semantics inputTy outputTy) :
      CertificateRowsRefine [] semantics semantics
  | cons (row : CertificateRow) {rows : List CertificateRow}
      {before middle after : Semantics inputTy outputTy}
      (head : ObsEq before middle)
      (tail : CertificateRowsRefine rows middle after) :
      CertificateRowsRefine (row :: rows) before after

/-- Exact certificate validity: Graph rows, Plan rows and certificate rows are
the same ordered semantic records; their type chain, identity, path premises,
and row-by-row observation refinement all hold. -/
structure CertificateValid {inputTy outputTy : Ty}
    (certificate : PlanCertificate)
    (graphVersion graphFingerprint : Nat)
    (graphRows planRows : List CertificateRow)
    (graphSemantics planSemantics : Semantics inputTy outputTy) : Prop where
  version : certificate.version = .v1
  graphIdentity : CertificateIdentity certificate graphVersion graphFingerprint
  graphRowsAgreement : certificate.rows = graphRows
  planRowsAgreement : certificate.rows = planRows
  typeChain : RowsTypeChain inputTy outputTy certificate.rows
  path : CertificatePathWellFormed certificate
  refinement : CertificateRowsRefine certificate.rows graphSemantics planSemantics

/-- A reduction tree retains the source order exactly. Internal subtrees model
nonempty contiguous chunks; `ReductionTree` has no empty constructor. -/
def PreservesEncounterOrder (source : List α)
    (reduction : ReductionTree α) : Prop :=
  reduction.flatten = source

structure ContiguousNonemptyChunks (α : Type) where
  source : List α
  reduction : ReductionTree α
  preservesEncounterOrder : PreservesEncounterOrder source reduction

example : CertificateVersion.toNat .v1 = 1 := rfl
example : CertifiedOpcode.toNat .filter = 0 := rfl
example : CertifiedOpcode.toNat .map = 1 := rfl
example : CertifiedOpcode.toNat .flatMap = 2 := rfl
example : CertifiedOpcode.toNat .reduce = 3 := rfl
example : CertifiedPath.toNat .sequential = 0 := rfl
example : CertifiedPath.toNat .orderedParallelReduce = 1 := rfl
example : CertifiedOrder.toNat .notApplicable = 0 := rfl
example : CertifiedOrder.toNat .encounter = 1 := rfl
example : concurrentExecutorCapabilityMask = 4 := rfl

end CMetaCFlowCalculus.CFlow
