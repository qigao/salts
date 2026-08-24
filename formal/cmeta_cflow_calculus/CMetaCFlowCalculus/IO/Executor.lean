namespace CMetaCFlowCalculus.IO.Executor

inductive Capability where
  | manual
  | serial
  | concurrent
  deriving Repr, DecidableEq

inductive Terminal where
  | «open»
  | draining
  | closed
  deriving Repr, DecidableEq

structure Task (α : Type) where
  id : Nat
  payload : α
  deriving Repr, DecidableEq

structure State (α : Type) where
  capacity : Nat
  parallelism : Nat
  capability : Capability
  terminal : Terminal
  queue : List (Task α)
  /-- IDs of tasks whose callbacks have started but not finished. -/
  running : List Nat
  nextId : Nat

namespace State

def knownIds (state : State α) : List Nat :=
  state.running ++ state.queue.map Task.id

def Valid (state : State α) : Prop :=
  0 < state.capacity ∧
    0 < state.parallelism ∧
    state.queue.length ≤ state.capacity ∧
    state.running.length ≤ state.parallelism ∧
    (state.capability = .concurrent ∨ state.parallelism = 1)

/-- Identifier freshness is independent of resource-capacity validity. -/
def IdentifiersValid (state : State α) : Prop :=
  state.knownIds.Nodup ∧
    (∀ taskId ∈ state.knownIds, taskId < state.nextId) ∧
    0 < state.nextId

end State

inductive PostStatus where
  | accepted
  | full
  | closed
  deriving Repr, DecidableEq

structure PostResult (α : Type) where
  status : PostStatus
  taskId : Option Nat
  state : State α

inductive StartStatus where
  | started
  | empty
  | saturated
  | closed
  deriving Repr, DecidableEq

structure StartResult (α : Type) where
  status : StartStatus
  task : Option (Task α)
  state : State α

inductive FinishStatus where
  | finished
  | notFound
  deriving Repr, DecidableEq

structure FinishResult (α : Type) where
  status : FinishStatus
  state : State α

inductive ControlStatus where
  | ok
  | busy
  | closed
  deriving Repr, DecidableEq

structure ControlResult (α : Type) where
  status : ControlStatus
  state : State α

/-- Non-blocking bounded task admission. -/
def tryPost (state : State α) (payload : α) : PostResult α :=
  match state.terminal with
  | .draining | .closed =>
      { status := .closed, taskId := none, state := state }
  | .open =>
      if state.queue.length < state.capacity then
        let task := { id := state.nextId, payload := payload }
        { status := .accepted
          taskId := some task.id
          state :=
            { state with
              queue := state.queue ++ [task]
              nextId := state.nextId + 1 } }
      else
        { status := .full, taskId := none, state := state }

/-- Start the FIFO head when one execution permit is available. -/
def start (state : State α) : StartResult α :=
  match state.terminal with
  | .closed => { status := .closed, task := none, state := state }
  | .open | .draining =>
      if state.running.length < state.parallelism then
        match state.queue with
        | [] => { status := .empty, task := none, state := state }
        | task :: remaining =>
            { status := .started
              task := some task
              state :=
                { state with
                  queue := remaining
                  running := state.running ++ [task.id] } }
      else
        { status := .saturated, task := none, state := state }

/-- A task ID can finish only while it owns one running permit. -/
def finish (state : State α) (taskId : Nat) : FinishResult α :=
  if taskId ∈ state.running then
    { status := .finished,
      state := { state with running := state.running.erase taskId } }
  else
    { status := .notFound, state := state }

/-- Shutdown closes admission but drains tasks already accepted. -/
def shutdown (state : State α) : ControlResult α :=
  match state.terminal with
  | .open => { status := .ok, state := { state with terminal := .draining } }
  | .draining | .closed => { status := .closed, state := state }

/-- Draining becomes closed only after queued and running tasks are empty. -/
def settle (state : State α) : ControlResult α :=
  match state.terminal with
  | .draining =>
      if state.queue.isEmpty && state.running.isEmpty then
        { status := .ok, state := { state with terminal := .closed } }
      else
        { status := .busy, state := state }
  | .closed => { status := .closed, state := state }
  | .open => { status := .busy, state := state }

end CMetaCFlowCalculus.IO.Executor
