import CMetaCFlowCalculus.CFlow.TimerEvent
import CMetaCFlowCalculus.CFlow.MachineRuntime
import CMetaCFlowCalculus.Proofs.Mailbox
import CMetaCFlowCalculus.Proofs.MachineRuntime

namespace CMetaCFlowCalculus.CFlow.TimerEvent

open CMetaCFlowCalculus.CFlow.Machine
open CMetaCFlowCalculus.CFlow.MachineRuntime

theorem removeId_sublist (timers : List Timer) (timerId : Nat) :
    List.Sublist (removeId timers timerId) timers := by
  induction timers with
  | nil => simp [removeId]
  | cons timer remaining inductionHypothesis =>
      by_cases idMatches : timer.id = timerId
      · simp [removeId, idMatches]
      · simp [removeId, idMatches, inductionHypothesis]

theorem removeId_length_le (timers : List Timer) (timerId : Nat) :
    (removeId timers timerId).length ≤ timers.length :=
  (removeId_sublist timers timerId).length_le

theorem mem_removeId_of_mem_id_ne {timers : List Timer} {timer : Timer}
    {timerId : Nat} (member : timer ∈ timers)
    (different : timer.id ≠ timerId) : timer ∈ removeId timers timerId := by
  induction timers with
  | nil => simp at member
  | cons head remaining inductionHypothesis =>
      simp only [List.mem_cons] at member
      rcases member with rfl | member
      · simp [removeId, different]
      · by_cases idMatches : head.id = timerId
        · simpa [removeId, idMatches] using member
        · simp [removeId, idMatches, inductionHypothesis member]

theorem earliest_eq_none_iff (timers : List Timer) :
    earliest timers = none ↔ timers = [] := by
  induction timers with
  | nil => simp [earliest]
  | cons timer remaining inductionHypothesis =>
      simp only [earliest]
      cases recursive : earliest remaining with
      | none => simp
      | some candidate =>
          by_cases isEarlier : earlier timer candidate = true <;>
            simp [isEarlier]

theorem earliest_mem {timers : List Timer} {selected : Timer}
    (selection : earliest timers = some selected) : selected ∈ timers := by
  induction timers generalizing selected with
  | nil => simp [earliest] at selection
  | cons timer remaining inductionHypothesis =>
      simp only [earliest] at selection
      cases recursive : earliest remaining with
      | none =>
          simp [recursive] at selection
          subst selected
          simp
      | some candidate =>
          by_cases isEarlier : earlier timer candidate
          · simp [recursive, isEarlier] at selection
            subst selected
            simp
          · simp [recursive, isEarlier] at selection
            subst selected
            exact List.mem_cons_of_mem timer
              (inductionHypothesis recursive)

theorem earlier_true_implies_keyLE {left right : Timer}
    (comparison : earlier left right = true) : KeyLE left right := by
  simp [earlier, KeyLE] at comparison ⊢
  omega

theorem earlier_false_implies_reverse_keyLE {left right : Timer}
    (comparison : earlier left right = false) : KeyLE right left := by
  simp [earlier, KeyLE] at comparison ⊢
  omega

theorem keyLE_trans {first second third : Timer} :
    KeyLE first second → KeyLE second third → KeyLE first third := by
  simp [KeyLE]
  omega

/-- `earliest` is a lexicographic deadline/order minimum for every nonempty
    pending set, not merely for a two-element example. -/
theorem earliest_is_lexicographic_minimum {timers : List Timer}
    {selected : Timer} (selection : earliest timers = some selected) :
    selected ∈ timers ∧
      ∀ candidate ∈ timers, KeyLE selected candidate := by
  induction timers generalizing selected with
  | nil => simp [earliest] at selection
  | cons timer remaining inductionHypothesis =>
      simp only [earliest] at selection
      cases recursive : earliest remaining with
      | none =>
          have empty : remaining = [] :=
            (earliest_eq_none_iff remaining).mp recursive
          subst remaining
          simp [earliest] at selection
          subst selected
          constructor
          · simp
          · intro other member
            have same : other = timer := by simpa using member
            subst other
            simp [KeyLE]
      | some candidate =>
          have remainingMinimum := inductionHypothesis recursive
          by_cases isEarlier : earlier timer candidate
          · simp [recursive, isEarlier] at selection
            subst selected
            refine ⟨by simp, ?_⟩
            intro other member
            have cases : other = timer ∨ other ∈ remaining := by
              simpa using member
            rcases cases with rfl | tailMember
            · simp [KeyLE]
            · exact keyLE_trans
                (earlier_true_implies_keyLE isEarlier)
                (remainingMinimum.2 other tailMember)
          · have comparison : earlier timer candidate = false := by
              simpa using isEarlier
            simp [recursive, isEarlier] at selection
            subst selected
            refine ⟨List.mem_cons_of_mem timer remainingMinimum.1, ?_⟩
            intro other member
            have cases : other = timer ∨ other ∈ remaining := by
              simpa using member
            rcases cases with rfl | tailMember
            · exact earlier_false_implies_reverse_keyLE comparison
            · exact remainingMinimum.2 other tailMember

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
  rcases valid with
    ⟨capacityPositive, bounded, idsUnique, ordersUnique, nextIdPositive,
      timerBounds, claimedMember⟩
  cases terminal : state.terminal with
  | closed =>
      simp only [schedule, terminal]
      exact ⟨capacityPositive, bounded, idsUnique, ordersUnique,
        nextIdPositive, timerBounds, claimedMember⟩
  | «open» =>
      by_cases hasCapacity : state.active.length < state.capacity
      · simp only [schedule, terminal, hasCapacity, ↓reduceIte]
        refine ⟨capacityPositive, ?_, ?_, ?_, Nat.zero_lt_succ _, ?_, ?_⟩
        · simpa using Nat.succ_le_of_lt hasCapacity
        · rw [List.map_append, List.nodup_append]
          refine ⟨idsUnique, by simp, ?_⟩
          intro old oldMember _ newMember
          simp only [List.map_cons, List.map_nil,
            List.mem_singleton] at newMember
          subst newMember
          have oldTimer : ∃ timer ∈ state.active, timer.id = old := by
            simpa using oldMember
          rcases oldTimer with ⟨timer, member, rfl⟩
          exact Nat.ne_of_lt (timerBounds timer member).2.1
        · rw [List.map_append, List.nodup_append]
          refine ⟨ordersUnique, by simp, ?_⟩
          intro old oldMember _ newMember
          simp only [List.map_cons, List.map_nil,
            List.mem_singleton] at newMember
          subst newMember
          have oldTimer : ∃ timer ∈ state.active, timer.order = old := by
            simpa using oldMember
          rcases oldTimer with ⟨timer, member, rfl⟩
          exact Nat.ne_of_lt (timerBounds timer member).2.2
        · intro timer member
          simp only [List.mem_append, List.mem_singleton] at member
          rcases member with oldMember | rfl
          · have bound := timerBounds timer oldMember
            exact ⟨bound.1, Nat.lt_succ_of_lt bound.2.1,
              Nat.lt_succ_of_lt bound.2.2⟩
          · exact ⟨Nat.ne_of_gt nextIdPositive, Nat.lt_succ_self _,
              Nat.lt_succ_self _⟩
        · intro timer claimed
          exact List.mem_append_left _ (claimedMember timer claimed)
      · simp only [schedule, terminal, hasCapacity, ↓reduceIte]
        exact ⟨capacityPositive, bounded, idsUnique, ordersUnique,
          nextIdPositive, timerBounds, claimedMember⟩

theorem claim_preserves_capacity (state : State) (now : Nat) :
    (claim state now).state.capacity = state.capacity := by
  cases terminal : state.terminal with
  | closed => simp [claim, terminal]
  | «open» =>
      cases claimed : state.claimed with
      | some timer => simp [claim, terminal, claimed]
      | none =>
          cases selected : earliest state.active with
          | none => simp [claim, terminal, claimed, selected]
          | some timer =>
              by_cases ready : timer.deadline ≤ now <;>
                simp [claim, terminal, claimed, selected, ready]

theorem claim_preserves_valid (state : State) (now : Nat)
    (valid : state.Valid) : (claim state now).state.Valid := by
  rcases valid with
    ⟨capacityPositive, bounded, idsUnique, ordersUnique, nextIdPositive,
      timerBounds, claimedMember⟩
  cases terminal : state.terminal with
  | closed =>
      simp only [claim, terminal]
      exact ⟨capacityPositive, bounded, idsUnique, ordersUnique,
        nextIdPositive, timerBounds, claimedMember⟩
  | «open» =>
      cases current : state.claimed with
      | some timer =>
          simp only [claim, terminal, current]
          exact ⟨capacityPositive, bounded, idsUnique, ordersUnique,
            nextIdPositive, timerBounds, claimedMember⟩
      | none =>
          cases selected : earliest state.active with
          | none =>
              simp only [claim, terminal, current, selected]
              exact ⟨capacityPositive, bounded, idsUnique, ordersUnique,
                nextIdPositive, timerBounds, claimedMember⟩
          | some timer =>
              by_cases ready : timer.deadline ≤ now
              · simp only [claim, terminal, current, selected, ready,
                  ↓reduceIte]
                refine ⟨capacityPositive, bounded, idsUnique, ordersUnique,
                  nextIdPositive, timerBounds, ?_⟩
                intro claimedTimer equality
                simp only [Option.some.injEq] at equality
                subst claimedTimer
                exact earliest_mem selected
              · simp only [claim, terminal, current, selected, ready,
                  ↓reduceIte]
                exact ⟨capacityPositive, bounded, idsUnique, ordersUnique,
                  nextIdPositive, timerBounds, claimedMember⟩

theorem removeId_preserves_valid (state : State) (timerId : Nat)
    (valid : state.Valid)
    (claimedSafe : ∀ timer, state.claimed = some timer →
      timer.id ≠ timerId) :
    ({ state with active := removeId state.active timerId } : State).Valid := by
  rcases valid with
    ⟨capacityPositive, bounded, idsUnique, ordersUnique, nextIdPositive,
      timerBounds, claimedMember⟩
  have sublist := removeId_sublist state.active timerId
  refine ⟨capacityPositive, Nat.le_trans sublist.length_le bounded,
    (sublist.map Timer.id).nodup idsUnique,
    (sublist.map Timer.order).nodup ordersUnique, nextIdPositive, ?_, ?_⟩
  · intro timer member
    exact timerBounds timer (sublist.subset member)
  · intro timer claimed
    exact mem_removeId_of_mem_id_ne
      (claimedMember timer claimed) (claimedSafe timer claimed)

theorem cancel_preserves_valid (state : State) (timerId : Nat)
    (valid : state.Valid) : (cancel state timerId).state.Valid := by
  cases terminal : state.terminal with
  | closed => simpa [cancel, terminal] using valid
  | «open» =>
      cases current : state.claimed with
      | none =>
          by_cases present : containsId state.active timerId
          · simp only [cancel, terminal, current, present, ↓reduceIte]
            have preserved := removeId_preserves_valid state timerId valid (by
              intro timer impossible
              simp [current] at impossible)
            simpa [current, terminal] using preserved
          · simp [cancel, terminal, current, present, valid]
      | some claimedTimer =>
          by_cases same : claimedTimer.id = timerId
          · simp [cancel, terminal, current, same, valid]
          · by_cases present : containsId state.active timerId
            · simp only [cancel, terminal, current, same, present, ↓reduceIte]
              have preserved := removeId_preserves_valid state timerId valid (by
                intro timer equality
                simp only [current, Option.some.injEq] at equality
                subst timer
                exact same)
              simpa [current, terminal] using preserved
            · simp [cancel, terminal, current, same, present, valid]

theorem commit_preserves_valid (state : State) (valid : state.Valid) :
    (commit state).state.Valid := by
  cases claimed : state.claimed with
  | none => simpa [commit, claimed] using valid
  | some timer =>
      rcases valid with
        ⟨capacityPositive, bounded, idsUnique, ordersUnique, nextIdPositive,
          timerBounds, claimedMember⟩
      have sublist := removeId_sublist state.active timer.id
      simp only [commit, claimed]
      refine ⟨capacityPositive, Nat.le_trans sublist.length_le bounded,
        (sublist.map Timer.id).nodup idsUnique,
        (sublist.map Timer.order).nodup ordersUnique, nextIdPositive, ?_, ?_⟩
      · intro activeTimer member
        exact timerBounds activeTimer (sublist.subset member)
      · simp

theorem close_preserves_valid (state : State) (valid : state.Valid) :
    (close state).state.Valid := by
  cases terminal : state.terminal with
  | closed => simpa [close, terminal] using valid
  | «open» =>
      rcases valid with
        ⟨capacityPositive, bounded, idsUnique, ordersUnique, nextIdPositive,
          timerBounds, claimedMember⟩
      cases claimed : state.claimed with
      | none =>
          simp [close, terminal, claimed, State.Valid, capacityPositive,
            nextIdPositive]
      | some timer =>
          have member := claimedMember timer claimed
          simp only [close, terminal, claimed, Option.toList]
          refine ⟨capacityPositive, by
            change 1 ≤ state.capacity
            omega, by simp,
            by simp,
            nextIdPositive, ?_, ?_⟩
          · intro activeTimer activeMember
            have same : activeTimer = timer := by simpa using activeMember
            subst activeTimer
            exact timerBounds timer member
          · intro claimedTimer equality
            have same : claimedTimer = timer := by
              simpa using equality.symm
            subst claimedTimer
            simp

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
      by_cases present : containsId state.active timerId <;>
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
        by_cases present : containsId state.active timerId <;>
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

/-- One successful Timer handoff is a single composed relation: the claimed
    Timer is committed to the Mailbox, that same Event is received once, and
    the Machine runtime consumes exactly that Event in its small step. -/
inductive MachineHandoff (machine : Machine) (guards : GuardValuation)
    (actions : ActionEvaluation) (timerState : State)
    (before after : Config) (timer : Timer)
    (mailboxAfter : Mailbox.State) : Prop where
  | delivered :
      timerState.claimed = some timer →
      (Mailbox.send timerState.mailbox timer.event).1 = .ok →
      Mailbox.receive (commit timerState).state.mailbox =
        (.received timer.event, mailboxAfter) →
      RuntimeStep machine guards actions before
        (.transition timer.event after) →
      MachineHandoff machine guards actions timerState before after timer
        mailboxAfter

/-- Timer commit, Mailbox receive, and Machine transition refine one connected
    trace: the Event is appended once, received once, and drives the exact
    Machine observation suffix. -/
theorem delivered_timer_handoff_refines_machine {machine : Machine}
    {guards : GuardValuation} {actions : ActionEvaluation}
    {timerState : State} {before after : Config} {timer : Timer}
    {mailboxAfter : Mailbox.State}
    (handoff : MachineHandoff machine guards actions timerState before after
      timer mailboxAfter) :
    (commit timerState).status = .delivered ∧
      (commit timerState).timer = some timer ∧
      (commit timerState).state.mailbox.queue =
        timerState.mailbox.queue ++ [timer.event] ∧
      Mailbox.receive (commit timerState).state.mailbox =
        (.received timer.event, mailboxAfter) ∧
      after.trace = before.trace ++ traceSuffix before after := by
  cases handoff with
  | delivered claimed accepted received runtime =>
      have committed := commit_claimed_delivery_appends_once
        timerState timer claimed accepted
      exact ⟨committed.1, by simp [commit, claimed], committed.2,
        received, runtime_step_trace_refines_machine runtime⟩

end CMetaCFlowCalculus.CFlow.TimerEvent
