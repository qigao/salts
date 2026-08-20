import CMeta.Execution

/-!
# Execution cardinality safety

This module proves the count semantics of the direct execution steps modeled in
`CMeta.Execution`, matching the cardinality classes carried by CFlow operator
schemas:

* FILTER never increases the number of values;
* MAP (including optimized `fn_chain`) preserves the number of values exactly;
* FLAT_MAP produces exactly the sum of the completed generator outputs;
* REDUCE maps empty input to zero values and non-empty input to exactly one.

The FLAT_MAP theorem is conditional on the finite successful generator trace
already modeled by `CompletedGenerator`; it is not a generator-termination
claim.
-/

namespace CMeta

private theorem filter_length_le {α : Type} (pred : α → Bool) (xs : List α) :
    (xs.filter pred).length ≤ xs.length := by
  induction xs with
  | nil => simp
  | cons x xs ih =>
      cases h : pred x with
      | false =>
          simp [h]
          exact Nat.le_trans ih (Nat.le_succ _)
      | true =>
          simp [h]
          exact Nat.succ_le_succ ih

private theorem flatMap_length_sum {α β : Type} (emit : α → List β)
    (xs : List α) :
    (xs.flatMap emit).length =
      (xs.map (fun x => (emit x).length)).sum := by
  induction xs with
  | nil => rfl
  | cons x xs ih => simp [ih]

/-- FILTER may discard values but cannot create new ones. -/
theorem ExecInst.filter_cardinality {T : CType} (pred : Callable1 T .bool)
    (xs : ValueVec T) :
    (ExecInst.run (.filter T pred) xs).length ≤ xs.length := by
  change (xs.filter pred.run).length ≤ xs.length
  exact filter_length_le pred.run xs

/-- MAP preserves cardinality even when its implementation is a fused callback
    chain. -/
theorem ExecInst.map_cardinality {A R : CType} (chain : MapChain A R)
    (xs : ValueVec A) :
    (ExecInst.run (.map A R chain) xs).length = xs.length := by
  change (xs.map chain.run).length = xs.length
  exact ExecInst.map_length chain xs

/-- FLAT_MAP cardinality is exactly the sum of the outputs produced for each
    input element by the completed generator traces. -/
theorem ExecInst.flatMap_cardinality {A R : CType}
    (gen : CompletedGenerator A R) (xs : ValueVec A) :
    (ExecInst.run (.flatMap A R gen) xs).length =
      (xs.map (fun x => (gen.generateAll x).length)).sum := by
  change (xs.flatMap gen.generateAll).length =
    (xs.map (fun x => (gen.generateAll x).length)).sum
  exact flatMap_length_sum gen.generateAll xs

/-- Count transfer function for REDUCE. -/
def reduceCount : Nat → Nat
  | 0 => 0
  | _ + 1 => 1

/-- REDUCE produces no value from empty input and exactly one value from every
    non-empty input. -/
theorem ExecInst.reduce_cardinality {T : CType} (reducer : Callable2 T T T)
    (xs : ValueVec T) :
    (ExecInst.run (.reduce T reducer) xs).length = reduceCount xs.length := by
  cases xs <;> simp [ExecInst.run, reduceValues, reduceCount]

/-- The weaker cardinality bound previously used by the executor follows from
    the exact REDUCE count equation. -/
theorem ExecInst.reduce_cardinality_le_one {T : CType}
    (reducer : Callable2 T T T) (xs : ValueVec T) :
    (ExecInst.run (.reduce T reducer) xs).length ≤ 1 := by
  rw [ExecInst.reduce_cardinality reducer xs]
  cases xs <;> simp [reduceCount]

end CMeta
