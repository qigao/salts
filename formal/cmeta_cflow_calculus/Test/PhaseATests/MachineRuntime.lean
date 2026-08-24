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

def executingControl : CommitControl :=
  CommitControl.executing initialConfig doneConfig

example : executingControl.Valid :=
  CommitControl.executing_valid initialConfig doneConfig

example :
    CommitControl.beginCommit
      (CommitControl.requestCancel executingControl) = none :=
  CommitControl.cancel_before_begin_commit initialConfig doneConfig

example :
    let after := CommitControl.Legacy.unconditionalCommit
      (CommitControl.requestCancel executingControl)
    after.lifecycle = .cancelRequested ∧
      after.source = doneConfig ∧
      after.source ≠ initialConfig ∧
      after.completed = 1 := by
  exact CommitControl.legacy_cancel_then_commit_counterexample
    initialConfig doneConfig (by native_decide)

example :
    ∃ after,
      CommitControl.discardCancelled
          (CommitControl.requestCancel executingControl) = some after ∧
      after.source = initialConfig ∧
      after.completed = 0 ∧
      after.cancelled = 1 := by
  obtain ⟨after, settled, _, _, source, _, completed, cancelled⟩ :=
    CommitControl.cancel_before_commit_preserves_source
      initialConfig doneConfig
  exact ⟨after, settled, source, completed, cancelled⟩

example :
    ∃ begun after,
      CommitControl.beginCommit executingControl = some begun ∧
      CommitControl.commit (CommitControl.requestCancel begun) = some after ∧
      after.source = doneConfig ∧
      after.completed = 1 ∧
      after.cancelled = 0 := by
  obtain ⟨begun, after, admitted, committed, _, _, source, _, completed,
      cancelled⟩ :=
    CommitControl.begin_commit_before_cancel_commits_once
      initialConfig doneConfig
  exact ⟨begun, after, admitted, committed, source, completed, cancelled⟩

end CMetaCFlowCalculus.Tests.MachineRuntime
