import CMetaCFlowCalculus.IO.Communication

namespace CMetaCFlowCalculus.IO.ReadinessDriver

inductive Phase where
  | watched
  | signalled
  deriving Repr, DecidableEq

structure Registration where
  identity : Communication.Identity
  interests : Nat
  phase : Phase
  deriving Repr, DecidableEq

structure State where
  capacity : Nat
  commands : BoundedMpsc.State Communication.Identity
  events : BoundedMpsc.State Communication.Event
  pending : List Registration
  lifecycle : Communication.Lifecycle
  deriving Repr, DecidableEq

namespace State

def project (state : State) : Communication.Contract :=
  { capacity := state.capacity
    commands := state.commands
    events := state.events
    pending := state.pending.map Registration.identity
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

def tryWatch (state : State) (identity : Communication.Identity)
    (interests : Nat) : AdmissionResult :=
  if interests = 0 then
    { status := .invalid, state := state }
  else if ¬identity.Valid then
    { status := .invalid, state := state }
  else
    match state.lifecycle with
    | .draining => { status := .closed, state := state }
    | .open =>
        if identity ∈ state.pending.map Registration.identity then
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
                         interests := interests
                         phase := .watched }] } }
          | .full => { status := .full, state := state }
          | .closed => { status := .closed, state := state }
        else
          { status := .full, state := state }

def signalRegistration : List Registration → Communication.Identity →
    List Registration
  | [], _ => []
  | current :: remaining, identity =>
      if current.identity = identity then
        { current with phase := .signalled } :: remaining
      else current :: signalRegistration remaining identity

def signal (state : State) (identity : Communication.Identity) : State :=
  { state with pending := signalRegistration state.pending identity }

def eraseRegistration : List Registration → Communication.Identity →
    List Registration
  | [], _ => []
  | current :: remaining, identity =>
      if current.identity = identity then remaining
      else current :: eraseRegistration remaining identity

structure TerminalTransition where
  status : Communication.TerminalStatus
  state : State
  deriving Repr, DecidableEq

def tryTerminate (state : State) (event : Communication.Event) :
    TerminalTransition :=
  if ¬event.Valid then
    { status := .invalid, state := state }
  else if event.identity ∉ state.pending.map Registration.identity then
    { status := .stale, state := state }
  else
    let published := BoundedMpsc.tryPublish state.events event
    match published.1 with
    | .accepted =>
        { status := .accepted
          state :=
            { state with
              events := published.2
              pending := eraseRegistration state.pending event.identity } }
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

end CMetaCFlowCalculus.IO.ReadinessDriver
