import CMetaCFlowCalculus.CFlow.Machine
import CMetaCFlowCalculus.Proofs.Machine

namespace CMetaCFlowCalculus.Tests.Machine

open CMetaCFlowCalculus.CMeta
open CMetaCFlowCalculus.CFlow.Mailbox
open CMetaCFlowCalculus.CFlow.Machine

def intTy : Ty := .named "Int"
def longTy : Ty := .named "Long"
def triggerTy : Ty := .named "Trigger"

def trigger : TypedEvent :=
  { id := 100, payloadTy := triggerTy, payload := { token := 41 } }

def machine : Machine := {
  states := [
    { id := 10, valueTy := intTy, kind := .active },
    { id := 20, valueTy := longTy, kind := .done },
    { id := 30, valueTy := intTy, kind := .active }]
  initial := 10
  events := [{ id := 100, payloadTy := triggerTy }]
  guards := [
    { id := 1, stateTy := intTy, eventId := 100, eventTy := triggerTy,
      contract := { pureEffect := true, deterministic := true,
                    total := true, noAlias := true } },
    { id := 2, stateTy := intTy, eventId := 100, eventTy := triggerTy,
      contract := { pureEffect := true, deterministic := true,
                    total := true, noAlias := true } }]
  actions := [
    { id := 1, sourceTy := intTy, eventId := 100, eventTy := triggerTy,
      targetTy := longTy, deterministic := true, noAlias := true,
      mayFail := false, observation := .value longTy },
    { id := 2, sourceTy := intTy, eventId := 100, eventTy := triggerTy,
      targetTy := intTy, deterministic := true, noAlias := true,
      mayFail := true, observation := .none }]
  transitions := [
    { source := 10, event := 100, guard := 1, action := 1,
      target := 20, priority := 9 },
    { source := 10, event := 100, guard := 2, action := 2,
      target := 30, priority := 3 }]
}

def initialConfig : Config := {
  state := 10
  stateToken := 7
  terminal := .running
  trace := []
  consumedEvents := 0
}

def bothEnabled : GuardValuation := fun _ => true
def onlyFirstEnabled : GuardValuation := fun id => id = 1

def successfulActions : ActionEvaluation
  | 1 => .success 8 [.value longTy 70]
  | _ => .success 9 []

def failingSecond : ActionEvaluation
  | 2 => .error "boom"
  | _ => .success 8 []

example : step machine bothEnabled successfulActions initialConfig trigger =
    some { initialConfig with
      state := 30
      stateToken := 9
      trace := [.state 30]
      consumedEvents := 1 } := by native_decide

example : step machine onlyFirstEnabled successfulActions initialConfig trigger =
    some { initialConfig with
      state := 20
      stateToken := 8
      terminal := .done
      trace := [.value longTy 70, .state 20, .done]
      consumedEvents := 1 } := by native_decide

example : step machine (fun _ => false) successfulActions initialConfig trigger =
    some { initialConfig with
      terminal := .error "no enabled transition"
      trace := [.error "no enabled transition"]
      consumedEvents := 1 } := by native_decide

example : step machine bothEnabled failingSecond initialConfig trigger =
    some { initialConfig with
      terminal := .error "boom"
      trace := [.error "boom"]
      consumedEvents := 1 } := by native_decide

example : step machine bothEnabled successfulActions
    { initialConfig with terminal := .done } trigger = none := by native_decide

example {after : Config}
    (transition : SmallStep machine bothEnabled successfulActions
      initialConfig trigger after) :
    after.consumedEvents = initialConfig.consumedEvents + 1 :=
  step_consumes_once transition

example {after1 after2 : Config}
    (first : SmallStep machine bothEnabled successfulActions
      initialConfig trigger after1)
    (second : SmallStep machine bothEnabled successfulActions
      initialConfig trigger after2) : after1 = after2 :=
  smallStep_deterministic first second

example (terminal : initialConfig.terminal ≠ .running) :
    step machine bothEnabled successfulActions initialConfig trigger = none :=
  terminal_no_step terminal

example {candidate : Machine} {guards : GuardValuation}
    {actions : ActionEvaluation} {before after : Config}
    {event : TypedEvent}
    (valid : candidate.Valid)
    (beforeTyped : before.WellTyped candidate)
    (eventTyped : EventTyped candidate event)
    (transition : SmallStep candidate guards actions before event after) :
    after.WellTyped candidate :=
  step_preserves_state_typing valid beforeTyped eventTyped transition

example {candidate : Machine} {guards : GuardValuation}
    {actions : ActionEvaluation} {before after : Config}
    {event : TypedEvent}
    (transition : SmallStep candidate guards actions before event after) :
    EventTyped candidate event :=
  smallStep_requires_event_typing transition

example {candidate : Machine} {actions : ActionEvaluation}
    {before : Config} {transition : Transition} {message : String}
    (nonzero : transition.action ≠ 0)
    (failure : actions transition.action = .error message) :
    let after := applyTransition candidate actions before transition
    after.state = before.state ∧ after.stateToken = before.stateToken ∧
      after.terminal = .error message :=
  applyTransition_action_failure nonzero failure

end CMetaCFlowCalculus.Tests.Machine
