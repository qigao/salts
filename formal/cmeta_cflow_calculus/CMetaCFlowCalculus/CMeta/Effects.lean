import Init.Data.Order.Classes

namespace CMetaCFlowCalculus.CMeta

/-- Increasing points in the v1 observable-effect lattice. -/
inductive Effect where
  | pure
  | readOnly
  | stateful
  | external
  deriving Repr, DecidableEq

namespace Effect

def rank : Effect → Nat
  | .pure => 0
  | .readOnly => 1
  | .stateful => 2
  | .external => 3

instance effectLE : LE Effect where
  le left right := rank left ≤ rank right

instance (left right : Effect) : Decidable (left ≤ right) :=
  inferInstanceAs (Decidable (rank left ≤ rank right))

theorem rank_injective : Function.Injective rank := by
  intro left right equalRank
  cases left <;> cases right <;> simp_all [rank]

instance : Std.IsPartialOrder Effect where
  le_refl effect := Nat.le_refl (rank effect)
  le_trans _ _ _ leftMiddle middleRight :=
    Nat.le_trans leftMiddle middleRight
  le_antisymm _ _ leftRight rightLeft :=
    rank_injective (Nat.le_antisymm leftRight rightLeft)

/-- Least effect that conservatively includes both operands. -/
def join : Effect → Effect → Effect
  | .external, _ => .external
  | _, .external => .external
  | .stateful, _ => .stateful
  | _, .stateful => .stateful
  | .readOnly, _ => .readOnly
  | _, .readOnly => .readOnly
  | .pure, .pure => .pure

theorem join_comm (left right : Effect) :
    join left right = join right left := by
  cases left <;> cases right <;> rfl

theorem join_assoc (first second third : Effect) :
    join (join first second) third = join first (join second third) := by
  cases first <;> cases second <;> cases third <;> rfl

theorem join_idem (effect : Effect) : join effect effect = effect := by
  cases effect <;> rfl

theorem le_join_left (left right : Effect) : left ≤ join left right := by
  cases left <;> cases right <;> decide

theorem le_join_right (left right : Effect) : right ≤ join left right := by
  rw [join_comm]
  exact le_join_left right left

theorem join_least (left right upper : Effect)
    (leftBound : left ≤ upper) (rightBound : right ≤ upper) :
    join left right ≤ upper := by
  cases left <;> cases right <;> cases upper <;>
    simp_all [join]

instance : Max Effect where
  max := join

instance : Std.LawfulOrderSup Effect where
  max_le_iff := by
    intro left right upper
    change join left right ≤ upper ↔ left ≤ upper ∧ right ≤ upper
    constructor
    · intro joinedBound
      exact ⟨Nat.le_trans (le_join_left left right) joinedBound,
        Nat.le_trans (le_join_right left right) joinedBound⟩
    · intro bounds
      exact join_least left right upper bounds.1 bounds.2

end Effect
end CMetaCFlowCalculus.CMeta
