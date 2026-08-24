import CMetaCFlowCalculus.CFlow.MachineRuntime
import CMetaCFlowCalculus.Proofs.MachineRuntime
import PhaseATests.Machine

namespace CMetaCFlowCalculus.Tests.MachineRuntime

open CMetaCFlowCalculus.CFlow.Machine
open CMetaCFlowCalculus.CFlow.MachineRuntime
open CMetaCFlowCalculus.Tests.Machine

def doneConfig : Config :=
  { initialConfig with
    state := 20
    stateValue := { ty := longTy, token := 8 }
    terminal := .done
    trace := [.value longTy 70, .state 20, .done]
    consumedEvents := 1 }

def activeConfig : Config :=
  { initialConfig with
    state := 30
    stateValue := { ty := intTy, token := 9 }
    trace := [.state 30]
    consumedEvents := 1 }

def errorConfig : Config :=
  { initialConfig with
    terminal := .error "no enabled transition"
    trace := [.error "no enabled transition"]
    consumedEvents := 1 }

example : RuntimeStep machine onlyFirstEnabled successfulActions
    initialConfig .wait :=
  .wait

example : projectedStepKind initialConfig doneConfig = .valueAndDone := by
  native_decide

example : projectedStepKind initialConfig activeConfig = .wait := by
  native_decide

example : projectedStepKind initialConfig errorConfig = .error := by
  native_decide

example : RuntimeStep machine onlyFirstEnabled successfulActions
    initialConfig (.transition trigger doneConfig) := by
  apply RuntimeStep.transition
  · show step machine onlyFirstEnabled successfulActions initialConfig trigger =
      some doneConfig
    native_decide
  · native_decide

example : RuntimeStep machine bothEnabled successfulActions
    initialConfig (.transition trigger activeConfig) := by
  apply RuntimeStep.transition
  · show step machine bothEnabled successfulActions initialConfig trigger =
      some activeConfig
    native_decide
  · native_decide

example : RuntimeStep machine (fun _ => false) successfulActions
    initialConfig (.transition trigger errorConfig) := by
  apply RuntimeStep.transition
  · show step machine (fun _ => false) successfulActions initialConfig trigger =
      some errorConfig
    native_decide
  · native_decide

end CMetaCFlowCalculus.Tests.MachineRuntime
