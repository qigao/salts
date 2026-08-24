import CMetaCFlowCalculus.CFlow.ExecutorProtocol
import CMetaCFlowCalculus.Proofs.ExecutorProtocol

namespace CMetaCFlowCalculus.Tests.ExecutorProtocol

open CMetaCFlowCalculus.CFlow.MachineRuntime
open CMetaCFlowCalculus.CFlow.ExecutorProtocol

def serial : State := initial .serial .drain 1

example : serial.Safe := initial_safe .serial .drain 1

example :
    let admitted := tryPost serial .external
    admitted.result = .accepted ∧ admitted.state.queued = 1 ∧
      admitted.state.accepted = 1 ∧ admitted.state.Safe := by
  exact accepted_post_is_bounded_and_conserved serial
    (initial_safe .serial .drain 1) rfl (by decide)

example :
    let full := tryPost (tryPost serial .external).state .external
    full.result = .full ∧ full.state.accepted = 1 ∧
      full.state.queued = 1 := by
  native_decide

example :
    let withOne := (tryPost (initial .serial .drain 2) .external).state
    let withTwo := (tryPost withOne .external).state
    let started := start withTwo
    started.result = .started ∧ started.state.running = 1 ∧
      start started.state = { result := .serialBusy, state := started.state } := by
  native_decide

example :
    let closing := beginShutdown (tryPost serial .external).state
    (tryPost closing .external).result = .closed ∧
      closing.lifecycle = .closing := by
  native_decide

example :
    let cancelled := beginShutdown (tryPost
      (initial .serial .cancelPending 1) .external).state
    cancelled.queued = 0 ∧ cancelled.cancelled = 1 ∧ cancelled.Conserved := by
  constructor
  · native_decide
  constructor
  · native_decide
  · exact state_conserved _

example :
    let full := (tryPost serial .external).state
    (blockingPost full (.callback full.id)).result = .wouldBlock ∧
      (waitIdle full (.callback full.id)).result = .wouldBlock := by
  exact self_blocking_operations_fail_fast _ rfl (by native_decide)

example :
    let scheduled := scheduleMachine (MachineControl.ready serial)
    scheduled.result = .accepted ∧
      scheduled.state.worker = .scheduled ∧
      scheduled.state.executor.queued = 1 := by
  native_decide

example :
    let scheduled := scheduleMachine (MachineControl.ready serial)
    let executing := startMachine scheduled.state
    executing.result = .started ∧
      executing.state.worker = .executing ∧
      executing.state.executor.running = 1 := by
  native_decide

def drainClosing : State :=
  let withOne := (tryPost (initial .serial .drain 2) .external).state
  let withTwo := (tryPost withOne .external).state
  beginShutdown (start withTwo).state

def drainSettled : State := settleShutdown drainClosing

example : drainClosing.lifecycle = .closing ∧ drainClosing.queued = 1 ∧
    drainClosing.running = 1 := by
  native_decide

example : drainSettled.Settled :=
  settleShutdown_settled drainClosing (by native_decide)

example : drainSettled.accepted = drainSettled.completed +
    drainSettled.cancelled :=
  settled_tasks_have_exactly_one_terminal_outcome drainSettled
    (settleShutdown_settled drainClosing (by native_decide))

example :
    ∃ closed,
      close drainSettled = some closed ∧
      closed.lifecycle = .closed ∧ closed.Quiescent := by
  let closed : State := { drainSettled with lifecycle := .closed }
  have closedResult : close drainSettled = some closed := by
    native_decide
  exact ⟨closed, closedResult,
    close_produces_closed_quiescent drainSettled closed closedResult⟩

def cancelClosing : State :=
  let withOne := (tryPost (initial .serial .cancelPending 2) .external).state
  let withTwo := (tryPost withOne .external).state
  beginShutdown (start withTwo).state

example : cancelClosing.queued = 0 ∧ cancelClosing.running = 1 ∧
    cancelClosing.cancelled = 1 := by
  native_decide

example :
    let settled := settleShutdown cancelClosing
    settled.completed = 1 ∧ settled.cancelled = 1 ∧ settled.Settled := by
  constructor
  · native_decide
  constructor
  · native_decide
  · exact settleShutdown_settled cancelClosing (by native_decide)

end CMetaCFlowCalculus.Tests.ExecutorProtocol
