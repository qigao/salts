import CMeta.TypeIdentity

namespace CMeta

/-!
# Descriptor bridge semantics

`DescriptorView` separates legacy representation metadata from structural
`TypeId`. Semantic equality is equality of a tagged semantic key:

- structural descriptors use only `TypeId`;
- legacy descriptors use only the legacy projection;
- structural and legacy descriptors inhabit disjoint key variants.

Descriptor/object addresses are intentionally absent from the model.
-/

inductive LegacyKind where
  | void
  | bool
  | integer
  | float
  | pointer
  | object
  deriving Repr, DecidableEq

/-- Finite projection of the current legacy comparison fields. `pointeeKey`
represents the recursively compared legacy pointee projection when present. -/
structure LegacyDesc where
  name : String
  size : Nat
  align : Nat
  kind : LegacyKind
  pointeeKey : Option String := none
  deriving Repr, DecidableEq

structure DescriptorView where
  identity : Option TypeId
  legacy : LegacyDesc
  deriving Repr

inductive DescriptorSemanticKey where
  | legacy (desc : LegacyDesc)
  | structural (typeId : TypeId)

/-- One semantic authority per descriptor. The tag is the mixed-mode barrier. -/
def DescriptorView.semanticKey (desc : DescriptorView) : DescriptorSemanticKey :=
  match desc.identity with
  | some typeId => .structural typeId
  | none => .legacy desc.legacy

/-- Proposition-level semantic equality. This definition does not need a
`DecidableEq TypeId`; it uses Lean equality directly. -/
def DescriptorView.SemanticallyEqual (a b : DescriptorView) : Prop :=
  a.semanticKey = b.semanticKey

/-- Executable counterpart used by concrete C conformance witnesses. -/
def DescriptorView.semanticEqBool (a b : DescriptorView) : Bool :=
  match a.identity, b.identity with
  | some x, some y => x == y
  | none, none => decide (a.legacy = b.legacy)
  | _, _ => false

/-- Whether the descriptor is on the structural side of the migration boundary. -/
def DescriptorView.hasStructuralIdentity (desc : DescriptorView) : Bool :=
  match desc.identity with
  | some _ => true
  | none => false

/-- Semantic equality is reflexive. -/
theorem DescriptorView.semanticEq_refl (desc : DescriptorView) :
    desc.SemanticallyEqual desc := by
  rfl

/-- Semantic equality is symmetric. -/
theorem DescriptorView.semanticEq_symm {a b : DescriptorView}
    (h : a.SemanticallyEqual b) : b.SemanticallyEqual a := by
  exact h.symm

/-- Semantic equality is transitive. -/
theorem DescriptorView.semanticEq_trans {a b c : DescriptorView}
    (hab : a.SemanticallyEqual b)
    (hbc : b.SemanticallyEqual c) : a.SemanticallyEqual c := by
  exact hab.trans hbc

/-- Equal structural identities dominate any differences in legacy metadata. -/
theorem DescriptorView.structural_identity_dominates
    (typeId : TypeId) (legacyA legacyB : LegacyDesc) :
    ({ identity := some typeId, legacy := legacyA } : DescriptorView).SemanticallyEqual
      ({ identity := some typeId, legacy := legacyB } : DescriptorView) := by
  rfl

/-- Different structural identities cannot be made equal by matching legacy metadata. -/
theorem DescriptorView.different_structural_identity_is_distinct
    (a b : TypeId) (legacy : LegacyDesc) (h : a ≠ b) :
    ¬ ({ identity := some a, legacy := legacy } : DescriptorView).SemanticallyEqual
      ({ identity := some b, legacy := legacy } : DescriptorView) := by
  intro eqKey
  have : a = b := by
    cases eqKey
    rfl
  exact h this

/-- Structural and legacy descriptors are deliberately isolated. -/
theorem DescriptorView.mixed_mode_isolated
    (typeId : TypeId) (structuralLegacy legacyOnly : LegacyDesc) :
    ¬ ({ identity := some typeId, legacy := structuralLegacy } : DescriptorView).SemanticallyEqual
      ({ identity := none, legacy := legacyOnly } : DescriptorView) := by
  intro h
  cases h

end CMeta
