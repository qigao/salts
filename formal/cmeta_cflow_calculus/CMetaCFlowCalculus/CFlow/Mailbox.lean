import CMetaCFlowCalculus.CMeta.Ownership

namespace CMetaCFlowCalculus.CFlow.Mailbox

open CMetaCFlowCalculus.CMeta

/-- One row in the finite domain-event schema. -/
structure EventType where
  id : Nat
  payloadTy : Ty
  deriving Repr, DecidableEq

abbrev Schema := List EventType

namespace Schema

/-- Event identifiers are non-zero and unique within one finite schema. -/
def Valid (schema : Schema) : Prop :=
  (∀ eventType ∈ schema, eventType.id ≠ 0) ∧
    (schema.map EventType.id).Nodup

/-- Lookup is by stable event identifier, not descriptor address. -/
def lookup : Schema → Nat → Option EventType
  | [], _ => none
  | eventType :: remaining, id =>
      if eventType.id = id then some eventType else lookup remaining id

end Schema

/-- A heterogeneous event whose payload is indexed by its semantic CMeta type. -/
structure TypedEvent where
  id : Nat
  payloadTy : Ty
  payload : Value payloadTy

inductive Terminal where
  | open
  | draining
  | cancelled
  deriving Repr, DecidableEq

inductive Status where
  | ok
  | invalidArgument
  | typeMismatch
  | full
  | empty
  | closed
  | cancelled
  deriving Repr, DecidableEq

inductive ReceiveResult where
  | received (event : TypedEvent)
  | empty
  | closed
  | cancelled

structure State where
  schema : Schema
  capacity : Nat
  queue : List TypedEvent
  terminal : Terminal

namespace State

/-- The functional mailbox invariant represented by the bounded C ring. -/
def Valid (state : State) : Prop :=
  0 < state.capacity ∧ state.queue.length ≤ state.capacity

end State

/-- Pure MPSC admission linearized at one successful commit point. -/
def send (state : State) (event : TypedEvent) : Status × State :=
  match state.schema.lookup event.id with
  | none => (.invalidArgument, state)
  | some expected =>
      if expected.payloadTy = event.payloadTy then
        match state.terminal with
        | .cancelled => (.cancelled, state)
        | .draining => (.closed, state)
        | .open =>
            if state.queue.length < state.capacity then
              (.ok, { state with queue := state.queue ++ [event] })
            else
              (.full, state)
      else
        (.typeMismatch, state)

/-- Observe and remove the oldest event, except that cancellation is terminal. -/
def receive (state : State) : ReceiveResult × State :=
  match state.terminal with
  | .cancelled => (.cancelled, state)
  | .open =>
      match state.queue with
      | [] => (.empty, state)
      | event :: remaining =>
          (.received event, { state with queue := remaining })
  | .draining =>
      match state.queue with
      | [] => (.closed, state)
      | event :: remaining =>
          (.received event, { state with queue := remaining })

/-- Graceful close preserves accepted events for draining. -/
def close (state : State) : Status × State :=
  match state.terminal with
  | .open => (.ok, { state with terminal := .draining })
  | .draining => (.closed, state)
  | .cancelled => (.cancelled, state)

/-- Cancellation is absorbing and discards every accepted event. -/
def cancel (state : State) : Status × State :=
  let status :=
    if state.terminal = .cancelled then Status.cancelled else Status.ok
  (status, { state with queue := [], terminal := .cancelled })

end CMetaCFlowCalculus.CFlow.Mailbox
