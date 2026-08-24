import CMetaCFlowCalculus.CFlow.Actor
import CMetaCFlowCalculus.CFlow.MachineRuntime
import CMetaCFlowCalculus.Proofs.Mailbox
import CMetaCFlowCalculus.Proofs.MachineRuntime

namespace CMetaCFlowCalculus.CFlow.Actor

open CMetaCFlowCalculus.CFlow.Machine
open CMetaCFlowCalculus.CFlow.MachineRuntime

private theorem cancelAs_preserves_valid (state : State)
    (lifecycle : Lifecycle) (valid : state.Valid)
    (cancelled : lifecycle.expectedMailboxTerminal = .cancelled) :
    (cancelAs state lifecycle).Valid := by
  constructor
  · exact Mailbox.cancel_preserves_valid state.mailbox valid.1
  · simp [cancelAs, Mailbox.cancel, cancelled]

theorem initState_valid (mailbox : Mailbox.State)
    (valid : mailbox.Valid) (openMailbox : mailbox.terminal = .open) :
    (initState mailbox).Valid := by
  exact ⟨valid, openMailbox⟩

theorem start_preserves_valid (state : State) (succeeds : Bool)
    (valid : state.Valid) : (start state succeeds).2.Valid := by
  cases lifecycle : state.lifecycle with
  | start =>
      cases succeeds with
      | false =>
          simpa [start, lifecycle] using
            cancelAs_preserves_valid state .failed valid rfl
      | true =>
          have coherent : state.mailbox.terminal = .open := by
            simpa [Lifecycle.expectedMailboxTerminal, lifecycle] using valid.2
          have changedValid :
              ({ state with lifecycle := .running } : State).Valid :=
            ⟨valid.1, coherent⟩
          simpa [start, lifecycle] using changedValid
  | running => simpa [start, lifecycle] using valid
  | stopping => simpa [start, lifecycle] using valid
  | stopped => simpa [start, lifecycle] using valid
  | failed => simpa [start, lifecycle] using valid

theorem requestStop_preserves_valid (state : State) (valid : state.Valid) :
    (requestStop state).2.Valid := by
  cases lifecycle : state.lifecycle with
  | start =>
      simpa [requestStop, lifecycle] using
        cancelAs_preserves_valid state .stopped valid rfl
  | running =>
      simpa [requestStop, lifecycle] using
        cancelAs_preserves_valid state .stopping valid rfl
  | stopping => simpa [requestStop, lifecycle] using valid
  | stopped => simpa [requestStop, lifecycle] using valid
  | failed => simpa [requestStop, lifecycle] using valid

theorem requestStop_cancels_pending (state : State)
    (nonterminal : state.lifecycle = .start ∨ state.lifecycle = .running) :
    (requestStop state).2.mailbox.queue = [] ∧
      (requestStop state).2.mailbox.terminal = .cancelled := by
  rcases nonterminal with lifecycle | lifecycle <;>
    simp [requestStop, lifecycle, cancelAs, Mailbox.cancel]

theorem settle_preserves_valid (state : State) (valid : state.Valid) :
    (settle state).Valid := by
  cases lifecycle : state.lifecycle with
  | start => simpa [settle, lifecycle] using valid
  | running =>
      simpa [settle, lifecycle] using
        cancelAs_preserves_valid state .failed valid rfl
  | stopping =>
      have coherent : state.mailbox.terminal = .cancelled := by
        simpa [Lifecycle.expectedMailboxTerminal, lifecycle] using valid.2
      have changedValid :
          ({ state with lifecycle := .stopped } : State).Valid :=
        ⟨valid.1, coherent⟩
      simpa [settle, lifecycle] using changedValid
  | stopped => simpa [settle, lifecycle] using valid
  | failed => simpa [settle, lifecycle] using valid

theorem fail_preserves_valid (state : State) (valid : state.Valid) :
    (fail state).Valid := by
  cases lifecycle : state.lifecycle with
  | start =>
      simpa [fail, lifecycle] using
        cancelAs_preserves_valid state .failed valid rfl
  | running =>
      simpa [fail, lifecycle] using
        cancelAs_preserves_valid state .failed valid rfl
  | stopping =>
      simpa [fail, lifecycle] using
        cancelAs_preserves_valid state .failed valid rfl
  | stopped => simpa [fail, lifecycle] using valid
  | failed => simpa [fail, lifecycle] using valid

theorem fail_cancels_pending (state : State)
    (nonterminal : state.lifecycle = .start ∨
      state.lifecycle = .running ∨ state.lifecycle = .stopping) :
    (fail state).mailbox.queue = [] ∧
      (fail state).mailbox.terminal = .cancelled := by
  rcases nonterminal with lifecycle | lifecycle | lifecycle <;>
    simp [fail, lifecycle, cancelAs, Mailbox.cancel]

private theorem mailbox_send_preserves_terminal
    (mailbox : Mailbox.State) (event : Mailbox.TypedEvent) :
    (Mailbox.send mailbox event).2.terminal = mailbox.terminal := by
  cases lookup : mailbox.schema.lookup event.id with
  | none => simp [Mailbox.send, lookup]
  | some expected =>
      by_cases typeMatches : expected.payloadTy = event.payloadTy
      · cases terminal : mailbox.terminal with
        | «open» =>
            by_cases hasCapacity : mailbox.queue.length < mailbox.capacity <;>
              simp [Mailbox.send, lookup, typeMatches, terminal, hasCapacity]
        | draining =>
            simp [Mailbox.send, lookup, typeMatches, terminal]
        | cancelled =>
            simp [Mailbox.send, lookup, typeMatches, terminal]
      · simp [Mailbox.send, lookup, typeMatches]

private theorem mailbox_send_rejected_preserves_queue
    {before after : Mailbox.State} {event : Mailbox.TypedEvent}
    {status : Mailbox.Status}
    (transition : Mailbox.send before event = (status, after))
    (rejected : status ≠ .ok) : after.queue = before.queue := by
  cases lookup : before.schema.lookup event.id with
  | none =>
      simp [Mailbox.send, lookup] at transition
      rcases transition with ⟨rfl, rfl⟩
      rfl
  | some expected =>
      by_cases typeMatches : expected.payloadTy = event.payloadTy
      · cases terminal : before.terminal with
        | «open» =>
            by_cases hasCapacity : before.queue.length < before.capacity
            · simp [Mailbox.send, lookup, typeMatches, terminal,
                hasCapacity] at transition
              exact (rejected transition.1.symm).elim
            · simp [Mailbox.send, lookup, typeMatches, terminal,
                hasCapacity] at transition
              rcases transition with ⟨rfl, rfl⟩
              rfl
        | draining =>
            simp [Mailbox.send, lookup, typeMatches, terminal] at transition
            rcases transition with ⟨rfl, rfl⟩
            rfl
        | cancelled =>
            simp [Mailbox.send, lookup, typeMatches, terminal] at transition
            rcases transition with ⟨rfl, rfl⟩
            rfl
      · simp [Mailbox.send, lookup, typeMatches] at transition
        rcases transition with ⟨rfl, rfl⟩
        rfl

theorem send_preserves_valid (state : State) (event : Mailbox.TypedEvent)
    (valid : state.Valid) : (send state event).state.Valid := by
  by_cases live : state.live
  · cases lifecycle : state.lifecycle with
    | start => simpa [send, live, lifecycle] using valid
    | running =>
        constructor
        · simpa [send, live, lifecycle] using
            Mailbox.send_preserves_valid state.mailbox event valid.1
        · simpa [send, live, lifecycle] using
            (mailbox_send_preserves_terminal state.mailbox event).trans valid.2
    | stopping => simpa [send, live, lifecycle] using valid
    | stopped => simpa [send, live, lifecycle] using valid
    | failed => simpa [send, live, lifecycle] using valid
  · simpa [send, live] using valid

theorem send_accepted_only_running {before after : State}
    {event : Mailbox.TypedEvent}
    (transition : send before event = { status := .accepted, state := after }) :
    before.live = true ∧ before.lifecycle = .running := by
  by_cases live : before.live
  · cases lifecycle : before.lifecycle <;>
      simp [send, live, lifecycle] at transition ⊢
  · simp [send, live] at transition

theorem send_accepted_appends_once {before after : State}
    {event : Mailbox.TypedEvent}
    (transition : send before event = { status := .accepted, state := after }) :
    after.mailbox.queue = before.mailbox.queue ++ [event] := by
  by_cases live : before.live
  · cases lifecycle : before.lifecycle with
    | start => simp [send, live, lifecycle] at transition
    | stopping => simp [send, live, lifecycle] at transition
    | stopped => simp [send, live, lifecycle] at transition
    | failed => simp [send, live, lifecycle] at transition
    | running =>
        cases sent : Mailbox.send before.mailbox event with
        | mk status mailbox =>
            cases status <;>
              simp [send, live, lifecycle, sent, mapMailboxStatus] at transition
            subst after
            exact (Mailbox.send_ok_appends_once sent).1
  · simp [send, live] at transition

theorem send_rejected_preserves_queue {before after : State}
    {event : Mailbox.TypedEvent} {status : SendStatus}
    (transition : send before event = { status := status, state := after })
    (rejected : status ≠ .accepted) :
    after.mailbox.queue = before.mailbox.queue := by
  by_cases live : before.live
  · cases lifecycle : before.lifecycle with
    | start => simpa [send, live, lifecycle] using (congrArg (fun result =>
        result.state.mailbox.queue) transition).symm
    | stopping => simpa [send, live, lifecycle] using (congrArg (fun result =>
        result.state.mailbox.queue) transition).symm
    | stopped => simpa [send, live, lifecycle] using (congrArg (fun result =>
        result.state.mailbox.queue) transition).symm
    | failed => simpa [send, live, lifecycle] using (congrArg (fun result =>
        result.state.mailbox.queue) transition).symm
    | running =>
        cases sent : Mailbox.send before.mailbox event with
        | mk mailboxStatus mailbox =>
            have statusEq : mapMailboxStatus mailboxStatus = status := by
              simpa [send, live, lifecycle, sent] using
                congrArg SendResult.status transition
            have afterEq : { before with mailbox := mailbox } = after := by
              simpa [send, live, lifecycle, sent] using
                congrArg SendResult.state transition
            have mailboxRejected : mailboxStatus ≠ .ok := by
              intro accepted
              subst mailboxStatus
              exact rejected statusEq.symm
            rw [← afterEq]
            exact mailbox_send_rejected_preserves_queue sent mailboxRejected
  · simpa [send, live] using (congrArg (fun result =>
      result.state.mailbox.queue) transition).symm

theorem stopping_rejects_send (state : State) (event : Mailbox.TypedEvent)
    (live : state.live = true) (stopping : state.lifecycle = .stopping) :
    (send state event).status = .stopping := by
  simp [send, live, stopping]

theorem stopped_rejects_send (state : State) (event : Mailbox.TypedEvent)
    (live : state.live = true) (stopped : state.lifecycle = .stopped) :
    (send state event).status = .stopped := by
  simp [send, live, stopped]

theorem failed_rejects_send (state : State) (event : Mailbox.TypedEvent)
    (live : state.live = true) (failed : state.lifecycle = .failed) :
    (send state event).status = .failed := by
  simp [send, live, failed]

theorem stopped_cannot_restart (state : State) (succeeds : Bool)
    (stopped : state.lifecycle = .stopped) :
    start state succeeds = (.stopped, state) := by
  simp [start, stopped]

theorem failed_cannot_restart (state : State) (succeeds : Bool)
    (failed : state.lifecycle = .failed) :
    start state succeeds = (.failed, state) := by
  simp [start, failed]

theorem stopped_is_terminal (state : State) (succeeds : Bool)
    (stopped : state.lifecycle = .stopped) :
    (start state succeeds).2 = state ∧
      (requestStop state).2 = state ∧ settle state = state ∧ fail state = state := by
  simp [start, requestStop, settle, fail, stopped]

theorem failed_is_terminal (state : State) (succeeds : Bool)
    (failed : state.lifecycle = .failed) :
    (start state succeeds).2 = state ∧
      (requestStop state).2 = state ∧ settle state = state ∧ fail state = state := by
  simp [start, requestStop, settle, fail, failed]

/-- One Actor handoff is the existing bounded admission, FIFO receive, and
    existing Machine runtime step for the identical Event. -/
inductive MachineHandoff (machine : Machine) (guards : GuardValuation)
    (actions : ActionEvaluation) (actorBefore actorAfter : State)
    (before after : Config) (event : Mailbox.TypedEvent)
    (mailboxAfter : Mailbox.State) : Prop where
  | delivered :
      send actorBefore event = { status := .accepted, state := actorAfter } →
      Mailbox.receive actorAfter.mailbox =
        (.received event, mailboxAfter) →
      RuntimeStep machine guards actions before
        (.transition event after) →
      MachineHandoff machine guards actions actorBefore actorAfter before after
        event mailboxAfter

/-- Actor admission and Mailbox receive connect to the exact Machine event and
    its committed observation suffix; no Actor-specific transition is added. -/
theorem accepted_handoff_refines_machine {machine : Machine}
    {guards : GuardValuation} {actions : ActionEvaluation}
    {actorBefore actorAfter : State} {before after : Config}
    {event : Mailbox.TypedEvent} {mailboxAfter : Mailbox.State}
    (handoff : MachineHandoff machine guards actions actorBefore actorAfter
      before after event mailboxAfter) :
    send actorBefore event = { status := .accepted, state := actorAfter } ∧
      actorBefore.live = true ∧
      actorBefore.lifecycle = .running ∧
      actorAfter.mailbox.queue = actorBefore.mailbox.queue ++ [event] ∧
      Mailbox.receive actorAfter.mailbox =
        (.received event, mailboxAfter) ∧
      after.trace = before.trace ++ traceSuffix before after := by
  cases handoff with
  | delivered accepted received runtime =>
      have admission := send_accepted_only_running accepted
      exact ⟨accepted, admission.1, admission.2,
        send_accepted_appends_once accepted, received,
        runtime_step_trace_refines_machine runtime⟩

end CMetaCFlowCalculus.CFlow.Actor
