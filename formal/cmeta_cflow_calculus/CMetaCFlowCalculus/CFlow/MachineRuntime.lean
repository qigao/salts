import CMetaCFlowCalculus.CFlow.Machine

namespace CMetaCFlowCalculus.CFlow.MachineRuntime

open CMetaCFlowCalculus.CMeta
open CMetaCFlowCalculus.CFlow.Mailbox
open CMetaCFlowCalculus.CFlow.Machine

inductive StepKind where
  | value
  | valueAndDone
  | wait
  | done
  | error
  deriving Repr, DecidableEq

def ActionValueCompatible (outputTy : Ty) (action : ActionDecl) : Prop :=
  match action.observation with
  | .value valueTy => valueTy = outputTy
  | .none | .event _ _ => True

/-- The C runtime fragment has one downstream VALUE type. State and Event
    types remain transition-specific and are admitted separately by CMeta ABI
    checks in the executable implementation. -/
def SupportedFragment (machine : Machine) (outputTy : Ty) : Prop :=
  ∀ action ∈ machine.actions, ActionValueCompatible outputTy action

def traceSuffix (before after : Config) : List MachineObservation :=
  after.trace.drop before.trace.length

def MachineObservation.isValue : MachineObservation → Bool
  | .value _ _ => true
  | _ => false

def projectedStepKind (before after : Config) : StepKind :=
  match after.terminal with
  | .error _ => .error
  | .done =>
      if (traceSuffix before after).any MachineObservation.isValue then
        .valueAndDone
      else .done
  | .running =>
      if (traceSuffix before after).any MachineObservation.isValue then
        .value
      else .wait

inductive RuntimeOutcome where
  | wait
  | transition (event : TypedEvent) (after : Config)

/-- One runtime action either observes an empty mailbox (WAIT), or refines one
    existing Machine small step. The trace premise makes the executable
    adapter's committed suffix explicit and excludes partial publication. -/
inductive RuntimeStep (machine : Machine) (guards : GuardValuation)
    (actions : ActionEvaluation) (before : Config) : RuntimeOutcome → Prop where
  | wait : RuntimeStep machine guards actions before .wait
  | transition {event : TypedEvent} {after : Config} :
      SmallStep machine guards actions before event after →
      after.trace = before.trace ++ traceSuffix before after →
      RuntimeStep machine guards actions before (.transition event after)

inductive RuntimeTrace (machine : Machine) (guards : GuardValuation)
    (actions : ActionEvaluation) :
    Config → List TypedEvent → Config → List MachineObservation → Prop where
  | nil (config : Config) : RuntimeTrace machine guards actions config [] config []
  | cons {before middle after : Config} {event : TypedEvent}
      {events : List TypedEvent} {remaining : List MachineObservation} :
      RuntimeStep machine guards actions before (.transition event middle) →
      RuntimeTrace machine guards actions middle events after remaining →
      RuntimeTrace machine guards actions before (event :: events) after
        (traceSuffix before middle ++ remaining)

end CMetaCFlowCalculus.CFlow.MachineRuntime
