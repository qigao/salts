import CMetaCFlowCalculus.IO.BoundedMpsc
import CMetaCFlowCalculus.IO.Executor

namespace CMetaCFlowCalculus.IO.Actor

abbrev RequestId := Nat
abbrev LeaseId := Nat

inductive Completion where
  | ok (bytes : Nat)
  | eof
  | cancelled
  | failed (error : Nat)
  deriving Repr, DecidableEq

inductive RequestPhase where
  | admitted
  | ready
  | backendPending (cancelRequested : Bool)
  | completed (result : Completion)
  | dispatchQueued (taskId : Nat) (result : Completion)
  deriving Repr, DecidableEq

structure Request where
  id : RequestId
  lease : LeaseId
  phase : RequestPhase
  deriving Repr, DecidableEq

inductive Command where
  | submit (requestId : RequestId)
  | cancel (requestId : RequestId)
  deriving Repr, DecidableEq

inductive Terminal where
  | running
  | closing
  deriving Repr, DecidableEq

structure State where
  capacity : Nat
  commands : BoundedMpsc.State Command
  requests : List Request
  nextId : Nat
  terminal : Terminal
  deriving Repr, DecidableEq

namespace State

def Valid (state : State) : Prop :=
  0 < state.capacity ∧
    state.requests.length ≤ state.capacity ∧
    state.commands.Valid

def OwnershipValid (state : State) : Prop :=
  (state.requests.map Request.id).Nodup ∧
    (state.requests.map Request.lease).Nodup ∧
    (∀ requestId ∈ state.requests.map Request.id,
      requestId < state.nextId) ∧
    0 < state.nextId

end State

def findRequest : List Request → RequestId → Option Request
  | [], _ => none
  | request :: remaining, requestId =>
      if request.id = requestId then some request
      else findRequest remaining requestId

def containsLease : List Request → LeaseId → Bool
  | [], _ => false
  | request :: remaining, lease =>
      request.lease = lease || containsLease remaining lease

def modifyPhase (requests : List Request) (requestId : RequestId)
    (update : RequestPhase → RequestPhase) : List Request :=
  match requests with
  | [] => []
  | request :: remaining =>
      if request.id = requestId then
        { request with phase := update request.phase } :: remaining
      else
        request :: modifyPhase remaining requestId update

def removeRequest : List Request → RequestId → List Request
  | [], _ => []
  | request :: remaining, requestId =>
      if request.id = requestId then remaining
      else request :: removeRequest remaining requestId

def firstCompleted : List Request → Option (Request × Completion)
  | [] => none
  | request :: remaining =>
      match request.phase with
      | .completed result => some (request, result)
      | _ => firstCompleted remaining

def markReady : RequestPhase → RequestPhase
  | .admitted => .ready
  | phase => phase

def requestCancel : RequestPhase → RequestPhase
  | .admitted | .ready => .completed .cancelled
  | .backendPending _ => .backendPending true
  | phase => phase

inductive SubmitStatus where
  | accepted
  | full
  | closed
  | leaseInUse
  deriving Repr, DecidableEq

structure SubmitResult where
  status : SubmitStatus
  requestId : Option RequestId
  state : State
  deriving Repr, DecidableEq

/-- Admission atomically reserves a request/completion slot and publishes its command. -/
def trySubmit (state : State) (lease : LeaseId) : SubmitResult :=
  match state.terminal with
  | .closing => { status := .closed, requestId := none, state := state }
  | .running =>
      if containsLease state.requests lease then
        { status := .leaseInUse, requestId := none, state := state }
      else if state.requests.length < state.capacity then
        let request : Request :=
          { id := state.nextId, lease := lease, phase := .admitted }
        let published :=
          BoundedMpsc.tryPublish state.commands (.submit request.id)
        match published.1 with
        | .accepted =>
            { status := .accepted
              requestId := some request.id
              state :=
                { state with
                  commands := published.2
                  requests := state.requests ++ [request]
                  nextId := state.nextId + 1 } }
        | .full => { status := .full, requestId := none, state := state }
        | .closed => { status := .closed, requestId := none, state := state }
      else
        { status := .full, requestId := none, state := state }

inductive CancelStatus where
  | accepted
  | full
  | closed
  | notFound
  deriving Repr, DecidableEq

structure CancelResult where
  status : CancelStatus
  state : State
  deriving Repr, DecidableEq

def tryCancel (state : State) (requestId : RequestId) : CancelResult :=
  match findRequest state.requests requestId with
  | none => { status := .notFound, state := state }
  | some _ =>
      let published :=
        BoundedMpsc.tryPublish state.commands (.cancel requestId)
      match published.1 with
      | .accepted =>
          { status := .accepted, state := { state with commands := published.2 } }
      | .full => { status := .full, state := state }
      | .closed => { status := .closed, state := state }

inductive ProcessStatus where
  | processed
  | empty
  | closed
  deriving Repr, DecidableEq

structure ProcessResult where
  status : ProcessStatus
  command : Option Command
  state : State
  deriving Repr, DecidableEq

def processOne (state : State) : ProcessResult :=
  let consumed := BoundedMpsc.tryConsume state.commands
  match consumed.1 with
  | .item command =>
      let requests :=
        match command with
        | .submit requestId => modifyPhase state.requests requestId markReady
        | .cancel requestId => modifyPhase state.requests requestId requestCancel
      { status := .processed
        command := some command
        state := { state with commands := consumed.2, requests := requests } }
  | .empty => { status := .empty, command := none, state := state }
  | .closed => { status := .closed, command := none, state := state }

inductive ControlStatus where
  | ok
  | notReady
  | notFound
  | closed
  deriving Repr, DecidableEq

structure ControlResult where
  status : ControlStatus
  state : State
  deriving Repr, DecidableEq

def beginBackend (state : State) (requestId : RequestId) : ControlResult :=
  match state.terminal with
  | .closing => { status := .closed, state := state }
  | .running =>
      match findRequest state.requests requestId with
      | none => { status := .notFound, state := state }
      | some request =>
          match request.phase with
          | .ready =>
              { status := .ok
                state := { state with requests :=
                  (modifyPhase state.requests requestId
                    (fun _ => .backendPending false)) } }
          | _ => { status := .notReady, state := state }

inductive CompletionStatus where
  | completed
  | notPending
  | notFound
  deriving Repr, DecidableEq

structure CompletionResult where
  status : CompletionStatus
  state : State
  deriving Repr, DecidableEq

def backendComplete (state : State) (requestId : RequestId)
    (result : Completion) : CompletionResult :=
  match findRequest state.requests requestId with
  | none => { status := .notFound, state := state }
  | some request =>
      match request.phase with
      | .backendPending _ =>
          { status := .completed
            state := { state with requests :=
              (modifyPhase state.requests requestId
                (fun _ => .completed result)) } }
      | _ => { status := .notPending, state := state }

inductive DispatchStatus where
  | accepted
  | full
  | closed
  | noCompletion
  | executorInvalid
  deriving Repr, DecidableEq

structure DispatchResult where
  status : DispatchStatus
  requestId : Option RequestId
  actor : State
  executor : Executor.State RequestId

/-- Completion remains in its request slot unless Executor admission succeeds. -/
def tryDispatch (actor : State)
    (executor : Executor.State RequestId) : DispatchResult :=
  match firstCompleted actor.requests with
  | none =>
      { status := .noCompletion, requestId := none,
        actor := actor, executor := executor }
  | some (request, result) =>
      let posted := Executor.tryPost executor request.id
      match posted.status, posted.taskId with
      | .accepted, some taskId =>
          { status := .accepted
            requestId := some request.id
            actor := { actor with requests :=
              (modifyPhase actor.requests request.id
                (fun _ => .dispatchQueued taskId result)) }
            executor := posted.state }
      | .accepted, none =>
          { status := .executorInvalid, requestId := some request.id,
            actor := actor, executor := executor }
      | .full, _ =>
          { status := .full, requestId := some request.id,
            actor := actor, executor := executor }
      | .closed, _ =>
          { status := .closed, requestId := some request.id,
            actor := actor, executor := executor }

inductive AckStatus where
  | released
  | busy
  | notFound
  deriving Repr, DecidableEq

structure AckResult where
  status : AckStatus
  state : State
  deriving Repr, DecidableEq

def acknowledge (state : State) (requestId : RequestId) : AckResult :=
  match findRequest state.requests requestId with
  | none => { status := .notFound, state := state }
  | some request =>
      match request.phase with
      | .dispatchQueued _ _ =>
          { status := .released
            state := { state with requests := removeRequest state.requests requestId } }
      | _ => { status := .busy, state := state }

structure CloseResult where
  status : ControlStatus
  state : State
  deriving Repr, DecidableEq

def close (state : State) : CloseResult :=
  match state.terminal with
  | .closing => { status := .closed, state := state }
  | .running =>
      { status := .ok
        state :=
          { state with
            commands := (BoundedMpsc.close state.commands).2
            terminal := .closing } }

def Quiescent (state : State) : Prop :=
  state.terminal = .closing ∧ state.commands.queue = [] ∧ state.requests = []

end CMetaCFlowCalculus.IO.Actor
