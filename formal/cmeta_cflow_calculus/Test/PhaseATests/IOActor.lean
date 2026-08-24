import CMetaCFlowCalculus.Proofs.IOActor

open CMetaCFlowCalculus.IO
open CMetaCFlowCalculus.IO.Actor

namespace CMetaCFlowCalculus.Tests.IOActor

def commands : BoundedMpsc.State Command :=
  { capacity := 2, queue := [], terminal := .open }

def initial : Actor.State :=
  { capacity := 2
    commands := commands
    requests := []
    nextId := 1
    terminal := .running }

def executor : Executor.State RequestId :=
  { capacity := 1
    parallelism := 1
    capability := .serial
    terminal := .open
    queue := []
    running := []
    nextId := 1 }

theorem initialValid : initial.Valid := by
  simp [Actor.State.Valid, BoundedMpsc.State.Valid, initial, commands]

theorem initialOwnershipValid : initial.OwnershipValid := by
  simp [Actor.State.OwnershipValid, initial]

def submitted : SubmitResult := trySubmit initial 100
def submitProcessed : ProcessResult := processOne submitted.state
def backendStarted : ControlResult := beginBackend submitProcessed.state 1
def cancelQueued : CancelResult := tryCancel submitProcessed.state 1
def cancelledBeforeBackend : ProcessResult := processOne cancelQueued.state
def pendingCancelQueued : CancelResult := tryCancel backendStarted.state 1
def pendingCancelled : ProcessResult := processOne pendingCancelQueued.state
def completed : CompletionResult := backendComplete pendingCancelled.state 1 (.ok 4)
def dispatched : DispatchResult := tryDispatch completed.state executor

example : submitted.status = .accepted := by native_decide
example : submitted.requestId = some 1 := by native_decide
example : submitProcessed.status = .processed := by native_decide
example : (findRequest submitProcessed.state.requests 1).map Request.phase =
    some .ready := by native_decide
example : backendStarted.status = .ok := by native_decide
example : (findRequest backendStarted.state.requests 1).map Request.phase =
    some (.backendPending false) := by native_decide
example : (findRequest cancelledBeforeBackend.state.requests 1).map
    Request.phase = some (.completed .cancelled) := by native_decide
example : (findRequest pendingCancelled.state.requests 1).map Request.phase =
    some (.backendPending true) := by native_decide
example : completed.status = .completed := by native_decide
example : (backendComplete completed.state 1 .eof).status = .notPending := by
  native_decide
example : dispatched.status = .accepted := by native_decide
example : (acknowledge dispatched.actor 1).status = .released := by
  native_decide
example : (acknowledge dispatched.actor 1).state.requests = [] := by
  native_decide
example : (trySubmit (close initial).state 100).status = .closed := by
  native_decide

def requestFull : Actor.State :=
  { initial with capacity := 1, requests := submitted.state.requests }

example : (trySubmit requestFull 101).status = .full := by native_decide

def commandFull : Actor.State :=
  { initial with commands :=
      { commands with queue := [.cancel 90, .cancel 91] } }

example : (trySubmit commandFull 100).status = .full := by native_decide
example : (trySubmit commandFull 100).state = commandFull := by native_decide

def fullExecutor : Executor.State RequestId :=
  { executor with queue := [{ id := 1, payload := 99 }], nextId := 2 }

example : (tryDispatch completed.state fullExecutor).status = .full := by
  native_decide
example : (tryDispatch completed.state fullExecutor).actor = completed.state := by
  native_decide

example : submitted.state.Valid :=
  trySubmit_preserves_valid initial 100 initialValid
example : submitted.state.OwnershipValid :=
  trySubmit_preserves_ownership initial 100 initialOwnershipValid
theorem submitProcessedValid : submitProcessed.state.Valid :=
  processOne_preserves_valid submitted.state
    (trySubmit_preserves_valid initial 100 initialValid)
theorem submitProcessedOwnershipValid : submitProcessed.state.OwnershipValid :=
  processOne_preserves_ownership submitted.state
    (trySubmit_preserves_ownership initial 100 initialOwnershipValid)
theorem backendStartedValid : backendStarted.state.Valid :=
  beginBackend_preserves_valid submitProcessed.state 1
    submitProcessedValid
theorem pendingCancelQueuedValid : pendingCancelQueued.state.Valid :=
  tryCancel_preserves_valid backendStarted.state 1 backendStartedValid
theorem pendingCancelledValid : pendingCancelled.state.Valid :=
  processOne_preserves_valid pendingCancelQueued.state pendingCancelQueuedValid
theorem completedValid : completed.state.Valid :=
  backendComplete_preserves_valid pendingCancelled.state 1 (.ok 4)
    pendingCancelledValid
theorem backendStartedOwnershipValid : backendStarted.state.OwnershipValid :=
  beginBackend_preserves_ownership submitProcessed.state 1
    submitProcessedOwnershipValid
theorem pendingCancelQueuedOwnershipValid :
    pendingCancelQueued.state.OwnershipValid :=
  tryCancel_preserves_ownership backendStarted.state 1
    backendStartedOwnershipValid
theorem pendingCancelledOwnershipValid : pendingCancelled.state.OwnershipValid :=
  processOne_preserves_ownership pendingCancelQueued.state
    pendingCancelQueuedOwnershipValid
theorem completedOwnershipValid : completed.state.OwnershipValid :=
  backendComplete_preserves_ownership pendingCancelled.state 1 (.ok 4)
    pendingCancelledOwnershipValid
theorem dispatchedValid : dispatched.actor.Valid :=
  tryDispatch_preserves_actor_valid completed.state executor completedValid
theorem dispatchedOwnershipValid : dispatched.actor.OwnershipValid :=
  tryDispatch_preserves_actor_ownership completed.state executor
    completedOwnershipValid
example : (acknowledge dispatched.actor 1).state.Valid :=
  acknowledge_preserves_valid dispatched.actor 1 dispatchedValid
example : (acknowledge dispatched.actor 1).state.OwnershipValid :=
  acknowledge_preserves_ownership dispatched.actor 1 dispatchedOwnershipValid
example : (close initial).state.Valid :=
  close_preserves_valid initial initialValid
example : (close initial).state.OwnershipValid :=
  close_preserves_ownership initial initialOwnershipValid
example : (tryDispatch completed.state fullExecutor).actor = completed.state :=
  dispatch_full_preserves_actor completed.state fullExecutor (by native_decide)
example : (acknowledge dispatched.actor 1).state.requests.length + 1 =
    dispatched.actor.requests.length := by native_decide
example : (acknowledge dispatched.actor 1).state.requests.length + 1 =
    dispatched.actor.requests.length :=
  acknowledge_released_removes_one dispatched.actor
    (acknowledge dispatched.actor 1).state 1 rfl

end CMetaCFlowCalculus.Tests.IOActor
