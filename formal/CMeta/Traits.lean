module
import CMeta.Calculus

/-!
# Trait/type inference model

The premise used by the higher-order proofs is explicit: a trait resolver can
recover a CMeta type descriptor for an expression.  The resolver is partial
(`Option`) because unsupported C expressions must remain rejectable.
-/

namespace CMeta

universe u

/-- Finite logical type universe used by the proof model. -/
public inductive CType where
  | bool
  | int
  | long
  | float
  | double
  deriving Repr, DecidableEq

/-- Logical callable signatures corresponding to CMeta unary/binary/generator ABIs. -/
public inductive Signature where
  | unary (input output : CType)
  | binary (left right output : CType)
  | generator (input output : CType)
  deriving Repr, DecidableEq

/-- A trait environment is precisely the premise "type can be recovered". -/
public structure Traits (Expr : Type u) where
  typeOf : Expr → Option CType

/-- Functional trait lookup cannot assign two different types to one expression. -/
theorem Traits.type_unique (tr : Traits Expr) (e : Expr)
    (a b : CType)
    (ha : tr.typeOf e = some a)
    (hb : tr.typeOf e = some b) : a = b := by
  have h : (some a : Option CType) = some b := ha.symm.trans hb
  exact Option.some.inj h

/-- Infer a unary callable signature from two trait-resolved expressions. -/
public def Traits.inferUnary (tr : Traits Expr) (input result : Expr) : Option Signature :=
  match tr.typeOf input, tr.typeOf result with
  | some a, some r => some (.unary a r)
  | _, _ => none

theorem Traits.inferUnary_of_known (tr : Traits Expr) (input result : Expr)
    (a r : CType)
    (hi : tr.typeOf input = some a)
    (hr : tr.typeOf result = some r) :
    tr.inferUnary input result = some (.unary a r) := by
  simp [Traits.inferUnary, hi, hr]

/-- Signature inference is deterministic once traits are fixed. -/
theorem Traits.inferUnary_unique (tr : Traits Expr) (input result : Expr)
    (s1 s2 : Signature)
    (h1 : tr.inferUnary input result = some s1)
    (h2 : tr.inferUnary input result = some s2) : s1 = s2 := by
  have h : (some s1 : Option Signature) = some s2 := h1.symm.trans h2
  exact Option.some.inj h

/-- A finite signature policy is the formal counterpart of CFlow's generated lists. -/
public abbrev SignaturePolicy := List Signature

/-- Admission is decidable because the signature universe has decidable equality. -/
public def policyAllows (policy : SignaturePolicy) (sig : Signature) : Bool :=
  policy.contains sig

theorem policyAllows_iff (policy : SignaturePolicy) (sig : Signature) :
    policyAllows policy sig = true ↔ sig ∈ policy := by
  simp [policyAllows]

end CMeta
