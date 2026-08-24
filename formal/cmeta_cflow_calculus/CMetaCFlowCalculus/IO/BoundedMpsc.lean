namespace CMetaCFlowCalculus.IO.BoundedMpsc

inductive Terminal where
  | «open»
  | draining
  deriving Repr, DecidableEq

inductive Admission where
  | accepted
  | full
  | closed
  deriving Repr, DecidableEq

inductive Observation (α : Type) where
  | item (value : α)
  | empty
  | closed
  deriving Repr, DecidableEq

structure State (α : Type) where
  capacity : Nat
  queue : List α
  terminal : Terminal
  deriving Repr, DecidableEq

namespace State

def Valid (state : State α) : Prop :=
  0 < state.capacity ∧ state.queue.length ≤ state.capacity

end State

/-- A successful publication is the abstract MPSC linearization point. -/
def tryPublish (state : State α) (value : α) : Admission × State α :=
  match state.terminal with
  | .draining => (.closed, state)
  | .open =>
      if state.queue.length < state.capacity then
        (.accepted, { state with queue := state.queue ++ [value] })
      else
        (.full, state)

/-- The single logical consumer removes the oldest published value. -/
def tryConsume (state : State α) : Observation α × State α :=
  match state.queue with
  | value :: remaining =>
      (.item value, { state with queue := remaining })
  | [] =>
      match state.terminal with
      | .open => (.empty, state)
      | .draining => (.closed, state)

/-- Close rejects future publications while preserving accepted values. -/
def close (state : State α) : Admission × State α :=
  match state.terminal with
  | .open => (.accepted, { state with terminal := .draining })
  | .draining => (.closed, state)

end CMetaCFlowCalculus.IO.BoundedMpsc
