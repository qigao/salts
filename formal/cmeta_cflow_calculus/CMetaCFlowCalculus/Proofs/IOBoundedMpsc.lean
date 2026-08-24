import CMetaCFlowCalculus.IO.BoundedMpsc

namespace CMetaCFlowCalculus.IO.BoundedMpsc

theorem tryPublish_preserves_valid (state : State α) (value : α)
    (valid : state.Valid) : (tryPublish state value).2.Valid := by
  rcases valid with ⟨capacityPositive, bounded⟩
  cases terminal : state.terminal with
  | draining =>
      simp [tryPublish, terminal, State.Valid, capacityPositive, bounded]
  | «open» =>
      by_cases hasCapacity : state.queue.length < state.capacity
      · simp [tryPublish, terminal, hasCapacity, State.Valid,
          capacityPositive, Nat.succ_le_of_lt hasCapacity]
      · simp [tryPublish, terminal, hasCapacity, State.Valid,
          capacityPositive, bounded]

theorem tryConsume_preserves_valid (state : State α)
    (valid : state.Valid) : (tryConsume state).2.Valid := by
  rcases valid with ⟨capacityPositive, bounded⟩
  cases queue : state.queue with
  | nil =>
      cases terminal : state.terminal <;>
        simp [tryConsume, queue, terminal, State.Valid, capacityPositive]
  | cons value remaining =>
      have remainingBounded : remaining.length ≤ state.capacity := by
        rw [queue] at bounded
        simp only [List.length_cons] at bounded
        exact Nat.le_trans (Nat.le_succ remaining.length) bounded
      simp [tryConsume, queue, State.Valid, capacityPositive, remainingBounded]

theorem accepted_appends_once {before after : State α} {value : α}
    (transition : tryPublish before value = (.accepted, after)) :
    after.queue = before.queue ++ [value] ∧
      after.capacity = before.capacity := by
  cases terminal : before.terminal with
  | draining => simp [tryPublish, terminal] at transition
  | «open» =>
      by_cases hasCapacity : before.queue.length < before.capacity
      · simp [tryPublish, terminal, hasCapacity] at transition
        subst after
        simp
      · simp [tryPublish, terminal, hasCapacity] at transition

theorem rejected_publish_unchanged (state : State α) (value : α)
    (rejected : (tryPublish state value).1 ≠ .accepted) :
    (tryPublish state value).2 = state := by
  cases terminal : state.terminal with
  | draining => simp [tryPublish, terminal]
  | «open» =>
      by_cases hasCapacity : state.queue.length < state.capacity
      · simp [tryPublish, terminal, hasCapacity] at rejected
      · simp [tryPublish, terminal, hasCapacity]

theorem consume_observes_fifo_head (state : State α) (value : α)
    (remaining : List α) (queue : state.queue = value :: remaining) :
    (tryConsume state).1 = .item value ∧
      (tryConsume state).2.queue = remaining := by
  simp [tryConsume, queue]

theorem close_preserves_queue (state : State α) :
    (close state).2.queue = state.queue := by
  cases terminal : state.terminal <;> simp [close, terminal]

theorem close_preserves_valid (state : State α) (valid : state.Valid) :
    (close state).2.Valid := by
  cases terminal : state.terminal <;>
    simpa [close, terminal, State.Valid] using valid

theorem close_rejects_publish (state : State α) (value : α) :
    (tryPublish (close state).2 value).1 = .closed := by
  cases terminal : state.terminal <;>
    simp [close, tryPublish, terminal]

end CMetaCFlowCalculus.IO.BoundedMpsc
