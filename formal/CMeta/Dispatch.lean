module
public import CMeta.Traits
import all CMeta.Traits

/-!
# Operator policy and deterministic dispatch

This models the generated `_Generic`/signature-list dispatch used by CFlow.
Traits produce a `Signature`; an operator policy admits a finite set of such
signatures; dispatch resolves an admitted `(operator, signature)` key to one
implementation target.
-/

namespace CMeta

public inductive Operator where
  | filter
  | map
  | transform
  | flatMap
  | reduce
  | zip
  deriving Repr, DecidableEq

public structure DispatchRule where
  op : Operator
  sig : Signature
  target : Nat
  deriving Repr, DecidableEq

/-- First-match finite dispatch. -/
public def dispatch : List DispatchRule → Operator → Signature → Option Nat
  | [], _, _ => none
  | r :: rs, op, sig =>
      if r.op = op ∧ r.sig = sig then some r.target
      else dispatch rs op sig

/-- Every successful dispatch comes from an actual matching finite rule. -/
theorem dispatch_sound (rules : List DispatchRule) (op : Operator)
    (sig : Signature) (target : Nat)
    (h : dispatch rules op sig = some target) :
    ∃ r, r ∈ rules ∧ r.op = op ∧ r.sig = sig ∧ r.target = target := by
  induction rules with
  | nil => simp [dispatch] at h
  | cons r rs ih =>
      by_cases hm : r.op = op ∧ r.sig = sig
      · simp [dispatch, hm] at h
        exact ⟨r, by simp, hm.1, hm.2, h⟩
      · have ht : dispatch rs op sig = some target := by
          simpa [dispatch, hm] using h
        obtain ⟨q, hqmem, hqop, hqsig, hqtarget⟩ := ih ht
        exact ⟨q, by simp [hqmem], hqop, hqsig, hqtarget⟩

/-- A policy associates each operator with its finite set of admitted signatures. -/
public abbrev OperatorPolicy := Operator → SignaturePolicy

public def RulesRespectPolicy (policy : OperatorPolicy) (rules : List DispatchRule) : Prop :=
  ∀ r, r ∈ rules → r.sig ∈ policy r.op

/-- Dispatch cannot escape the operator's type policy when the table is well formed. -/
theorem dispatch_policy_sound (policy : OperatorPolicy) (rules : List DispatchRule)
    (hwf : RulesRespectPolicy policy rules)
    (op : Operator) (sig : Signature) (target : Nat)
    (h : dispatch rules op sig = some target) :
    sig ∈ policy op := by
  obtain ⟨r, hrmem, hrop, hrsig, _⟩ := dispatch_sound rules op sig target h
  have hp := hwf r hrmem
  simpa [hrop, hrsig] using hp

/-- Unary composition can itself be dispatched/type-checked from signatures. -/
public def composeSignature : Signature → Signature → Option Signature
  | .unary a b, .unary b' c =>
      if b = b' then some (.unary a c) else none
  | _, _ => none

theorem composeSignature_unary (a b c : CType) :
    composeSignature (.unary a b) (.unary b c) = some (.unary a c) := by
  simp [composeSignature]

theorem composeSignature_rejects_mismatch (a b b' c : CType) (h : b ≠ b') :
    composeSignature (.unary a b) (.unary b' c) = none := by
  simp [composeSignature, h]

/-- Trait inference followed by policy admission is a deterministic pipeline. -/
public def inferAndAllow (tr : Traits Expr) (policy : OperatorPolicy)
    (op : Operator) (input result : Expr) : Bool :=
  match tr.inferUnary input result with
  | some sig => policyAllows (policy op) sig
  | none => false

theorem inferAndAllow_known (tr : Traits Expr) (policy : OperatorPolicy)
    (op : Operator) (input result : Expr) (a r : CType)
    (hi : tr.typeOf input = some a) (hr : tr.typeOf result = some r) :
    inferAndAllow tr policy op input result =
      policyAllows (policy op) (.unary a r) := by
  simp [inferAndAllow, Traits.inferUnary, hi, hr]

end CMeta