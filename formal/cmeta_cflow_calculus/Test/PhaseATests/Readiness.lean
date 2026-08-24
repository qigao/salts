import CMetaCFlowCalculus.CFlow.Readiness
import CMetaCFlowCalculus.Proofs.Readiness

namespace CMetaCFlowCalculus.Tests.Readiness

open CMetaCFlowCalculus.CFlow.Readiness

example : nextGeneration 41 42 = some 42 := by decide
example : nextGeneration 42 42 = none := by decide

def registering : Slot := Slot.zero.beginRegister
def registered : Slot := registering.commitRegister
def arming : Slot := registered.beginArm
def armed : Slot := arming.commitArm
def firing : Slot := armed.dispatch
def rearmable : Slot := firing.completeCallback

example : Slot.zero.Valid := zero_valid

example : Step Slot.zero registering :=
  .registerBegin Slot.zero rfl

example : Step registering registered :=
  .registerCommit registering rfl rfl rfl rfl rfl rfl rfl rfl rfl

example : Step registered arming :=
  .armBegin registered rfl rfl rfl rfl rfl rfl

example : Step arming armed :=
  .armCommit arming rfl rfl rfl rfl rfl

example : Step armed firing :=
  .normalDispatch armed rfl rfl rfl rfl rfl

example : Step firing rearmable :=
  .callbackComplete firing (by decide) rfl rfl (by decide)

example : rearmable.lifecycle = .open ∧ rearmable.interest = .idle ∧
    rearmable.delivery = .idle := by
  decide

example : decideArm false true firing = .wait := by decide
example : decideArm true true firing = .busy := by decide
example : decideArm false true rearmable = .proceed := by decide
example : decideShutdown true false false false = .busy := by decide
example : decideShutdown false true false true = .wait := by decide

example : Admission.zero.enter 8 = some
    { closed := false, entrants := 1, slotEpoch := none } := by
  decide

example : ((Admission.zero.enter 8).bind Admission.reserveRegister) = none := by
  decide

end CMetaCFlowCalculus.Tests.Readiness
