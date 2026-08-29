import CMetaCFlowCalculus.IO.Communication

namespace CMetaCFlowCalculus.IO.CompletionDriver

inductive Phase where
  | submitted
  | inflight
  deriving Repr, DecidableEq

structure Operation where
  identity : Communication.Identity
  operation : Nat
  phase : Phase
  deriving Repr, DecidableEq

structure State where
  capacity : Nat
  commands : BoundedMpsc.State Communication.Identity
  events : BoundedMpsc.State Communication.Event
  pending : List Operation
  lifecycle : Communication.Lifecycle
  deriving Repr, DecidableEq

namespace State

def project (state : State) : Communication.Contract :=
  { capacity := state.capacity
    commands := state.commands
    events := state.events
    pending := state.pending.map Operation.identity
    lifecycle := state.lifecycle }

def Valid (state : State) : Prop :=
  state.project.Valid

end State

def empty (capacity mailboxCapacity : Nat) : State :=
  { capacity := capacity
    commands := (Communication.empty capacity mailboxCapacity).commands
    events := (Communication.empty capacity mailboxCapacity).events
    pending := []
    lifecycle := .open }

structure AdmissionResult where
  status : Communication.AdmissionStatus
  state : State
  deriving Repr, DecidableEq

def trySubmit (state : State) (identity : Communication.Identity)
    (operation : Nat) : AdmissionResult :=
  if operation = 0 then
    { status := .invalid, state := state }
  else if ¬identity.Valid then
    { status := .invalid, state := state }
  else
    match state.lifecycle with
    | .draining => { status := .closed, state := state }
    | .open =>
        if identity ∈ state.pending.map Operation.identity then
          { status := .duplicate, state := state }
        else if state.pending.length < state.capacity then
          let published := BoundedMpsc.tryPublish state.commands identity
          match published.1 with
          | .accepted =>
              { status := .accepted
                state :=
                  { state with
                    commands := published.2
                    pending := state.pending ++
                      [{ identity := identity
                         operation := operation
                         phase := .submitted }] } }
          | .full => { status := .full, state := state }
          | .closed => { status := .closed, state := state }
        else
          { status := .full, state := state }

def beginOperation : List Operation → Communication.Identity → List Operation
  | [], _ => []
  | current :: remaining, identity =>
      if current.identity = identity then
        { current with phase := .inflight } :: remaining
      else current :: beginOperation remaining identity

def begin (state : State) (identity : Communication.Identity) : State :=
  { state with pending := beginOperation state.pending identity }

def eraseOperation : List Operation → Communication.Identity → List Operation
  | [], _ => []
  | current :: remaining, identity =>
      if current.identity = identity then remaining
      else current :: eraseOperation remaining identity

structure TerminalTransition where
  status : Communication.TerminalStatus
  state : State
  deriving Repr, DecidableEq

def tryComplete (state : State) (event : Communication.Event) :
    TerminalTransition :=
  if ¬event.Valid then
    { status := .invalid, state := state }
  else if event.identity ∉ state.pending.map Operation.identity then
    { status := .stale, state := state }
  else
    let published := BoundedMpsc.tryPublish state.events event
    match published.1 with
    | .accepted =>
        { status := .accepted
          state :=
            { state with
              events := published.2
              pending := eraseOperation state.pending event.identity } }
    | .full => { status := .full, state := state }
    | .closed => { status := .closed, state := state }

structure ObserveResult where
  observation : BoundedMpsc.Observation Communication.Event
  state : State
  deriving Repr, DecidableEq

def tryObserve (state : State) : ObserveResult :=
  let consumed := BoundedMpsc.tryConsume state.events
  { observation := consumed.1, state := { state with events := consumed.2 } }

def close (state : State) : State :=
  { state with
    commands := (BoundedMpsc.close state.commands).2
    lifecycle := .draining }

end CMetaCFlowCalculus.IO.CompletionDriver
