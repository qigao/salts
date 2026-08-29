import CMetaCFlowCalculus.IO.BoundedMpsc

namespace CMetaCFlowCalculus.IO.Communication

structure EndpointIdentity where
  endpoint : Nat
  generation : Nat
  deriving Repr, DecidableEq

namespace EndpointIdentity

def Valid (identity : EndpointIdentity) : Prop :=
  0 < identity.endpoint ∧ 0 < identity.generation

instance (identity : EndpointIdentity) : Decidable identity.Valid := by
  unfold Valid
  infer_instance

end EndpointIdentity

inductive ControlCommandKind where
  | close
  deriving Repr, DecidableEq

structure ControlCommand where
  kind : ControlCommandKind
  endpoint : EndpointIdentity
  deriving Repr, DecidableEq

namespace ControlCommand

def Valid (command : ControlCommand) : Prop :=
  command.endpoint.Valid

instance (command : ControlCommand) : Decidable command.Valid := by
  unfold Valid
  infer_instance

end ControlCommand

structure Identity where
  endpoint : Nat
  request : Nat
  generation : Nat
  deriving Repr, DecidableEq

namespace Identity

def Valid (identity : Identity) : Prop :=
  0 < identity.endpoint ∧ 0 < identity.request ∧ 0 < identity.generation

instance (identity : Identity) : Decidable identity.Valid := by
  unfold Valid
  infer_instance

end Identity

inductive TerminalResult where
  | completed (bytes : Nat)
  | eof
  | cancelled
  | failed (error : Nat)
  deriving Repr, DecidableEq

structure Event where
  identity : Identity
  result : TerminalResult
  deriving Repr, DecidableEq

namespace Event

def Valid (event : Event) : Prop :=
  event.identity.Valid ∧
    match event.result with
    | .failed error => 0 < error
    | _ => True

instance (event : Event) : Decidable event.Valid := by
  unfold Valid
  cases event.result <;> infer_instance

end Event

inductive Lifecycle where
  | «open»
  | draining
  deriving Repr, DecidableEq

structure Contract where
  capacity : Nat
  commands : BoundedMpsc.State Identity
  events : BoundedMpsc.State Event
  pending : List Identity
  lifecycle : Lifecycle
  deriving Repr, DecidableEq

namespace Contract

def Valid (state : Contract) : Prop :=
  0 < state.capacity ∧
    state.pending.length ≤ state.capacity ∧
    state.pending.Nodup ∧
    state.commands.Valid ∧
    state.events.Valid ∧
    match state.lifecycle with
    | .open => state.commands.terminal = .open
    | .draining => state.commands.terminal = .draining

end Contract

inductive AdmissionStatus where
  | accepted
  | full
  | closed
  | invalid
  | duplicate
  deriving Repr, DecidableEq

structure AdmissionResult where
  status : AdmissionStatus
  state : Contract
  deriving Repr, DecidableEq

def empty (capacity mailboxCapacity : Nat) : Contract :=
  { capacity := capacity
    commands :=
      { capacity := mailboxCapacity, queue := [], terminal := .open }
    events :=
      { capacity := mailboxCapacity, queue := [], terminal := .open }
    pending := []
    lifecycle := .open }

/-- Admission owns one bounded pending credit and one published command. -/
def tryAdmit (state : Contract) (identity : Identity) : AdmissionResult :=
  if ¬identity.Valid then
    { status := .invalid, state := state }
  else
    match state.lifecycle with
    | .draining => { status := .closed, state := state }
    | .open =>
        if identity ∈ state.pending then
          { status := .duplicate, state := state }
        else if state.pending.length < state.capacity then
          let published := BoundedMpsc.tryPublish state.commands identity
          match published.1 with
          | .accepted =>
              { status := .accepted
                state :=
                  { state with
                    commands := published.2
                    pending := state.pending ++ [identity] } }
          | .full => { status := .full, state := state }
          | .closed => { status := .closed, state := state }
        else
          { status := .full, state := state }

inductive TerminalStatus where
  | accepted
  | full
  | closed
  | invalid
  | stale
  deriving Repr, DecidableEq

structure TerminalTransition where
  status : TerminalStatus
  state : Contract
  deriving Repr, DecidableEq

def eraseIdentity : List Identity → Identity → List Identity
  | [], _ => []
  | current :: remaining, identity =>
      if current = identity then remaining
      else current :: eraseIdentity remaining identity

/-- A terminal event releases its pending credit only after publication. -/
def tryTerminate (state : Contract) (event : Event) : TerminalTransition :=
  if ¬event.Valid then
    { status := .invalid, state := state }
  else if event.identity ∉ state.pending then
    { status := .stale, state := state }
  else
    let published := BoundedMpsc.tryPublish state.events event
    match published.1 with
    | .accepted =>
        { status := .accepted
          state :=
            { state with
              events := published.2
              pending := eraseIdentity state.pending event.identity } }
    | .full => { status := .full, state := state }
    | .closed => { status := .closed, state := state }

structure ObserveResult where
  observation : BoundedMpsc.Observation Event
  state : Contract
  deriving Repr, DecidableEq

def tryObserve (state : Contract) : ObserveResult :=
  let consumed := BoundedMpsc.tryConsume state.events
  { observation := consumed.1, state := { state with events := consumed.2 } }

/-- Close stops command admission but retains accepted work and event capacity. -/
def close (state : Contract) : Contract :=
  { state with
    commands := (BoundedMpsc.close state.commands).2
    lifecycle := .draining }

end CMetaCFlowCalculus.IO.Communication
