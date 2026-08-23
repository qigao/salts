import CMetaCFlowCalculus.CMeta.Effects
import CMetaCFlowCalculus.CMeta.Properties
import CMetaCFlowCalculus.CMeta.Types

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
  rows : List CertificateRow

example : CertificateVersion.toNat .v1 = 1 := rfl
example : CertifiedOpcode.toNat .filter = 0 := rfl
example : CertifiedOpcode.toNat .map = 1 := rfl
example : CertifiedOpcode.toNat .flatMap = 2 := rfl
example : CertifiedOpcode.toNat .reduce = 3 := rfl
example : CertifiedPath.toNat .sequential = 0 := rfl
example : CertifiedPath.toNat .orderedParallelReduce = 1 := rfl
example : CertifiedOrder.toNat .notApplicable = 0 := rfl
example : CertifiedOrder.toNat .encounter = 1 := rfl

end CMetaCFlowCalculus.CFlow
