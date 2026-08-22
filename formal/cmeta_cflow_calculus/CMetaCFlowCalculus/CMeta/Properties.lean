import CMetaCFlowCalculus.CMeta.Types

namespace CMetaCFlowCalculus.CMeta

/-- Positive semantic guarantees used as trusted rewrite premises. -/
inductive Property where
  | deterministic
  | total
  | idempotent
  | associative
  | commutative
  | stable
  deriving Repr, DecidableEq

abbrev PropertySet := Property → Prop

namespace PropertySet

def empty : PropertySet := fun _ => False

def ofList (properties : List Property) : PropertySet :=
  fun property => property ∈ properties

def insert (property : Property) (properties : PropertySet) : PropertySet :=
  fun candidate => candidate = property ∨ properties candidate

def union (left right : PropertySet) : PropertySet :=
  fun property => left property ∨ right property

def intersection (left right : PropertySet) : PropertySet :=
  fun property => left property ∧ right property

theorem insert_self (property : Property) (properties : PropertySet) :
    insert property properties property := by
  exact Or.inl rfl

end PropertySet
end CMetaCFlowCalculus.CMeta
