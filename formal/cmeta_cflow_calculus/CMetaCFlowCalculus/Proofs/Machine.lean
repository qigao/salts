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

theorem terminal_state_no_step {machine : Machine} {guards : GuardValuation}
    {actions : ActionEvaluation} {before : Config} {event : Mailbox.TypedEvent}
    {state : StateDecl}
    (stateLookup : lookupState machine.states before.state = some state)
    (terminal : state.kind ≠ .active) :
    step machine guards actions before event = none := by
  cases terminalState : before.terminal with
  | done => simp [step, terminalState]
  | error message => simp [step, terminalState]
  | running =>
      cases stateKind : state.kind with
      | active => exact False.elim (terminal stateKind)
      | done => simp [step, terminalState, stateLookup, stateKind]
      | error => simp [step, terminalState, stateLookup, stateKind]

theorem actionOutput_trace_is_user_owned {output : ActionOutput} :
    ∀ observation ∈ output.trace, observation.FromAction := by
  cases output <;> simp [ActionOutput.trace, MachineObservation.FromAction]

theorem initConfig_wellTyped {machine : Machine} {value : TypedValue}
    {config : Config} (initialized : initConfig machine value = some config) :
    config.WellTyped machine := by
  unfold initConfig at initialized
  cases stateLookup : lookupState machine.states machine.initial with
  | none => simp [stateLookup] at initialized
  | some state =>
      have stateId : state.id = machine.initial :=
        lookupState_some_id stateLookup
      by_cases valueTyped : state.valueTy = value.ty
      · cases stateKind : state.kind <;>
          simp [stateLookup, valueTyped, stateKind] at initialized <;>
          subst config <;>
          refine ⟨state, ?_⟩ <;>
          simp [stateId, stateLookup, valueTyped, stateKind, Terminal.Coherent]
      · simp [stateLookup, valueTyped] at initialized

private theorem failureConfig_wellTyped {machine : Machine}
    {before : Config} {message : String}
    (beforeTyped : before.WellTyped machine)
    (running : before.terminal = .running) :
    (failureConfig before message).WellTyped machine := by
  rcases beforeTyped with ⟨state, stateLookup, valueTyped, coherent⟩
  have active : state.kind = .active := by
    simpa [Terminal.Coherent, running] using coherent
  refine ⟨state, ?_⟩
  simp [failureConfig, stateLookup, valueTyped,
    Terminal.Coherent, active]

private theorem commitTarget_consumes_once {machine : Machine}
    {before : Config} {transition : Transition} {value : TypedValue}
    {output : ActionOutput} :
    (commitTarget machine before transition value output).consumedEvents =
      before.consumedEvents + 1 := by
  unfold commitTarget
  split
  · simp [failureConfig]
  · split
    · split <;> simp
    · simp [failureConfig]

private theorem applyTransition_consumes_once {machine : Machine}
    {actions : ActionEvaluation} {before : Config} {transition : Transition} :
    (applyTransition machine actions before transition).consumedEvents =
      before.consumedEvents + 1 := by
  unfold applyTransition
  split
  · exact commitTarget_consumes_once
  · split
    · simp [failureConfig]
    · split
      · split <;> simp [failureConfig]
      · split
        · exact commitTarget_consumes_once
        · simp [failureConfig]

theorem step_consumes_once {machine : Machine}
    {guards : GuardValuation} {actions : ActionEvaluation}
    {before after : Config} {event : Mailbox.TypedEvent}
    (transition : SmallStep machine guards actions before event after) :
    after.consumedEvents = before.consumedEvents + 1 := by
  unfold SmallStep at transition
  cases terminalState : before.terminal with
  | done => simp [step, terminalState] at transition
  | error message => simp [step, terminalState] at transition
  | running =>
      cases stateLookup : lookupState machine.states before.state with
      | none => simp [step, terminalState, stateLookup] at transition
      | some state =>
          cases stateKind : state.kind with
          | done => simp [step, terminalState, stateLookup, stateKind] at transition
          | error => simp [step, terminalState, stateLookup, stateKind] at transition
          | active =>
              cases eventLookup : machine.events.lookup event.id with
              | none =>
                  simp [step, terminalState, stateLookup, stateKind,
                    eventLookup] at transition
              | some expected =>
                  by_cases typeMatches : expected.payloadTy = event.payloadTy
                  · cases selection : selectTransition machine before.state
                        event.id guards with
                    | none =>
                        simp [step, terminalState, stateLookup, stateKind,
                          eventLookup, typeMatches, selection] at transition
                        subst after
                        simp [failureConfig]
                    | some selected =>
                        simp [step, terminalState, stateLookup, stateKind,
                          eventLookup, typeMatches, selection] at transition
                        subst after
                        exact applyTransition_consumes_once
                  · simp [step, terminalState, stateLookup, stateKind,
                      eventLookup, typeMatches] at transition

theorem applyTransition_action_failure {machine : Machine}
    {actions : ActionEvaluation} {before : Config}
    {transition : Transition} {action : ActionDecl} {message : String}
    (nonzero : transition.action ≠ 0)
    (actionLookup : lookupAction machine.actions transition.action = some action)
    (mayFail : action.mayFail = true)
    (failure : actions transition.action = .error message) :
    let after := applyTransition machine actions before transition
    after.state = before.state ∧ after.stateValue = before.stateValue ∧
      after.terminal = .error message := by
  simp [applyTransition, nonzero, actionLookup, mayFail, failure, failureConfig]

private theorem commitTarget_preserves_wellTyped
    {machine : Machine} {before : Config} {transition : Transition}
    {value : TypedValue} {output : ActionOutput} {target : StateDecl}
    (targetLookup : lookupState machine.states transition.target = some target)
    (beforeTyped : before.WellTyped machine)
    (running : before.terminal = .running) :
    (commitTarget machine before transition value output).WellTyped machine := by
  have targetId : target.id = transition.target :=
    lookupState_some_id targetLookup
  by_cases valueTyped : target.valueTy = value.ty
  · cases targetKind : target.kind with
    | active =>
        refine ⟨target, ?_⟩
        simp [commitTarget, targetLookup, valueTyped,
          targetId, targetKind, Terminal.Coherent, running]
    | done =>
        refine ⟨target, ?_⟩
        simp [commitTarget, targetLookup, valueTyped,
          targetId, targetKind, Terminal.Coherent]
    | error =>
        refine ⟨target, ?_⟩
        simp [commitTarget, targetLookup, valueTyped,
          targetId, targetKind, Terminal.Coherent]
  · simpa [commitTarget, targetLookup, valueTyped] using
      failureConfig_wellTyped beforeTyped running

private theorem applyTransition_preserves_wellTyped
    {machine : Machine} {actions : ActionEvaluation}
    {before : Config} {transition : Transition}
    (valid : machine.Valid) (member : transition ∈ machine.transitions)
    (beforeTyped : before.WellTyped machine)
    (running : before.terminal = .running) :
    (applyTransition machine actions before transition).WellTyped machine := by
  rcases valid.targetKnown transition member with ⟨target, targetLookup⟩
  by_cases identity : transition.action = 0
  · simp only [applyTransition, if_pos identity]
    exact commitTarget_preserves_wellTyped targetLookup beforeTyped running
  · simp only [applyTransition, if_neg identity]
    cases actionLookup : lookupAction machine.actions transition.action with
    | none => exact failureConfig_wellTyped beforeTyped running
    | some action =>
        cases actionResult : actions transition.action with
        | error message =>
            by_cases mayFail : action.mayFail = true
            · simp [mayFail]
              exact failureConfig_wellTyped beforeTyped running
            · simp [mayFail]
              exact failureConfig_wellTyped beforeTyped running
        | success value output =>
            by_cases resultTyped :
                value.ty = action.targetTy ∧ output.matches action.observation
            · simp [resultTyped]
              exact commitTarget_preserves_wellTyped targetLookup beforeTyped running
            · simp [resultTyped]
              exact failureConfig_wellTyped beforeTyped running

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
      cases stateLookup : lookupState machine.states before.state with
      | none => simp [step, terminalState, stateLookup] at transition
      | some state =>
          cases stateKind : state.kind with
          | done => simp [step, terminalState, stateLookup, stateKind] at transition
          | error => simp [step, terminalState, stateLookup, stateKind] at transition
          | active =>
              cases eventLookup : machine.events.lookup event.id with
              | none =>
                  simp [step, terminalState, stateLookup, stateKind,
                    eventLookup] at transition
              | some expected =>
                  by_cases typeMatches : expected.payloadTy = event.payloadTy
                  · exact ⟨expected, eventLookup, typeMatches⟩
                  · simp [step, terminalState, stateLookup, stateKind,
                      eventLookup, typeMatches] at transition

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
      cases stateLookup : lookupState machine.states before.state with
      | none => simp [step, terminalState, stateLookup] at transition
      | some state =>
          cases stateKind : state.kind with
          | done => simp [step, terminalState, stateLookup, stateKind] at transition
          | error => simp [step, terminalState, stateLookup, stateKind] at transition
          | active =>
              cases eventLookup : machine.events.lookup event.id with
              | none =>
                  simp [step, terminalState, stateLookup, stateKind,
                    eventLookup] at transition
              | some expected =>
                  by_cases typeMatches : expected.payloadTy = event.payloadTy
                  · cases selection : selectTransition machine before.state
                        event.id guards with
                    | none =>
                        simp [step, terminalState, stateLookup, stateKind,
                          eventLookup, typeMatches, selection] at transition
                        subst after
                        exact failureConfig_wellTyped beforeTyped terminalState
                    | some selected =>
                        have member : selected ∈ machine.transitions :=
                          selectTransition_mem selection
                        simp [step, terminalState, stateLookup, stateKind,
                          eventLookup, typeMatches, selection] at transition
                        subst after
                        exact applyTransition_preserves_wellTyped valid member
                          beforeTyped terminalState
                  · simp [step, terminalState, stateLookup, stateKind,
                      eventLookup, typeMatches] at transition

end CMetaCFlowCalculus.CFlow.Machine
