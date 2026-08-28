namespace CMetaCFlowCalculus.CFlow.PlanManagedValues

/-- The unique owner of a compiled materialized live prefix. Caller input is
    borrowed and therefore does not appear in this ownership state. -/
inductive Owner where
  | vector
  | result
  | none
  deriving Repr, DecidableEq

inductive Phase where
  | building
  | committed
  | failed
  | released
  deriving Repr, DecidableEq

/-- Count abstraction of the C live-prefix executor. `constructed` and
    `destroyed` count constructed object instances; move construction adds one
    construction and destroys its source while preserving the live count. -/
structure State where
  owner : Owner
  phase : Phase
  constructed : Nat
  destroyed : Nat
  live : Nat
  deriving Repr, DecidableEq

namespace State

def Balanced (state : State) : Prop :=
  state.constructed = state.destroyed + state.live

def OwnershipValid (state : State) : Prop :=
  match state.phase with
  | .building => state.owner = .vector
  | .committed => state.owner = .result
  | .failed | .released => state.owner = .none ∧ state.live = 0

def Safe (state : State) : Prop :=
  state.Balanced ∧ state.OwnershipValid

def empty : State :=
  { owner := .vector
    phase := .building
    constructed := 0
    destroyed := 0
    live := 0 }

/-- A successful copy extends the live prefix. A failed copy constructs
    nothing and leaves the complete state unchanged. -/
def copyOne (state : State) (succeeded : Bool) : State :=
  match state.owner, state.phase, succeeded with
  | .vector, .building, true =>
      { state with
        constructed := state.constructed + 1
        live := state.live + 1 }
  | _, _, _ => state

/-- Relocation move-constructs one destination and destroys one source. -/
def moveOne (state : State) : Option State :=
  match state.owner, state.phase, state.live with
  | .vector, .building, _live + 1 =>
      some { state with
        constructed := state.constructed + 1
        destroyed := state.destroyed + 1 }
  | _, _, _ => none

/-- Filtering, take, or successful replacement destroys a suffix/subset only
    after its replacement prefix is complete. -/
def discard (state : State) (count : Nat) : Option State :=
  if state.owner = .vector ∧ state.phase = .building ∧ count ≤ state.live then
    some { state with
      destroyed := state.destroyed + count
      live := state.live - count }
  else
    none

/-- Any execution failure destroys the complete live prefix and publishes no
    result. -/
def failCleanup (state : State) : Option State :=
  match state.owner, state.phase with
  | .vector, .building =>
      some { state with
        owner := .none
        phase := .failed
        destroyed := state.destroyed + state.live
        live := 0 }
  | _, _ => none

/-- Commit transfers the complete prefix to the public result without changing
    lifecycle counts. -/
def commit (state : State) : Option State :=
  match state.owner, state.phase with
  | .vector, .building =>
      some { state with owner := .result, phase := .committed }
  | _, _ => none

/-- Result destruction consumes the sole result owner. A released state cannot
    be released a second time. -/
def release (state : State) : Option State :=
  match state.owner, state.phase with
  | .result, .committed =>
      some { state with
        owner := .none
        phase := .released
        destroyed := state.destroyed + state.live
        live := 0 }
  | _, _ => none

end State

export State
  (Balanced OwnershipValid Safe commit copyOne discard empty failCleanup
   moveOne release)

end CMetaCFlowCalculus.CFlow.PlanManagedValues
