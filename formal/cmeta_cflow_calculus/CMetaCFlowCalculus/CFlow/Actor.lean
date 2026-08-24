import CMetaCFlowCalculus.CFlow.Mailbox

namespace CMetaCFlowCalculus.CFlow.Actor

inductive Lifecycle where
  | start
  | running
  | stopping
  | stopped
  | failed
  deriving Repr, DecidableEq

inductive LifecycleStatus where
  | ok
  | alreadyStarted
  | stopping
  | stopped
  | failed
  deriving Repr, DecidableEq

inductive SendStatus where
  | accepted
  | invalidArgument
  | typeMismatch
  | full
  | notStarted
  | stopping
  | stopped
  | failed
  | stale
  deriving Repr, DecidableEq

def Lifecycle.expectedMailboxTerminal : Lifecycle → Mailbox.Terminal
  | .start | .running => .open
  | .stopping | .stopped | .failed => .cancelled

structure State where
  lifecycle : Lifecycle
  mailbox : Mailbox.State
  /-- `false` abstracts owner destruction while retained producer refs survive. -/
  live : Bool

namespace State

/-- Machine/Mailbox data remain authoritative; Actor validity adds only the
    lifecycle admission boundary and its required Mailbox terminal mode. -/
def Valid (state : State) : Prop :=
  state.mailbox.Valid ∧
    state.mailbox.terminal = state.lifecycle.expectedMailboxTerminal

end State

structure SendResult where
  status : SendStatus
  state : State

def initState (mailbox : Mailbox.State) : State :=
  { lifecycle := .start, mailbox := mailbox, live := true }

/-- Internal lifecycle update shared by the proof layer. -/
def cancelAs (state : State) (lifecycle : Lifecycle) : State :=
  { state with
    lifecycle := lifecycle
    mailbox := (Mailbox.cancel state.mailbox).2 }

/-- `succeeds = false` represents attachment/open/demand failure. -/
def start (state : State) (succeeds : Bool) : LifecycleStatus × State :=
  match state.lifecycle with
  | .start =>
      if succeeds then
        (.ok, { state with lifecycle := .running })
      else
        (.failed, cancelAs state .failed)
  | .running => (.alreadyStarted, state)
  | .stopping => (.stopping, state)
  | .stopped => (.stopped, state)
  | .failed => (.failed, state)

/-- Admission closes before the existing Machine/Mailbox cancellation. -/
def requestStop (state : State) : LifecycleStatus × State :=
  match state.lifecycle with
  | .start => (.ok, cancelAs state .stopped)
  | .running => (.ok, cancelAs state .stopping)
  | .stopping => (.stopping, state)
  | .stopped => (.stopped, state)
  | .failed => (.failed, state)

/-- Normal Run completion settles STOPPING; a done signal while RUNNING is the
    existing runtime protocol violation and therefore fails the Actor. -/
def settle (state : State) : State :=
  match state.lifecycle with
  | .stopping => { state with lifecycle := .stopped }
  | .running => cancelAs state .failed
  | .start | .stopped | .failed => state

/-- First runtime failure cancels queued Events. Terminal states are absorbing. -/
def fail (state : State) : State :=
  match state.lifecycle with
  | .stopped | .failed => state
  | .start | .running | .stopping => cancelAs state .failed

/-- Synchronous owner destruction leaves retained refs stale and terminal. -/
def destroy (state : State) : State :=
  { settle (requestStop state).2 with live := false }

def mapMailboxStatus : Mailbox.Status → SendStatus
  | .ok => .accepted
  | .invalidArgument => .invalidArgument
  | .typeMismatch => .typeMismatch
  | .full => .full
  | .empty => .invalidArgument
  | .closed | .cancelled => .failed

/-- Actor-gated classification followed by exactly one existing Mailbox send
    only in RUNNING. Stale classification precedes Event validation. -/
def send (state : State) (event : Mailbox.TypedEvent) : SendResult :=
  if !state.live then
    { status := .stale, state := state }
  else
    match state.lifecycle with
    | .start => { status := .notStarted, state := state }
    | .stopping => { status := .stopping, state := state }
    | .stopped => { status := .stopped, state := state }
    | .failed => { status := .failed, state := state }
    | .running =>
        let sent := Mailbox.send state.mailbox event
        { status := mapMailboxStatus sent.1
          state := { state with mailbox := sent.2 } }

def sendMany : State → List Mailbox.TypedEvent → State
  | state, [] => state
  | state, event :: remaining => sendMany (send state event).state remaining

end CMetaCFlowCalculus.CFlow.Actor
