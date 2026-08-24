import CMetaCFlowCalculus.CFlow.MachineRuntime

namespace CMetaCFlowCalculus.CFlow.ExecutorProtocol

open CMetaCFlowCalculus.CFlow.MachineRuntime

inductive ExecutorKind where
  | manual
  | serial
  | worker
  deriving Repr, DecidableEq

inductive ShutdownPolicy where
  | drain
  | cancelPending
  deriving Repr, DecidableEq

inductive Lifecycle where
  | open
  | closing
  | closed
  deriving Repr, DecidableEq

inductive CallerContext where
  | external
  | callback (executorId : Nat)
  deriving Repr, DecidableEq

inductive TaskPhase where
  | queued
  | running
  | completed
  | cancelled
  deriving Repr, DecidableEq, BEq

def phaseCount (wanted : TaskPhase) : List TaskPhase → Nat
  | [] => 0
  | phase :: remaining =>
      (if phase = wanted then 1 else 0) + phaseCount wanted remaining

inductive AdmissionResult where
  | accepted
  | full
  | closed
  | wouldBlock
  deriving Repr, DecidableEq

inductive StartResult where
  | started
  | empty
  | serialBusy
  deriving Repr, DecidableEq

inductive FinishResult where
  | completed
  | empty
  deriving Repr, DecidableEq

inductive WaitResult where
  | idle
  | pending
  | wouldBlock
  deriving Repr, DecidableEq

structure State where
  id : Nat
  kind : ExecutorKind
  policy : ShutdownPolicy
  capacity : Nat
  lifecycle : Lifecycle
  tasks : List TaskPhase
  rejectedFull : Nat
  rejectedClosed : Nat
  rejectedWouldBlock : Nat
  deriving Repr, DecidableEq

namespace State

def accepted (state : State) : Nat := state.tasks.length
def queued (state : State) : Nat := phaseCount .queued state.tasks
def running (state : State) : Nat := phaseCount .running state.tasks
def completed (state : State) : Nat := phaseCount .completed state.tasks
def cancelled (state : State) : Nat := phaseCount .cancelled state.tasks

def Bounded (state : State) : Prop := state.queued ≤ state.capacity

def Conserved (state : State) : Prop :=
  state.accepted = state.queued + state.running +
    state.completed + state.cancelled

def SerialSafe (state : State) : Prop :=
  match state.kind with
  | .manual | .serial => state.running ≤ 1
  | .worker => True

def Quiescent (state : State) : Prop :=
  state.queued = 0 ∧ state.running = 0

def ClosedQuiescent (state : State) : Prop :=
  state.lifecycle = .closed → state.Quiescent

def Safe (state : State) : Prop :=
  state.Bounded ∧ state.Conserved ∧ state.SerialSafe

def Settled (state : State) : Prop :=
  ∀ phase ∈ state.tasks, phase = .completed ∨ phase = .cancelled

end State

def initial (kind : ExecutorKind) (policy : ShutdownPolicy)
    (capacity : Nat) : State :=
  { id := 1
    kind := kind
    policy := policy
    capacity := capacity
    lifecycle := .open
    tasks := []
    rejectedFull := 0
    rejectedClosed := 0
    rejectedWouldBlock := 0 }

structure AdmissionOutcome where
  result : AdmissionResult
  state : State
  deriving Repr, DecidableEq

structure StartOutcome where
  result : StartResult
  state : State
  deriving Repr, DecidableEq

structure FinishOutcome where
  result : FinishResult
  state : State
  deriving Repr, DecidableEq

structure WaitOutcome where
  result : WaitResult
  state : State
  deriving Repr, DecidableEq

def sameCallback (state : State) : CallerContext → Bool
  | .external => false
  | .callback executorId => executorId == state.id

def tryPost (state : State) (_caller : CallerContext) : AdmissionOutcome :=
  match state.lifecycle with
  | .closing | .closed =>
      { result := .closed
        state := { state with rejectedClosed := state.rejectedClosed + 1 } }
  | .open =>
      if state.queued < state.capacity then
        { result := .accepted
          state := { state with tasks := state.tasks ++ [.queued] } }
      else
        { result := .full
          state := { state with rejectedFull := state.rejectedFull + 1 } }

/-- The external branch abstracts the eventual blocking implementation by its
    immediate safety boundary. Temporal progress requires a separate driver or
    fairness premise and is intentionally not claimed here. -/
def blockingPost (state : State) (caller : CallerContext) : AdmissionOutcome :=
  if state.lifecycle = .open ∧ state.capacity ≤ state.queued ∧
      sameCallback state caller then
    { result := .wouldBlock
      state := { state with
        rejectedWouldBlock := state.rejectedWouldBlock + 1 } }
  else
    tryPost state caller

def replaceFirst (source target : TaskPhase) : List TaskPhase → List TaskPhase
  | [] => []
  | phase :: remaining =>
      if phase = source then target :: remaining
      else phase :: replaceFirst source target remaining

def start (state : State) : StartOutcome :=
  if state.queued = 0 then
    { result := .empty, state := state }
  else
    match state.kind with
    | .manual | .serial =>
        if state.running = 0 then
          { result := .started
            state := { state with
              tasks := replaceFirst .queued .running state.tasks } }
        else
          { result := .serialBusy, state := state }
    | .worker =>
        { result := .started
          state := { state with
            tasks := replaceFirst .queued .running state.tasks } }

def finish (state : State) : FinishOutcome :=
  if state.running = 0 then
    { result := .empty, state := state }
  else
    { result := .completed
      state := { state with
        tasks := replaceFirst .running .completed state.tasks } }

def cancelQueued : TaskPhase → TaskPhase
  | .queued => .cancelled
  | phase => phase

def beginShutdown (state : State) : State :=
  match state.lifecycle with
  | .closing | .closed => state
  | .open =>
      match state.policy with
      | .drain => { state with lifecycle := .closing }
      | .cancelPending =>
          { state with
            lifecycle := .closing
            tasks := state.tasks.map cancelQueued }

/-- Summary settlement under the explicit premise that every running task
    terminates and drain-mode queued work is driven. It is not an OS-liveness
    transition. -/
def settleShutdown (state : State) : State :=
  match state.lifecycle, state.policy with
  | .closing, .drain =>
      { state with tasks := state.tasks.map fun phase =>
          match phase with
          | .queued | .running => .completed
          | terminal => terminal }
  | .closing, .cancelPending =>
      { state with tasks := state.tasks.map fun phase =>
          match phase with
          | .queued => .cancelled
          | .running => .completed
          | terminal => terminal }
  | _, _ => state

def close (state : State) : Option State :=
  if state.lifecycle == .closing && state.queued == 0 && state.running == 0 then
    some { state with lifecycle := .closed }
  else
    none

def waitIdle (state : State) (caller : CallerContext) : WaitOutcome :=
  if sameCallback state caller then
    { result := .wouldBlock
      state := { state with
        rejectedWouldBlock := state.rejectedWouldBlock + 1 } }
  else if state.queued == 0 && state.running == 0 then
    { result := .idle, state := state }
  else
    { result := .pending, state := state }

structure MachineControl where
  worker : WorkerPhase
  executor : State
  deriving Repr, DecidableEq

namespace MachineControl

def ready (executor : State) : MachineControl :=
  { worker := .idle, executor := executor }

end MachineControl

structure MachineAdmissionOutcome where
  result : AdmissionResult
  state : MachineControl
  deriving Repr, DecidableEq

structure MachineStartOutcome where
  result : StartResult
  state : MachineControl
  deriving Repr, DecidableEq

def scheduleMachine (control : MachineControl) : MachineAdmissionOutcome :=
  match control.worker with
  | .idle =>
      let admitted := tryPost control.executor .external
      { result := admitted.result
        state :=
          { worker := if admitted.result = .accepted then .scheduled else .idle
            executor := admitted.state } }
  | .scheduled | .executing | .committing =>
      { result := .wouldBlock, state := control }

def startMachine (control : MachineControl) : MachineStartOutcome :=
  match control.worker with
  | .scheduled =>
      let started := start control.executor
      { result := started.result
        state :=
          { worker := if started.result = .started then .executing else .scheduled
            executor := started.state } }
  | .idle | .executing | .committing =>
      { result := .empty, state := control }

end CMetaCFlowCalculus.CFlow.ExecutorProtocol
