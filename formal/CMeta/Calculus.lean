import Std

/-!
# CMeta finite generative calculus

This file models the semantic core behind the preprocessor layer.  It is not a
model of arbitrary token expansion.  The intended correspondence is:

* `source`  — a finite type/schema/relation list;
* `map`     — `FOR_EACH` / schema replay;
* `append`  — juxtaposition of generated artifact streams;
* `product` — nested `FOR_EACH_A/B/C` Cartesian products;
* `choose`  — finite compile-time routing/policy selection.

There is deliberately no general recursion/fixpoint constructor.
-/

namespace CMeta

universe u v

/-- Ordered Cartesian product, matching nested preprocessor foreach order. -/
def product (xs : List α) (ys : List β) : List (α × β) :=
  xs.flatMap (fun x => ys.map (fun y => (x, y)))

theorem product_length (xs : List α) (ys : List β) :
    (product xs ys).length = xs.length * ys.length := by
  induction xs with
  | nil => simp [product]
  | cons x xs ih =>
      simp [product, ih, Nat.succ_mul, Nat.add_comm, Nat.add_left_comm,
        Nat.add_assoc]

/-- A small typed algebra sufficient for the finite CMeta generators studied here. -/
inductive CoreExpr : Type u → Type (u + 1) where
  | source {α : Type u} : List α → CoreExpr α
  | map {α β : Type u} : (α → β) → CoreExpr α → CoreExpr β
  | append {α : Type u} : CoreExpr α → CoreExpr α → CoreExpr α
  | product {α β : Type u} : CoreExpr α → CoreExpr β → CoreExpr (α × β)
  | choose {α : Type u} : Bool → CoreExpr α → CoreExpr α → CoreExpr α

namespace CoreExpr

def eval {α : Type u} : CoreExpr α → List α
  | .source xs => xs
  | .map f e => (eval e).map f
  | .append a b => eval a ++ eval b
  | .product a b => CMeta.product (eval a) (eval b)
  | .choose p a b => if p then eval a else eval b

/-- Exact static output cardinality of a core expression. -/
def cardinality {α : Type u} : CoreExpr α → Nat
  | .source xs => xs.length
  | .map _ e => cardinality e
  | .append a b => cardinality a + cardinality b
  | .product a b => cardinality a * cardinality b
  | .choose p a b => if p then cardinality a else cardinality b

/-- Every core program evaluates to exactly its structurally computed finite size. -/
theorem eval_length_eq_cardinality {α : Type u} (e : CoreExpr α) :
    (eval e).length = cardinality e := by
  induction e with
  | source xs => rfl
  | map f e ih => simp [eval, cardinality, ih]
  | append a b iha ihb => simp [eval, cardinality, iha, ihb]
  | product a b iha ihb =>
      simp [eval, cardinality, CMeta.product_length, iha, ihb]
  | choose p a b iha ihb =>
      cases p <;> simp [eval, cardinality, iha, ihb]

/-- Mapping is cardinality preserving. -/
theorem map_cardinality (f : α → β) (e : CoreExpr α) :
    cardinality (.map f e) = cardinality e := rfl

/-- Sequencing adds expansion sizes. -/
theorem append_cardinality (a b : CoreExpr α) :
    cardinality (.append a b) = cardinality a + cardinality b := rfl

/-- Nested generation multiplies expansion sizes. -/
theorem product_cardinality (a : CoreExpr α) (b : CoreExpr β) :
    cardinality (.product a b) = cardinality a * cardinality b := rfl

end CoreExpr

/-- `repeat n` is semantically a map over the finite interval `[0,n)`. -/
def repeat (n : Nat) (emit : Nat → α) : List α :=
  (List.range n).map emit

theorem repeat_length (n : Nat) (emit : Nat → α) :
    (repeat n emit).length = n := by
  simp [repeat]

theorem repeat_index_domain (i n : Nat) :
    i ∈ List.range n ↔ i < n := by
  exact List.mem_range

theorem repeat_indices_unique (n : Nat) :
    (List.range n).Nodup := by
  exact List.nodup_range

/-- Schema replay is ordinary ordered map. -/
def replay (emit : α → β) (rows : List α) : List β := rows.map emit

theorem replay_length (emit : α → β) (rows : List α) :
    (replay emit rows).length = rows.length := by
  simp [replay]

/-- Two consumers of one schema remain positionally synchronized. -/
theorem replay_zip (left : α → β) (right : α → γ) (rows : List α) :
    List.zip (replay left rows) (replay right rows) =
      rows.map (fun x => (left x, right x)) := by
  induction rows with
  | nil => rfl
  | cons x xs ih => simp [replay, ih]

end CMeta
