module
import Std

/-!
# CMeta Producer / Replay algebra

The semantic representation of a finite zero-or-more CMeta sequence is a
producer sequence, modeled extensionally here by an ordinary finite `List`.
Raw C `__VA_ARGS__` is intentionally not part of this model.

`replay` maps a producer without first inspecting its arity.  `count`, storage,
and bounds are all derived from the same finite source.
-/

namespace CMeta
namespace Producer

/-- Replay a finite producer through one mapper. -/
public def replay (map : α → β) (xs : List α) : List β :=
  xs.map map

/-- Producer composition is ordinary ordered append. -/
public def append (xs ys : List α) : List α :=
  xs ++ ys

/-- Count by replaying the constant-one mapper and folding the result. -/
public def count (xs : List α) : Nat :=
  (replay (fun _ : α => 1) xs).foldr Nat.add 0

/-- Materialize mapped producer items and append one storage-only sentinel. -/
public def storage (map : α → β) (sentinel : β) (xs : List α) : List β :=
  replay map xs ++ [sentinel]

/-- Recover logical count from the normalized storage representation. -/
public def storageCount (values : List β) : Nat :=
  values.length - 1

/-- The only logical read guard needed by normalized producer storage. -/
public def canRead (values : List β) (i : Nat) : Prop :=
  i < storageCount values

/-- Zero elements replay to zero mapper applications. -/
theorem replay_empty (map : α → β) :
    replay map ([] : List α) = [] := rfl

/-- A singleton producer performs exactly one mapper application. -/
theorem replay_single (map : α → β) (x : α) :
    replay map [x] = [map x] := rfl

/-- Replay is a homomorphism from producer append to output append. -/
theorem replay_append (map : α → β) (xs ys : List α) :
    replay map (append xs ys) = replay map xs ++ replay map ys := by
  simp [replay, append]

/-- Replaying constant one and folding gives the producer's exact length. -/
-- TEMP-MODULE-BRIDGE(M7b): legacy NestedReplay.nestedReplay_count
public theorem count_eq_length (xs : List α) :
    count xs = xs.length := by
  induction xs with
  | nil => rfl
  | cons x xs ih =>
      simp [count, replay] at ih
      simp [count, replay, ih, Nat.add_comm]

/-- Normalized storage contains every logical item plus exactly one sentinel. -/
theorem storage_length (map : α → β) (sentinel : β) (xs : List α) :
    (storage map sentinel xs).length = count xs + 1 := by
  rw [count_eq_length]
  simp [storage, replay]

/-- Storage-derived count agrees with producer-derived count. -/
theorem storage_count_eq_count (map : α → β) (sentinel : β) (xs : List α) :
    storageCount (storage map sentinel xs) = count xs := by
  unfold storageCount
  rw [storage_length]
  simp

/-- The storage guard is exactly the logical producer-index bound. -/
theorem canRead_iff (map : α → β) (sentinel : β) (xs : List α) (i : Nat) :
    canRead (storage map sentinel xs) i ↔ i < xs.length := by
  unfold canRead
  rw [storage_count_eq_count, count_eq_length]

/-- Every logically readable index is also a physically valid storage index. -/
theorem canRead_implies_physical_bound
    (map : α → β) (sentinel : β) (xs : List α) (i : Nat)
    (h : canRead (storage map sentinel xs) i) :
    i < (storage map sentinel xs).length := by
  rw [canRead_iff] at h
  simpa [storage, replay] using Nat.lt_trans h (Nat.lt_succ_self xs.length)

/-- The sentinel occupies the first physical slot rejected by the logical guard. -/
theorem sentinel_index_rejected (map : α → β) (sentinel : β) (xs : List α) :
    ¬ canRead (storage map sentinel xs) xs.length := by
  rw [canRead_iff]
  exact Nat.lt_irrefl xs.length

/-- Mapping composition is independent of producer arity. -/
theorem map_composition
    (f : α → β) (g : β → γ) (xs : List α) :
    replay g (replay f xs) = replay (fun x => g (f x)) xs := by
  simp [replay]

end Producer
end CMeta
