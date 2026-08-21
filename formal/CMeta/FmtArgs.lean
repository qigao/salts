module
import CMeta.Calculus

/-!
# FMT variadic argument normalization

This file models the semantic content of `FMT_ARGS` independently from raw
preprocessor tokens.  The current C macros have one special empty case and an
arity-dispatch family for one through eight arguments.  Every non-empty branch
ultimately performs the same CMeta `FOR_EACH(FMT_ARG, ...)` replay.

The formatter receives an explicit `arg_count` and reads `args[i]` only while
`i < arg_count`.  Therefore storage after the first `arg_count` slots is not
observable by formatting.  This lets us compare the current representation with
a simpler normal form that always appends one `FMT_TYPE_NONE` sentinel.
-/

namespace CMeta
namespace FmtArgs

/-- Semantic slot corresponding to one `fmt_arg_t` entry. -/
public inductive Slot (α : Type) where
  | none
  | arg (value : α)
  deriving Repr, DecidableEq

/-- The current storage behavior after the 0..8 arity dispatch has resolved. -/
public def legacyStorage : List α → List (Slot α)
  | [] => [.none]
  | x :: xs => (x :: xs).map (fun value => .arg value)

/-- Proposed semantic normal form: replay every real argument, then append NONE. -/
public def normalizedStorage (xs : List α) : List (Slot α) :=
  xs.map (fun value => .arg value) ++ [.none]

/-- Model of what `fmt_print(args, arg_count)` may observe from the argument
    array: at most the first `arg_count` slots. -/
public def observe : Nat → List α → List α
  | 0, _ => []
  | _ + 1, [] => []
  | n + 1, x :: xs => x :: observe n xs

/-- Reading exactly the logical length exposes exactly that list. -/
theorem observe_length (xs : List α) :
    observe xs.length xs = xs := by
  induction xs with
  | nil => rfl
  | cons x xs ih => simp [observe, ih]

/-- Appending one storage-only sentinel cannot be observed when the logical
    count remains the length of the real prefix. -/
theorem appended_sentinel_unobservable (xs : List α) (sentinel : α) :
    observe xs.length (xs ++ [sentinel]) = xs := by
  induction xs with
  | nil => rfl
  | cons x xs ih => simp [observe, ih]

/-- The current empty representation is observationally an empty argument list:
    its single NONE slot is hidden by `arg_count = 0`. -/
theorem legacy_empty_observation :
    observe 0 (legacyStorage ([] : List α)) = [] := rfl

/-- Every current non-empty arity branch is exactly the same map/replay shape. -/
theorem legacy_nonempty_is_replay (x : α) (xs : List α) :
    legacyStorage (x :: xs) = (x :: xs).map (fun value => Slot.arg value) := rfl

/-- The current storage observed through its logical count is exactly the
    mapped real argument sequence, for every finite list. -/
theorem legacy_observation (xs : List α) :
    observe xs.length (legacyStorage xs) =
      xs.map (fun value => Slot.arg value) := by
  cases xs with
  | nil => rfl
  | cons x xs =>
      have h := observe_length ((x :: xs).map (fun value => Slot.arg value))
      simpa [legacyStorage] using h

/-- The always-sentinel normal form has the same observable argument sequence. -/
theorem normalized_observation (xs : List α) :
    observe xs.length (normalizedStorage xs) =
      xs.map (fun value => Slot.arg value) := by
  unfold normalizedStorage
  have h := appended_sentinel_unobservable
    (xs.map (fun value => Slot.arg value)) (Slot.none : Slot α)
  simpa using h

/-- Main simplification theorem: the legacy empty/non-empty storage scheme and
    the always-sentinel normal form are observationally equivalent for every
    finite argument list. -/
theorem legacy_normalized_observational_equivalence (xs : List α) :
    observe xs.length (legacyStorage xs) =
      observe xs.length (normalizedStorage xs) := by
  rw [legacy_observation, normalized_observation]

/-- Model the current FMT dispatcher limit.  Its 0..8 routing is a capacity
    restriction, not a semantic distinction among non-empty arities. -/
public def legacyDispatch (xs : List α) : Option (List (Slot α)) :=
  if xs.length ≤ 8 then some (legacyStorage xs) else none

/-- Within the exact current macro domain, dispatch contributes no semantics
    beyond selecting the already-defined legacy storage. -/
theorem legacy_dispatch_resolves (xs : List α) (h : xs.length ≤ 8) :
    legacyDispatch xs = some (legacyStorage xs) := by
  simp [legacyDispatch, h]

/-- Consequently every currently accepted arity has the same observation as
    the normal form. -/
theorem legacy_dispatch_normalizes (xs : List α) (h : xs.length ≤ 8) :
    (legacyDispatch xs).map (fun storage => observe xs.length storage) =
      some (observe xs.length (normalizedStorage xs)) := by
  rw [legacy_dispatch_resolves xs h]
  simp [legacy_normalized_observational_equivalence]

/-- The normalized representation has one storage slot more than the real
    logical argument count. -/
theorem normalized_storage_length (xs : List α) :
    (normalizedStorage xs).length = xs.length + 1 := by
  simp [normalizedStorage]

/-- Recover a logical argument count from a normalized storage representation. -/
public def argCountFromStorage (storage : List α) : Nat :=
  storage.length - 1

/-- Normalized storage makes the logical argument count exactly recoverable
    from storage length alone. -/
theorem normalized_storage_recovers_arg_count (xs : List α) :
    argCountFromStorage (normalizedStorage xs) = xs.length := by
  unfold argCountFromStorage
  rw [normalized_storage_length]
  simp

/-- The legacy storage length is not sufficient to recover argument count:
    zero arguments and one argument both occupy one storage slot. -/
theorem legacy_storage_length_not_injective (x : α) :
    (legacyStorage ([] : List α)).length = (legacyStorage [x]).length ∧
      ([] : List α).length ≠ [x].length := by
  simp [legacyStorage]

/-- The formatter's read guard can be derived from normalized storage alone. -/
public def canReadRealArg (storage : List α) (index : Nat) : Prop :=
  index < argCountFromStorage storage

/-- The derived guard accepts exactly the indices of real arguments.  Arity is
    not stored separately: it is recovered from normalized storage length. -/
theorem normalized_guard_exactly_real_indices (xs : List α) (index : Nat) :
    canReadRealArg (normalizedStorage xs) index ↔ index < xs.length := by
  simp [canReadRealArg, normalized_storage_recovers_arg_count]

/-- The prefix visible through the derived guard consists only of real argument
    slots; the trailing NONE sentinel is not part of the observable prefix. -/
theorem normalized_guarded_prefix (xs : List α) :
    observe (argCountFromStorage (normalizedStorage xs)) (normalizedStorage xs) =
      xs.map (fun value => Slot.arg value) := by
  rw [normalized_storage_recovers_arg_count]
  exact normalized_observation xs

/-- Every index accepted by the derived guard is physically inside normalized
    storage.  This is the bounds-safety implication required before dereference. -/
theorem normalized_guard_implies_physical_bound
    (xs : List α) (index : Nat)
    (h : canReadRealArg (normalizedStorage xs) index) :
    index < (normalizedStorage xs).length := by
  have hreal : index < xs.length :=
    (normalized_guard_exactly_real_indices xs index).mp h
  rw [normalized_storage_length]
  exact Nat.lt_trans hreal (by simp)

/-- The derived count is the physical index of the trailing sentinel. -/
theorem normalized_sentinel_is_inside_storage (xs : List α) :
    argCountFromStorage (normalizedStorage xs) < (normalizedStorage xs).length := by
  rw [normalized_storage_recovers_arg_count, normalized_storage_length]
  simp

/-- The real-argument guard rejects the sentinel itself. -/
theorem normalized_guard_rejects_sentinel (xs : List α) :
    ¬ canReadRealArg
        (normalizedStorage xs)
        (argCountFromStorage (normalizedStorage xs)) := by
  simp [canReadRealArg]

/-- One index after the sentinel is already outside physical storage.  Therefore
    a single sentinel cannot make an unbounded sequence of reads safe. -/
theorem normalized_one_past_sentinel_is_oob (xs : List α) :
    ¬ (argCountFromStorage (normalizedStorage xs) + 1 <
        (normalizedStorage xs).length) := by
  rw [normalized_storage_recovers_arg_count, normalized_storage_length]
  simp

/-- Combined boundary fact: the sentinel occupies the first non-argument slot,
    and the immediately following index is out of bounds.  Bounds protection
    still requires the derived guard; the sentinel alone is not a guard. -/
theorem trailing_sentinel_does_not_replace_guard (xs : List α) :
    argCountFromStorage (normalizedStorage xs) < (normalizedStorage xs).length ∧
      ¬ (argCountFromStorage (normalizedStorage xs) + 1 <
          (normalizedStorage xs).length) := by
  exact ⟨normalized_sentinel_is_inside_storage xs,
         normalized_one_past_sentinel_is_oob xs⟩

end FmtArgs
end CMeta
