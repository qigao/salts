import CMetaCFlowCalculus.CFlow.Mailbox

namespace CMetaCFlowCalculus.CFlow.Mailbox

theorem send_preserves_valid (state : State) (event : TypedEvent)
    (valid : state.Valid) : (send state event).2.Valid := by
  rcases valid with ⟨capacityPositive, bounded⟩
  cases lookup : state.schema.lookup event.id with
  | none =>
      simp [send, lookup, State.Valid, capacityPositive, bounded]
  | some expected =>
      by_cases typeMatches : expected.payloadTy = event.payloadTy
      · cases terminal : state.terminal with
        | «open» =>
            by_cases hasCapacity : state.queue.length < state.capacity
            · simp [send, lookup, typeMatches, terminal, hasCapacity,
                State.Valid, capacityPositive, Nat.succ_le_of_lt hasCapacity]
            · simp [send, lookup, typeMatches, terminal, hasCapacity,
                State.Valid, capacityPositive, bounded]
        | draining =>
            simp [send, lookup, typeMatches, terminal, State.Valid,
              capacityPositive, bounded]
        | cancelled =>
            simp [send, lookup, typeMatches, terminal, State.Valid,
              capacityPositive, bounded]
      · simp [send, lookup, typeMatches, State.Valid,
          capacityPositive, bounded]

/-- Admission classification never changes the Mailbox terminal mode. -/
theorem send_preserves_terminal (state : State) (event : TypedEvent) :
    (send state event).2.terminal = state.terminal := by
  cases lookup : state.schema.lookup event.id with
  | none => simp [send, lookup]
  | some expected =>
      by_cases typeMatches : expected.payloadTy = event.payloadTy
      · cases terminal : state.terminal with
        | «open» =>
            by_cases hasCapacity : state.queue.length < state.capacity <;>
              simp [send, lookup, typeMatches, terminal, hasCapacity]
        | draining => simp [send, lookup, typeMatches, terminal]
        | cancelled => simp [send, lookup, typeMatches, terminal]
      · simp [send, lookup, typeMatches]

/-- Every non-OK send leaves the bounded FIFO queue unchanged. -/
theorem send_rejected_preserves_queue {before after : State}
    {event : TypedEvent} {status : Status}
    (transition : send before event = (status, after))
    (rejected : status ≠ .ok) : after.queue = before.queue := by
  cases lookup : before.schema.lookup event.id with
  | none =>
      simp [send, lookup] at transition
      rcases transition with ⟨rfl, rfl⟩
      rfl
  | some expected =>
      by_cases typeMatches : expected.payloadTy = event.payloadTy
      · cases terminal : before.terminal with
        | «open» =>
            by_cases hasCapacity : before.queue.length < before.capacity
            · simp [send, lookup, typeMatches, terminal,
                hasCapacity] at transition
              exact (rejected transition.1.symm).elim
            · simp [send, lookup, typeMatches, terminal,
                hasCapacity] at transition
              rcases transition with ⟨rfl, rfl⟩
              rfl
        | draining =>
            simp [send, lookup, typeMatches, terminal] at transition
            rcases transition with ⟨rfl, rfl⟩
            rfl
        | cancelled =>
            simp [send, lookup, typeMatches, terminal] at transition
            rcases transition with ⟨rfl, rfl⟩
            rfl
      · simp [send, lookup, typeMatches] at transition
        rcases transition with ⟨rfl, rfl⟩
        rfl

/-- Successful admission appends the event exactly once and changes no bound. -/
theorem send_ok_appends_once {before after : State} {event : TypedEvent}
    (transition : send before event = (.ok, after)) :
    after.queue = before.queue ++ [event] ∧
      after.capacity = before.capacity ∧ after.schema = before.schema := by
  cases lookup : before.schema.lookup event.id with
  | none => simp [send, lookup] at transition
  | some expected =>
      by_cases typeMatches : expected.payloadTy = event.payloadTy
      · cases terminal : before.terminal with
        | «open» =>
            by_cases hasCapacity : before.queue.length < before.capacity
            · simp [send, lookup, typeMatches, terminal, hasCapacity] at transition
              subst after
              simp
            · simp [send, lookup, typeMatches, terminal, hasCapacity] at transition
        | draining => simp [send, lookup, typeMatches, terminal] at transition
        | cancelled => simp [send, lookup, typeMatches, terminal] at transition
      · simp [send, lookup, typeMatches] at transition

/-- The single consumer observes the oldest accepted event first. -/
theorem receive_fifo {state : State} {event : TypedEvent}
    {remaining : List TypedEvent} (openState : state.terminal = .open)
    (queued : state.queue = event :: remaining) :
    receive state =
      (.received event, { state with queue := remaining }) := by
  simp [receive, openState, queued]

theorem receive_preserves_valid (state : State) (valid : state.Valid) :
    (receive state).2.Valid := by
  rcases valid with ⟨capacityPositive, bounded⟩
  cases terminal : state.terminal with
  | cancelled =>
      simp [receive, terminal, State.Valid, capacityPositive, bounded]
  | «open» =>
      cases queue : state.queue with
      | nil => simp [receive, terminal, queue, State.Valid,
          capacityPositive]
      | cons event remaining =>
          have consBound : remaining.length.succ ≤ state.capacity := by
            simpa [queue] using bounded
          have remainingBound : remaining.length ≤ state.capacity := by
            exact Nat.le_trans (Nat.le_succ remaining.length) consBound
          simp [receive, terminal, queue, State.Valid,
            capacityPositive, remainingBound]
  | draining =>
      cases queue : state.queue with
      | nil => simp [receive, terminal, queue, State.Valid,
          capacityPositive]
      | cons event remaining =>
          have consBound : remaining.length.succ ≤ state.capacity := by
            simpa [queue] using bounded
          have remainingBound : remaining.length ≤ state.capacity := by
            exact Nat.le_trans (Nat.le_succ remaining.length) consBound
          simp [receive, terminal, queue, State.Valid,
            capacityPositive, remainingBound]

/-- Graceful close changes only terminal mode and retains the FIFO queue. -/
theorem close_preserves_queue (state : State) :
    (close state).2.queue = state.queue := by
  cases terminal : state.terminal <;> simp [close, terminal]

/-- Once an open mailbox closes, every otherwise valid send is rejected. -/
theorem close_rejects_send (state : State) (event : TypedEvent)
    (openState : state.terminal = .open)
    (known : state.schema.lookup event.id = some
      { id := event.id, payloadTy := event.payloadTy }) :
    (send (close state).2 event).1 = .closed := by
  simp [close, send, openState, known]

/-- Cancellation clears the queue and enters the unique cancelled mode. -/
theorem cancel_empties_queue (state : State) :
    (cancel state).2.queue = [] ∧
      (cancel state).2.terminal = .cancelled := by
  simp [cancel]

theorem cancel_preserves_valid (state : State) (valid : state.Valid) :
    (cancel state).2.Valid := by
  exact ⟨valid.1, Nat.zero_le state.capacity⟩

end CMetaCFlowCalculus.CFlow.Mailbox
