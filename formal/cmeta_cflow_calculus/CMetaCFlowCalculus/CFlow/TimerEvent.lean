import CMetaCFlowCalculus.CFlow.Mailbox

namespace CMetaCFlowCalculus.CFlow.TimerEvent

open CMetaCFlowCalculus.CFlow.Mailbox

inductive Terminal where
  | «open»
  | closed
  deriving Repr, DecidableEq

inductive Status where
  | ok
  | invalidArgument
  | full
  | closed
  | notFound
  | fireWon
  | notReady
  | busy
  | delivered
  | mailboxRejected
  deriving Repr, DecidableEq

structure Timer where
  id : Nat
  deadline : Nat
  order : Nat
  event : TypedEvent

structure State where
  capacity : Nat
  /-- All non-terminal timer slots, including the uniquely claimed slot. -/
  active : List Timer
  claimed : Option Timer
  nextId : Nat
  nextOrder : Nat
  terminal : Terminal
  mailbox : Mailbox.State

namespace State

def TimerBounded (state : State) (timer : Timer) : Prop :=
  timer.id ≠ 0 ∧ timer.id < state.nextId ∧ timer.order < state.nextOrder

def Valid (state : State) : Prop :=
  0 < state.capacity ∧
    state.active.length ≤ state.capacity ∧
    (state.active.map Timer.id).Nodup ∧
    (state.active.map Timer.order).Nodup ∧
    0 < state.nextId ∧
    (∀ timer ∈ state.active, state.TimerBounded timer) ∧
    (∀ timer, state.claimed = some timer → timer ∈ state.active)

end State

structure ScheduleResult where
  status : Status
  timerId : Option Nat
  state : State

structure ControlResult where
  status : Status
  state : State

structure ClaimResult where
  status : Status
  timer : Option Timer
  state : State

structure FireResult where
  status : Status
  timer : Option Timer
  mailboxStatus : Option Mailbox.Status
  state : State

/-- Deadline first, then unique successful-schedule order. -/
def earlier (left right : Timer) : Bool :=
  left.deadline < right.deadline ||
    (left.deadline == right.deadline && left.order < right.order)

/-- Lexicographic deadline/order comparison used by the selection proof. -/
def KeyLE (left right : Timer) : Prop :=
  left.deadline < right.deadline ∨
    (left.deadline = right.deadline ∧ left.order ≤ right.order)

def earliest : List Timer → Option Timer
  | [] => none
  | timer :: remaining =>
      match earliest remaining with
      | none => some timer
      | some candidate =>
          if earlier timer candidate then some timer else some candidate

def removeId : List Timer → Nat → List Timer
  | [], _ => []
  | timer :: remaining, id =>
      if timer.id = id then remaining else timer :: removeId remaining id

def containsId : List Timer → Nat → Bool
  | [], _ => false
  | timer :: remaining, id => timer.id = id || containsId remaining id

/-- Scheduling copies the semantic Event into a bounded pending slot. -/
def schedule (state : State) (deadline : Nat)
    (event : TypedEvent) : ScheduleResult :=
  match state.terminal with
  | .closed => { status := .closed, timerId := none, state := state }
  | .open =>
      if state.active.length < state.capacity then
        let timer : Timer :=
          { id := state.nextId
            deadline := deadline
            order := state.nextOrder
            event := event }
        { status := .ok
          timerId := some timer.id
          state :=
            { state with
              active := state.active ++ [timer]
              nextId := state.nextId + 1
              nextOrder := state.nextOrder + 1 } }
      else
        { status := .full, timerId := none, state := state }

/-- Claim is the fire linearization point and removes exactly one ready timer. -/
def claim (state : State) (now : Nat) : ClaimResult :=
  match state.terminal with
  | .closed => { status := .closed, timer := none, state := state }
  | .open =>
      match state.claimed with
      | some timer => { status := .busy, timer := some timer, state := state }
      | none =>
          match earliest state.active with
          | none => { status := .notReady, timer := none, state := state }
          | some timer =>
              if timer.deadline ≤ now then
                { status := .ok
                  timer := some timer
                  state := { state with claimed := some timer } }
              else
                { status := .notReady, timer := none, state := state }

/-- Cancel can win only before claim. -/
def cancel (state : State) (timerId : Nat) : ControlResult :=
  match state.terminal with
  | .closed => { status := .closed, state := state }
  | .open =>
      match state.claimed with
      | some timer =>
          if timer.id = timerId then
            { status := .fireWon, state := state }
          else if containsId state.active timerId then
            { status := .ok,
              state := { state with active := removeId state.active timerId } }
          else
            { status := .notFound, state := state }
      | none =>
          if containsId state.active timerId then
            { status := .ok,
              state := { state with active := removeId state.active timerId } }
          else
            { status := .notFound, state := state }

/-- One claimed timer performs exactly one Mailbox send and then leaves handoff. -/
def commit (state : State) : FireResult :=
  match state.claimed with
  | none =>
      { status := .invalidArgument
        timer := none
        mailboxStatus := none
        state := state }
  | some timer =>
      let sent := Mailbox.send state.mailbox timer.event
      { status := if sent.1 = .ok then .delivered else .mailboxRejected
        timer := some timer
        mailboxStatus := some sent.1
        state :=
          { state with
            active := removeId state.active timer.id
            claimed := none
            mailbox := sent.2 } }

/-- Close cancels pending timers but preserves a fire claim that already won. -/
def close (state : State) : ControlResult :=
  match state.terminal with
  | .closed => { status := .closed, state := state }
  | .open =>
      { status := .ok,
        state :=
          { state with active := state.claimed.toList, terminal := .closed } }

end CMetaCFlowCalculus.CFlow.TimerEvent
