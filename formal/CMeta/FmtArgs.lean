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
inductive Slot (α : Type) where
  | none
  | arg (value : α)
  deriving Repr, DecidableEq

/-- The current storage behavior after the 0..8 arity dispatch has resolved. -/
def legacyStorage : List α → List (Slot α)
  | [] => [.none]
  | x :: xs => (x :: xs).map (fun value => .arg value)

/-- Proposed semantic normal form: replay every real argument, then append NONE. -/
def normalizedStorage (xs : List α) : List (Slot α) :=
  xs.map (fun value => .arg value) ++ [.none]

/-- Model of what `fmt_print(args, arg_count)` may observe from the argument
    array: at most the first `arg_count` slots. -/
def observe : Nat → List α → List α
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
      simp [legacyStorage, observe_length]

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
def legacyDispatch (xs : List α) : Option (List (Slot α)) :=
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
    logical argument count.  This is the algebraic basis for deriving count
    from storage size in a later C implementation, if desired. -/
theorem normalized_storage_length (xs : List α) :
    (normalizedStorage xs).length = xs.length + 1 := by
  simp [normalizedStorage]

end FmtArgs
end CMeta
