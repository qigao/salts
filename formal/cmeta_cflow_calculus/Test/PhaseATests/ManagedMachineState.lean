import CMetaCFlowCalculus.CFlow.ManagedMachineState
import CMetaCFlowCalculus.Proofs.ManagedMachineState

namespace CMetaCFlowCalculus.Tests.ManagedMachineState

open CMetaCFlowCalculus.CMeta
open CMetaCFlowCalculus.CFlow.MachineRuntime
open CMetaCFlowCalculus.CFlow.ManagedMachineState

def stateTy : Ty := .named "ManagedState"

def sourceValue : ManagedValue :=
  { ty := stateTy, token := 1 }

def targetValue : ManagedValue :=
  { ty := stateTy, token := 2 }

def duplicateTarget : ManagedValue :=
  { ty := .named "ReplacementState", token := sourceValue.token }

example : stageCopy (ready sourceValue) targetValue false = ready sourceValue :=
  copy_failure_preserves_control sourceValue targetValue

example : stageCopy (ready sourceValue) duplicateTarget true = ready sourceValue :=
  occupied_token_stage_preserves_control sourceValue duplicateTarget rfl

example : (ready sourceValue).Balanced :=
  ready_balanced sourceValue

example :
    ∃ settled,
      cancelPath sourceValue targetValue = some settled ∧
      settled.source = some sourceValue ∧
      settled.staged = none ∧
      settled.ledger sourceValue.token = some (ResourceRecord.live sourceValue.ty) ∧
      settled.ledger targetValue.token = some (ResourceRecord.destroyed targetValue.ty) ∧
      settled.completed = 0 ∧
      settled.cancelled = 1 ∧
      settled.Balanced := by
  exact cancel_path_preserves_source_and_destroys_target
    sourceValue targetValue (by decide)

example :
    ∃ settled,
      commitPath sourceValue targetValue = some settled ∧
      settled.source = some targetValue ∧
      settled.staged = none ∧
      settled.ledger sourceValue.token = some (ResourceRecord.destroyed sourceValue.ty) ∧
      settled.ledger targetValue.token = some (ResourceRecord.live targetValue.ty) ∧
      settled.completed = 1 ∧
      settled.cancelled = 0 ∧
      settled.Balanced := by
  exact commit_path_moves_target_and_destroys_source
    sourceValue targetValue (by decide)

example :
    ∃ disposed,
      cancelPath sourceValue targetValue >>= dispose = some disposed ∧
      disposed.source = none ∧
      disposed.staged = none ∧
      disposed.ledger sourceValue.token =
        some (ResourceRecord.destroyed sourceValue.ty) ∧
      disposed.ledger targetValue.token =
        some (ResourceRecord.destroyed targetValue.ty) ∧
      disposed.liveCount = 0 ∧
      disposed.Balanced ∧
      dispose disposed = none := by
  exact cancel_path_disposes_every_value_once
    sourceValue targetValue (by decide)

example :
    ∃ disposed,
      commitPath sourceValue targetValue >>= dispose = some disposed ∧
      disposed.source = none ∧
      disposed.staged = none ∧
      disposed.ledger sourceValue.token =
        some (ResourceRecord.destroyed sourceValue.ty) ∧
      disposed.ledger targetValue.token =
        some (ResourceRecord.destroyed targetValue.ty) ∧
      disposed.liveCount = 0 ∧
      disposed.Balanced ∧
      dispose disposed = none := by
  exact commit_path_disposes_every_value_once
    sourceValue targetValue (by decide)

end CMetaCFlowCalculus.Tests.ManagedMachineState
