import CMetaCFlowCalculus.Proofs.IOExecutor

open CMetaCFlowCalculus.IO.Executor

namespace CMetaCFlowCalculus.Tests.IOExecutor

def initial : State Nat :=
  { capacity := 1
    parallelism := 1
    capability := .serial
    terminal := .open
    queue := []
    running := []
    nextId := 1 }

theorem initialValid : initial.Valid := by
  simp [State.Valid, initial]

theorem initialIdentifiersValid : initial.IdentifiersValid := by
  simp [State.IdentifiersValid, State.knownIds, initial]

def posted : PostResult Nat := tryPost initial 7
def started : StartResult Nat := start posted.state

example : posted.status = .accepted := by native_decide
example : posted.taskId = some 1 := by native_decide
example : (tryPost posted.state 8).status = .full := by native_decide
example : started.status = .started := by native_decide
example : started.task.map Task.payload = some 7 := by native_decide
example : started.state.running = [1] := by native_decide
example : (start started.state).status = .saturated := by native_decide
example : (finish started.state 1).status = .finished := by native_decide
example : (finish (finish started.state 1).state 1).status = .notFound := by
  native_decide
example : (tryPost (shutdown initial).state 7).status = .closed := by
  native_decide

example : posted.state.Valid := tryPost_preserves_valid initial 7 initialValid
example : posted.state.IdentifiersValid :=
  tryPost_preserves_identifiers initial 7 initialIdentifiersValid
example : started.state.Valid := start_preserves_valid posted.state
  (tryPost_preserves_valid initial 7 initialValid)
example : started.state.IdentifiersValid :=
  start_preserves_identifiers posted.state
    (tryPost_preserves_identifiers initial 7 initialIdentifiersValid)
example : (finish started.state 1).state.Valid :=
  finish_preserves_valid started.state 1
    (start_preserves_valid posted.state
      (tryPost_preserves_valid initial 7 initialValid))
example : (finish started.state 1).state.IdentifiersValid :=
  finish_preserves_identifiers started.state 1
    (start_preserves_identifiers posted.state
      (tryPost_preserves_identifiers initial 7 initialIdentifiersValid))
example : (shutdown posted.state).state.Valid :=
  shutdown_preserves_valid posted.state
    (tryPost_preserves_valid initial 7 initialValid)

end CMetaCFlowCalculus.Tests.IOExecutor
