module
public import CMeta.Graph
import all CMeta.Graph
import all CMeta.Flow
import all CMeta.Callable

/-!
# Optimizer type and semantic preservation

This module models the type-relevant optimizer rewrites implemented in
`cflow/src/opt.c`:

* TRANSFORM canonicalization to MAP;
* fusion of a pure MAP/TRANSFORM run into one node carrying an ordered
  `fn_chain`;
* elimination of adjacent duplicate idempotent endomaps.

The C optimizer stores a chain rather than synthesizing a new C function. The
formal model therefore keeps the same ordered chain and proves both its dynamic
type check and its denotational composition.
-/

namespace CMeta

/-- A typed ordered map callback chain. Intermediate CType indices are hidden
    in the constructors but enforced by Lean. -/
public inductive MapChain : CType → CType → Type where
  | done (t : CType) : MapChain t t
  | cons {A B R : CType} : Callable [A] B → MapChain B R → MapChain A R

namespace MapChain

/-- Execute callbacks in the same order as CFlow's optimized `fn_chain`. -/
public def run : {A R : CType} → MapChain A R → A.denote → R.denote
  | _, _, .done _ => fun x => x
  | _, _, .cons fn rest => fun x => run rest (fn.invoke1 x)

/-- Erase a typed callback chain to the logical signatures carried by runtime
    callables. -/
public def signatures : {A R : CType} → MapChain A R → List Signature
  | _, _, .done _ => []
  | _, _, .cons fn rest => fn.unaryBackendSignature :: rest.signatures

/-- Dynamic checker for an erased unary callback chain. -/
public def check : CType → List Signature → Option CType
  | current, [] => some current
  | current, .unary input output :: rest =>
      if input = current then check output rest else none
  | _, _ :: _ => none

/-- Type preservation for the exact ordered callback chain. -/
theorem check_signatures {A R : CType} (chain : MapChain A R) :
    check A chain.signatures = some R := by
  induction chain with
  | done t => rfl
  | cons fn rest ih =>
      simp [signatures, check, Callable.unaryBackendSignature, ih]

/-- The semantic equation used by map fusion: a chain is ordinary function
    composition in callback order. -/
theorem run_cons {A B R : CType} (fn : Callable [A] B)
    (rest : MapChain B R) (x : A.denote) :
    (MapChain.cons fn rest).run x = rest.run (fn.invoke1 x) := rfl

end MapChain

/-- A fused MAP node is exactly one typed callback chain. This mirrors the
    optimizer's `fn_chain` plus the explicit tail `output_type`. -/
public structure FusedMap (A R : CType) where
  chain : MapChain A R

namespace FusedMap

/-- Even after callback type indices are erased, the fused node validates from
    its original input type to its chain-tail output type. -/
theorem type_preserved {A R : CType} (fused : FusedMap A R) :
    MapChain.check A fused.chain.signatures = some R :=
  fused.chain.check_signatures

end FusedMap

/-- Canonicalization used by the optimizer: TRANSFORM and MAP have the same
    type equation, so changing only the operator tag preserves the indices. -/
public def canonicalizeMapLike : {A R : CType} → TypedOp A R → TypedOp A R
  | _, _, .transform input output => .map input output
  | _, _, .filter t => .filter t
  | _, _, .map input output => .map input output
  | _, _, .flatMap input output => .flatMap input output
  | _, _, .reduce t => .reduce t
  | _, _, .zip left right output => .zip left right output

/-- Canonicalization remains accepted by the ordinary dynamic type checker. -/
theorem canonicalizeMapLike_preserves_type {A R : CType}
    (node : TypedOp A R) :
    stepType (canonicalizeMapLike node).operator A
      (canonicalizeMapLike node).signature = some R := by
  exact TypedOp.step_exact (canonicalizeMapLike node)

/-- Semantic contract corresponding to `CMETA_PROP_IDEMPOTENT` on a pure
    homogeneous map callback. -/
public structure IdempotentEndomap (T : CType) where
  fn : Callable [T] T
  law : ∀ x, fn.invoke1 (fn.invoke1 x) = fn.invoke1 x

/-- The optimizer may remove an adjacent duplicate only when the idempotence
    contract is valid. -/
theorem duplicate_idempotent_elimination_sound {T : CType}
    (f : IdempotentEndomap T) (x : T.denote) :
    f.fn.invoke1 (f.fn.invoke1 x) = f.fn.invoke1 x :=
  f.law x

/-- Removing the duplicate also preserves the logical input/output type. -/
theorem duplicate_idempotent_elimination_type {T : CType}
    (f : IdempotentEndomap T) :
    MapChain.check T [f.fn.unaryBackendSignature, f.fn.unaryBackendSignature] = some T ∧
    MapChain.check T [f.fn.unaryBackendSignature] = some T := by
  simp [MapChain.check, Callable.unaryBackendSignature]

end CMeta
