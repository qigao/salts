import CMetaCFlowCalculus.CFlow.MachineRuntime

namespace CMetaCFlowCalculus.CFlow.MachineRuntime

open CMetaCFlowCalculus.CFlow.Machine

theorem runtime_wait_preserves_config {machine : Machine}
    {guards : GuardValuation} {actions : ActionEvaluation} {before : Config}
    (_step : RuntimeStep machine guards actions before .wait) :
    before.trace = before.trace :=
  rfl

theorem runtime_step_trace_refines_machine {machine : Machine}
    {guards : GuardValuation} {actions : ActionEvaluation}
    {before after : Config} {event : Mailbox.TypedEvent}
    (runtime : RuntimeStep machine guards actions before
      (.transition event after)) :
    after.trace = before.trace ++ traceSuffix before after := by
  cases runtime with
  | transition _ refinement => exact refinement

theorem runtime_trace_refines_machine {machine : Machine}
    {guards : GuardValuation} {actions : ActionEvaluation}
    {before after : Config} {events : List Mailbox.TypedEvent}
    {trace : List MachineObservation}
    (runtime : RuntimeTrace machine guards actions before events after trace) :
    after.trace = before.trace ++ trace := by
  induction runtime with
  | nil => simp
  | cons first remaining inductionHypothesis =>
      rw [inductionHypothesis,
          runtime_step_trace_refines_machine first]
      simp [List.append_assoc]

end CMetaCFlowCalculus.CFlow.MachineRuntime
