import CMetaCFlowCalculus.CFlow.TimerEvent
import CMetaCFlowCalculus.CFlow.MachineRuntime
import CMetaCFlowCalculus.Proofs.Mailbox
import CMetaCFlowCalculus.Proofs.MachineRuntime

namespace CMetaCFlowCalculus.CFlow.TimerEvent

open CMetaCFlowCalculus.CFlow.Machine
open CMetaCFlowCalculus.CFlow.MachineRuntime

theorem removeId_length_le (timers : List Timer) (timerId : Nat) :
    (removeId timers timerId).length ≤ timers.length := by
  induction timers with
  | nil => simp [removeId]
  | cons timer remaining inductionHypothesis =>
      by_cases idMatches : timer.id = timerId
      · simp [removeId, idMatches]
      · simp [removeId, idMatches, inductionHypothesis]

theorem earlier_same_deadline_preserves_order {left right : Timer}
    (sameDeadline : left.deadline = right.deadline)
    (scheduledFirst : left.order < right.order) :
    earlier left right = true := by
  simp [earlier, sameDeadline, scheduledFirst]

theorem earliest_pair_same_deadline_is_fifo {left right : Timer}
    (sameDeadline : left.deadline = right.deadline)
    (scheduledFirst : left.order < right.order) :
    earliest [left, right] = some left := by
  simp [earliest,
    earlier_same_deadline_preserves_order sameDeadline scheduledFirst]

theorem schedule_preserves_valid (state : State) (deadline : Nat)
    (event : Mailbox.TypedEvent) (valid : state.Valid) :
    (schedule state deadline event).state.Valid := by
  rcases valid with ⟨capacityPositive, bounded⟩
  cases terminal : state.terminal with
  | closed =>
      simp [schedule, terminal, State.Valid, capacityPositive, bounded]
  | «open» =>
      by_cases hasCapacity :
          state.pending.length + state.claimed.toList.length < state.capacity
      · have pendingLt : state.pending.length < state.capacity :=
          Nat.lt_of_le_of_lt (Nat.le_add_right state.pending.length _) hasCapacity
        simp [schedule, terminal, hasCapacity, State.Valid, capacityPositive,
          Nat.succ_le_of_lt pendingLt]
      · simp [schedule, terminal, hasCapacity, State.Valid,
          capacityPositive, bounded]

theorem claim_preserves_pending_bound (state : State) (now : Nat)
    (valid : state.Valid) :
    (claim state now).state.pending.length ≤ state.capacity := by
  rcases valid with ⟨_, bounded⟩
  cases terminal : state.terminal with
  | closed => simp [claim, terminal, bounded]
  | «open» =>
      cases claimed : state.claimed with
      | some timer => simp [claim, terminal, claimed, bounded]
      | none =>
          cases selected : earliest state.pending with
          | none => simp [claim, terminal, claimed, selected, bounded]
          | some timer =>
              by_cases ready : timer.deadline ≤ now
              · simp [claim, terminal, claimed, selected, ready]
                exact Nat.le_trans
                  (removeId_length_le state.pending timer.id) bounded
              · simp [claim, terminal, claimed, selected, ready, bounded]

theorem claim_preserves_capacity (state : State) (now : Nat) :
    (claim state now).state.capacity = state.capacity := by
  cases terminal : state.terminal with
  | closed => simp [claim, terminal]
  | «open» =>
      cases claimed : state.claimed with
      | some timer => simp [claim, terminal, claimed]
      | none =>
          cases selected : earliest state.pending with
          | none => simp [claim, terminal, claimed, selected]
          | some timer =>
              by_cases ready : timer.deadline ≤ now <;>
                simp [claim, terminal, claimed, selected, ready]

theorem claim_preserves_valid (state : State) (now : Nat)
    (valid : state.Valid) : (claim state now).state.Valid := by
  constructor
  · rw [claim_preserves_capacity]
    exact valid.1
  · rw [claim_preserves_capacity]
    exact claim_preserves_pending_bound state now valid

theorem claimed_fire_beats_cancel (state : State) (timer : Timer)
    (openState : state.terminal = .open)
    (claimed : state.claimed = some timer) :
    (cancel state timer.id).status = .fireWon ∧
      (cancel state timer.id).state = state := by
  simp [cancel, openState, claimed]

theorem close_rejects_future_claim (state : State) (now : Nat)
    (openState : state.terminal = .open) :
    (claim (close state).state now).status = .closed := by
  simp [close, claim, openState]

theorem commit_without_claim_preserves_mailbox (state : State)
    (unclaimed : state.claimed = none) :
    (commit state).status = .invalidArgument ∧
      (commit state).state.mailbox = state.mailbox := by
  simp [commit, unclaimed]

theorem cancel_unclaimed_preserves_unclaimed (state : State) (timerId : Nat)
    (unclaimed : state.claimed = none) :
    (cancel state timerId).state.claimed = none := by
  cases terminal : state.terminal with
  | closed => simp [cancel, terminal, unclaimed]
  | «open» =>
      by_cases present : containsId state.pending timerId <;>
        simp [cancel, terminal, unclaimed, present]

theorem cancel_before_fire_prevents_delivery (state : State) (timerId : Nat)
    (unclaimed : state.claimed = none) :
    (commit (cancel state timerId).state).status = .invalidArgument ∧
      (commit (cancel state timerId).state).state.mailbox = state.mailbox := by
  have remainsUnclaimed :=
    cancel_unclaimed_preserves_unclaimed state timerId unclaimed
  have preserved :=
    commit_without_claim_preserves_mailbox
      (cancel state timerId).state remainsUnclaimed
  exact ⟨preserved.1, preserved.2.trans (by
    cases terminal : state.terminal with
    | closed => simp [cancel, terminal]
    | «open» =>
        by_cases present : containsId state.pending timerId <;>
          simp [cancel, terminal, unclaimed, present])⟩

theorem commit_clears_claim (state : State) :
    (commit state).state.claimed = none := by
  cases claimed : state.claimed <;> simp [commit, claimed]

theorem commit_preserves_mailbox_valid (state : State)
    (valid : state.mailbox.Valid) :
    (commit state).state.mailbox.Valid := by
  cases claimed : state.claimed with
  | none => simpa [commit, claimed] using valid
  | some timer =>
      simpa [commit, claimed] using
        Mailbox.send_preserves_valid state.mailbox timer.event valid

theorem commit_claimed_delivery_appends_once (state : State) (timer : Timer)
    (claimed : state.claimed = some timer)
    (accepted : (Mailbox.send state.mailbox timer.event).1 = .ok) :
    (commit state).status = .delivered ∧
      (commit state).state.mailbox.queue =
        state.mailbox.queue ++ [timer.event] := by
  have transition : Mailbox.send state.mailbox timer.event =
      (.ok, (Mailbox.send state.mailbox timer.event).2) := by
    apply Prod.ext
    · exact accepted
    · rfl
  have appended := (Mailbox.send_ok_appends_once transition).1
  constructor
  · simp [commit, claimed, accepted]
  · simpa [commit, claimed] using appended

/-- A delivered Timer Event enters the existing Machine runtime relation; the
    resulting observation suffix is therefore exactly a Machine small step. -/
theorem delivered_timer_runtime_step_refines_machine {machine : Machine}
    {guards : GuardValuation} {actions : ActionEvaluation}
    {timerState : State} {timer : Timer} {before after : Config}
    (claimed : timerState.claimed = some timer)
    (delivered : (commit timerState).status = .delivered)
    (runtime : RuntimeStep machine guards actions before
      (.transition timer.event after)) :
    after.trace = before.trace ++ traceSuffix before after ∧
      (commit timerState).timer = some timer ∧
      (commit timerState).status = .delivered := by
  refine ⟨runtime_step_trace_refines_machine runtime, ?_, delivered⟩
  simp [commit, claimed]

end CMetaCFlowCalculus.CFlow.TimerEvent
