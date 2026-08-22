module
import all CMeta.Producer

/-!
# CMeta nested Producer replay

Nested replay is a semantic finite Cartesian map.  Expansion lanes, macro
self-suppression, deferral and rescan budgets are deliberately absent from this
model: they are strict-C11 preprocessor backend concerns.
-/

namespace CMeta
namespace Producer

/-- Ordered Cartesian replay: for each left item, replay every right item. -/
public def nestedReplay (f : α → β → γ) : List α → List β → List γ
  | [], _ => []
  | x :: xs, ys => ys.map (f x) ++ nestedReplay f xs ys

/-- An empty left producer emits no nested mapper applications. -/
theorem nestedReplay_empty_left (f : α → β → γ) (ys : List β) :
    nestedReplay f [] ys = [] := rfl

/-- An empty right producer emits no nested mapper applications. -/
theorem nestedReplay_empty_right (f : α → β → γ) (xs : List α) :
    nestedReplay f xs [] = [] := by
  induction xs with
  | nil => rfl
  | cons x xs ih => simp [nestedReplay, ih]

/-- Nested replay cardinality is the Cartesian product of producer lengths. -/
theorem nestedReplay_length (f : α → β → γ) (xs : List α) (ys : List β) :
    (nestedReplay f xs ys).length = xs.length * ys.length := by
  induction xs with
  | nil => simp [nestedReplay]
  | cons x xs ih =>
      simp [nestedReplay, ih, Nat.succ_mul, Nat.add_comm]

/-- Replaying a producer against itself has square cardinality. -/
theorem nestedReplay_same_length (f : α → α → γ) (xs : List α) :
    (nestedReplay f xs xs).length = xs.length * xs.length := by
  exact nestedReplay_length f xs xs

/-- Producer-derived count agrees with nested Cartesian cardinality. -/
theorem nestedReplay_count (f : α → β → γ) (xs : List α) (ys : List β) :
    count (nestedReplay f xs ys) = count xs * count ys := by
  rw [count_eq_length, nestedReplay_length, count_eq_length, count_eq_length]

end Producer
end CMeta
