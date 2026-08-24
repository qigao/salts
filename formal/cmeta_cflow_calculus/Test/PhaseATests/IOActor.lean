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

theorem initialLifecycleValid : initial.LifecycleValid := by
  simp [Actor.State.LifecycleValid, initial, commands]

def submitted : SubmitResult := trySubmit initial 100
def submitProcessed : ProcessResult := processOne submitted.state
def backendStarted : ControlResult := beginBackend submitProcessed.state 1
def cancelQueued : CancelResult := tryCancel submitProcessed.state 1
def cancelledBeforeBackend : ProcessResult := processOne cancelQueued.state
def pendingCancelQueued : CancelResult := tryCancel backendStarted.state 1
def pendingCancelled : ProcessResult := processOne pendingCancelQueued.state
def completed : CompletionResult := backendComplete pendingCancelled.state 1 (.ok 4)
def dispatched : DispatchResult := tryDispatch completed.state executor
def deliveryStarted : DeliverySystemResult :=
  startDelivery dispatched.actor dispatched.executor
def deliveryFinished : DeliverySystemResult :=
  finishDelivery deliveryStarted.actor deliveryStarted.executor 1
def mismatchedDispatch : Actor.State :=
  { dispatched.actor with requests :=
      (modifyPhase dispatched.actor.requests 1
        (fun _ => .dispatchQueued 99 (.ok 4))) }

example : submitted.status = .accepted := by native_decide
example : submitted.requestId = some 1 := by native_decide
example : HasCompletionCredit submitted.state 1 :=
  accepted_submit_has_completion_credit initial submitted.state 100 1
    initialOwnershipValid rfl
example : ∃ request ∈ submitted.state.requests, request.id = 1 ∧
    ∀ other ∈ submitted.state.requests, other.id = 1 → other = request :=
  completion_credit_is_exactly_one submitted.state 1 (by
    exact accepted_submit_has_completion_credit initial submitted.state 100 1
      initialOwnershipValid rfl)
example : submitted.state.LifecycleValid :=
  trySubmit_preserves_lifecycle initial 100 initialLifecycleValid
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
example : (acknowledge dispatched.actor 1).status = .busy := by
  native_decide
example : deliveryStarted.status = .observed := by native_decide
example : (acknowledge deliveryStarted.actor 1).status = .busy := by native_decide
example : deliveryFinished.status = .observed := by native_decide
example : (acknowledge deliveryFinished.actor 1).status = .released := by
  native_decide
example : (acknowledge deliveryFinished.actor 1).state.requests = [] := by
  native_decide
example : (startDelivery mismatchedDispatch dispatched.executor).actor =
    mismatchedDispatch := by native_decide
example : (startDelivery mismatchedDispatch dispatched.executor).executor.queue =
    dispatched.executor.queue := by native_decide
example : (startDelivery mismatchedDispatch dispatched.executor).executor.running =
    dispatched.executor.running := by native_decide
example : (startDelivery mismatchedDispatch dispatched.executor).actor =
      mismatchedDispatch ∧
    (startDelivery mismatchedDispatch dispatched.executor).executor =
      dispatched.executor :=
  startDelivery_rejected_unchanged mismatchedDispatch dispatched.executor
    .phaseMismatch (by decide) (by native_decide)
example : (finishDelivery deliveryStarted.actor executor 1).actor =
      deliveryStarted.actor ∧
    (finishDelivery deliveryStarted.actor executor 1).executor = executor :=
  finishDelivery_rejected_unchanged deliveryStarted.actor executor 1
    .executorRejected (by decide) (by native_decide)
example : ¬ SystemQuiescent (close dispatched.actor).state dispatched.executor := by
  intro quiescent
  have queueEmpty := quiescent.2.1
  have queueRetained : dispatched.executor.queue ≠ [] := by native_decide
  exact queueRetained queueEmpty
example : (trySubmit (close initial).state 100).status = .closed := by
  native_decide

def closeAdmitted : CloseResult := close submitted.state
def closePending : CloseResult := close backendStarted.state

example : (findRequest closeAdmitted.state.requests 1).map Request.phase =
    some (.completed .cancelled) := by native_decide
example : (findRequest closePending.state.requests 1).map Request.phase =
    some (.backendPending true) := by native_decide

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
theorem deliveryStartedValid : deliveryStarted.actor.Valid :=
  startDelivery_preserves_actor_valid dispatched.actor dispatched.executor
    dispatchedValid
theorem deliveryStartedOwnershipValid : deliveryStarted.actor.OwnershipValid :=
  startDelivery_preserves_actor_ownership dispatched.actor dispatched.executor
    dispatchedOwnershipValid
theorem deliveryFinishedValid : deliveryFinished.actor.Valid :=
  finishDelivery_preserves_actor_valid deliveryStarted.actor
    deliveryStarted.executor 1 deliveryStartedValid
theorem deliveryFinishedOwnershipValid : deliveryFinished.actor.OwnershipValid :=
  finishDelivery_preserves_actor_ownership deliveryStarted.actor
    deliveryStarted.executor 1 deliveryStartedOwnershipValid
example : (acknowledge dispatched.actor 1).state = dispatched.actor :=
  acknowledge_dispatchQueued_retains dispatched.actor 1 1 (.ok 4) (by native_decide)
example : (acknowledge deliveryStarted.actor 1).state = deliveryStarted.actor :=
  acknowledge_dispatchRunning_retains deliveryStarted.actor 1 1 (.ok 4)
    (by native_decide)
example : (acknowledge dispatched.actor 1).state.Valid :=
  acknowledge_preserves_valid dispatched.actor 1 dispatchedValid
example : (acknowledge dispatched.actor 1).state.OwnershipValid :=
  acknowledge_preserves_ownership dispatched.actor 1 dispatchedOwnershipValid
example : (close initial).state.Valid :=
  close_preserves_valid initial initialValid
example : (close initial).state.OwnershipValid :=
  close_preserves_ownership initial initialOwnershipValid
example : (close initial).state.LifecycleValid :=
  close_preserves_lifecycle initial initialLifecycleValid
example : (tryDispatch completed.state fullExecutor).actor = completed.state :=
  dispatch_full_preserves_actor completed.state fullExecutor (by native_decide)
example : (tryDispatch completed.state fullExecutor).executor = fullExecutor :=
  dispatch_rejected_preserves_executor completed.state fullExecutor .full
    (by decide) (by native_decide)
example : (acknowledge deliveryFinished.actor 1).state.requests.length + 1 =
    deliveryFinished.actor.requests.length := by native_decide
example : (acknowledge deliveryFinished.actor 1).state.requests.length + 1 =
    deliveryFinished.actor.requests.length :=
  acknowledge_released_removes_one deliveryFinished.actor
    (acknowledge deliveryFinished.actor 1).state 1 rfl

end CMetaCFlowCalculus.Tests.IOActor
