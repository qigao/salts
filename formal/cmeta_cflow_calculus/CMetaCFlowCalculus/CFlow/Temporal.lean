namespace CMetaCFlowCalculus.CFlow.Temporal

inductive ReadyCause where
  | inner
  | timer
  deriving Repr, DecidableEq

inductive Terminal where
  | «open»
  | done
  | error
  deriving Repr, DecidableEq

structure State (α : Type) where
  pending : Option α
  scratch : Option α
  deadline : Option Nat
  terminal : Terminal
  deriving Repr

def delayDeadline (now duration : Nat) : Nat := now + duration

def debounceReplace (state : State α) (value : α) : State α :=
  { state with pending := some value, scratch := none }

def timeoutTerminal : ReadyCause → Terminal
  | .inner => .open
  | .timer => .error

def retainedCount (state : State α) : Nat :=
  state.pending.toList.length

end CMetaCFlowCalculus.CFlow.Temporal
