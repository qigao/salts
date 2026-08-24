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

/-- Control-plane lifecycle is independent from executor scheduling state.
    Close preserves an already executing turn; cancel competes with its final
    commit decision. -/
inductive ControlLifecycle where
  | open
  | closeRequested
  | cancelRequested
  | terminal
  deriving Repr, DecidableEq

/-- Worker scheduling and commit admission form a second orthogonal state
    dimension. `committing` is the transition linearization point. -/
inductive WorkerPhase where
  | idle
  | scheduled
  | executing
  | committing
  deriving Repr, DecidableEq

/-- Abstract commit-arbitration state. `source` remains authoritative while an
    action writes `staged`; exactly one of commit or cancellation settlement
    consumes that staged result. -/
structure CommitControl where
  lifecycle : ControlLifecycle
  worker : WorkerPhase
  source : Config
  staged : Option Config
  completed : Nat
  cancelled : Nat
  deriving Repr, DecidableEq

namespace CommitControl

def executing (source target : Config) : CommitControl :=
  { lifecycle := .open
    worker := .executing
    source := source
    staged := some target
    completed := 0
    cancelled := 0 }

def Valid (control : CommitControl) : Prop :=
  match control.worker with
  | .idle | .scheduled => control.staged = none
  | .executing | .committing => ∃ target, control.staged = some target

def requestClose (control : CommitControl) : CommitControl :=
  match control.lifecycle with
  | .open => { control with lifecycle := .closeRequested }
  | .closeRequested | .cancelRequested | .terminal => control

def requestCancel (control : CommitControl) : CommitControl :=
  match control.lifecycle with
  | .terminal => control
  | .open | .closeRequested | .cancelRequested =>
      { control with lifecycle := .cancelRequested }

/-- The mutex-protected decision. Cancellation requested before this function
    wins; otherwise changing the worker to `committing` linearizes commit. -/
def beginCommit (control : CommitControl) : Option CommitControl :=
  match control.worker, control.lifecycle, control.staged with
  | .executing, .open, some _
  | .executing, .closeRequested, some _ =>
      some { control with worker := .committing }
  | _, _, _ => none

/-- Consume one staged result after commit has linearized. A cancellation that
    overlaps after `beginCommit` terminates subsequent work but cannot revoke
    this one commit. -/
def commit (control : CommitControl) : Option CommitControl :=
  match control.worker, control.lifecycle, control.staged with
  | .committing, .open, some target =>
      some { control with
        worker := .idle
        source := target
        staged := none
        completed := control.completed + 1 }
  | .committing, .closeRequested, some target
  | .committing, .cancelRequested, some target =>
      some { control with
        lifecycle := .terminal
        worker := .idle
        source := target
        staged := none
        completed := control.completed + 1 }
  | _, _, _ => none

/-- Cancellation settlement consumes the staged result without changing the
    authoritative source configuration. -/
def discardCancelled (control : CommitControl) : Option CommitControl :=
  match control.worker, control.lifecycle, control.staged with
  | .executing, .cancelRequested, some _ =>
      some { control with
        lifecycle := .terminal
        worker := .idle
        staged := none
        cancelled := control.cancelled + 1 }
  | _, _, _ => none

/- The legacy relation intentionally models the missing final cancellation
   recheck: it commits any staged result after a prior last check. -/
namespace Legacy

def unconditionalCommit (control : CommitControl) : CommitControl :=
  match control.staged with
  | none => control
  | some target =>
      { control with
        worker := .idle
        source := target
        staged := none
        completed := control.completed + 1 }

end Legacy

end CommitControl

end CMetaCFlowCalculus.CFlow.MachineRuntime
