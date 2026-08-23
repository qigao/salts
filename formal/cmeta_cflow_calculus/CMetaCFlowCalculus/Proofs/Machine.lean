import CMetaCFlowCalculus.CFlow.Machine

namespace CMetaCFlowCalculus.CFlow.Machine

theorem smallStep_deterministic {machine : Machine}
    {guards : GuardValuation} {actions : ActionEvaluation}
    {before : Config} {event : Mailbox.TypedEvent} {first second : Config}
    (firstStep : SmallStep machine guards actions before event first)
    (secondStep : SmallStep machine guards actions before event second) :
    first = second := by
  unfold SmallStep at firstStep secondStep
  rw [firstStep] at secondStep
  exact Option.some.inj secondStep

theorem terminal_no_step {machine : Machine} {guards : GuardValuation}
    {actions : ActionEvaluation} {before : Config} {event : Mailbox.TypedEvent}
    (terminal : before.terminal ≠ .running) :
    step machine guards actions before event = none := by
  cases terminalState : before.terminal with
  | running => exact False.elim (terminal terminalState)
  | done => simp [step, terminalState]
  | error message => simp [step, terminalState]

theorem step_consumes_once {machine : Machine}
    {guards : GuardValuation} {actions : ActionEvaluation}
    {before after : Config} {event : Mailbox.TypedEvent}
    (transition : SmallStep machine guards actions before event after) :
    after.consumedEvents = before.consumedEvents + 1 := by
  unfold SmallStep step at transition
  split at transition <;> try contradiction
  split at transition <;> try contradiction
  split at transition <;> try contradiction
  split at transition <;> try contradiction
  · simp only [Option.some.injEq] at transition
    subst after
    simp [failureConfig]
  · simp only [Option.some.injEq] at transition
    subst after
    simp only [applyTransition]
    split
    · simp [failureConfig]
    · unfold commitTarget
      split
      · simp [failureConfig]
      · split <;> simp

theorem applyTransition_action_failure {machine : Machine}
    {actions : ActionEvaluation} {before : Config}
    {transition : Transition} {message : String}
    (nonzero : transition.action ≠ 0)
    (failure : actions transition.action = .error message) :
    let after := applyTransition machine actions before transition
    after.state = before.state ∧ after.stateToken = before.stateToken ∧
      after.terminal = .error message := by
  simp [applyTransition, nonzero, failure, failureConfig]

private theorem applyTransition_preserves_state_typing
    {machine : Machine} {actions : ActionEvaluation}
    {before : Config} {transition : Transition}
    (valid : machine.Valid) (member : transition ∈ machine.transitions)
    (beforeTyped : before.WellTyped machine) :
    (applyTransition machine actions before transition).WellTyped machine := by
  rcases valid.targetKnown transition member with ⟨target, targetLookup⟩
  simp only [applyTransition]
  split
  · simpa [failureConfig, Config.WellTyped] using beforeTyped
  · have targetId : target.id = transition.target :=
      lookupState_some_id targetLookup
    have targetKnown : StateKnown machine target.id := by
      rw [targetId]
      exact ⟨target, targetLookup⟩
    cases targetKind : target.kind <;>
      simpa [commitTarget, targetLookup, targetKind, Config.WellTyped]
        using targetKnown

theorem smallStep_requires_event_typing {machine : Machine}
    {guards : GuardValuation} {actions : ActionEvaluation}
    {before after : Config} {event : Mailbox.TypedEvent}
    (transition : SmallStep machine guards actions before event after) :
    EventTyped machine event := by
  unfold SmallStep at transition
  cases terminalState : before.terminal with
  | done => simp [step, terminalState] at transition
  | error message => simp [step, terminalState] at transition
  | running =>
      cases eventLookup : machine.events.lookup event.id with
      | none => simp [step, terminalState, eventLookup] at transition
      | some expected =>
          by_cases typeMatches : expected.payloadTy = event.payloadTy
          · exact ⟨expected, eventLookup, typeMatches⟩
          · simp [step, terminalState, eventLookup, typeMatches] at transition

theorem step_preserves_state_typing {machine : Machine}
    {guards : GuardValuation} {actions : ActionEvaluation}
    {before after : Config} {event : Mailbox.TypedEvent}
    (valid : machine.Valid) (beforeTyped : before.WellTyped machine)
    (_eventTyped : EventTyped machine event)
    (transition : SmallStep machine guards actions before event after) :
    after.WellTyped machine := by
  unfold SmallStep at transition
  cases terminalState : before.terminal with
  | done => simp [step, terminalState] at transition
  | error message => simp [step, terminalState] at transition
  | running =>
      cases eventLookup : machine.events.lookup event.id with
      | none => simp [step, terminalState, eventLookup] at transition
      | some expected =>
          by_cases typeMatches : expected.payloadTy = event.payloadTy
          · cases selection :
                selectTransition machine before.state event.id guards with
            | none =>
                simp [step, terminalState, eventLookup, typeMatches,
                  selection] at transition
                subst after
                simpa [Config.WellTyped, failureConfig] using beforeTyped
            | some selected =>
                have member : selected ∈ machine.transitions :=
                  selectTransition_mem selection
                have typed := applyTransition_preserves_state_typing
                  valid member beforeTyped (actions := actions)
                simp [step, terminalState, eventLookup, typeMatches,
                  selection] at transition
                subst after
                exact typed
          · simp [step, terminalState, eventLookup, typeMatches] at transition

end CMetaCFlowCalculus.CFlow.Machine
