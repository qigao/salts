import CMetaCFlowCalculus.CMeta.Ownership

namespace CMetaCFlowCalculus.CFlow

open CMetaCFlowCalculus.CMeta

/--
The complete source continuation represented by the calculus. `liveRoots`
is not a caller-supplied side table: it is the captured-value component of
the continuation itself.
-/
structure SourceState where
  cursor : Nat
  liveRoots : List PackedValue

/-- Source-provided armable object identity. -/
structure Waitable where
  id : Nat
  deriving Repr, DecidableEq

/-- Kernel-issued registration identity. Generations are never reused in a run. -/
structure WaitToken where
  generation : Nat
  deriving Repr, DecidableEq

/-- Downstream-value demand. Phase B follows the v1 specification's `Nat`. -/
inductive Demand where
  | finite (remaining : Nat)
  deriving Repr, DecidableEq

namespace Demand

/-- Consume exactly one downstream-value request, failing at zero. -/
def consume : Demand → Option Demand
  | .finite 0 => none
  | .finite (remaining + 1) => some (.finite remaining)

/-- A source may be polled only when at least one request is available. -/
def Available (demand : Demand) : Prop :=
  ∃ remaining, demand.consume = some remaining

end Demand

/-- Registration is explicit so WAIT cannot be confused with an armed wait. -/
inductive WaitState where
  | ready
  | pendingArm (waitable : Waitable)
  | suspended (waitable : Waitable) (token : WaitToken)
  deriving Repr, DecidableEq

/-- The only terminal fact source for one run. -/
inductive Terminal where
  | running
  | done
  | error (message : String)
  | cancelled
  deriving Repr, DecidableEq

/-- Source completion is distinct from whole-run terminal completion. -/
inductive SourceTerminal where
  | active
  | done
  | error (message : String)
  deriving Repr, DecidableEq

/-- Downstream work may continue after the source has completed. -/
inductive DrainState where
  | accepting
  | draining
  | drained
  deriving Repr, DecidableEq

/-- The races covered by the abstract wait-registration contract. -/
inductive ArmTiming where
  | noSignal
  | signalBeforeArm
  | signalConcurrentWithArm
  deriving Repr, DecidableEq

/--
Runtime state `Σ`. Ownership, demand, wait registration, source/drain status,
and terminal status each have one authoritative representation. Suspension
roots belong to `SourceState`, the continuation fact source.
-/
structure RuntimeState where
  ownership : OwnershipContext
  demand : Demand
  wait : WaitState
  nextWakeGeneration : Nat
  sourceTerminal : SourceTerminal
  drain : DrainState
  terminal : Terminal

namespace RuntimeState

def nextWakeToken (runtime : RuntimeState) : WaitToken where
  generation := runtime.nextWakeGeneration

end RuntimeState

/-- A source continuation paired with source/output types and runtime state. -/
structure Config (sourceTy outputTy : Ty) where
  source : SourceState
  runtime : RuntimeState

/-- The five source outcomes from section 8.1 of the v1 specification. -/
inductive SourceResult (ty : Ty) where
  | value (output : Value ty) (next : SourceState)
  | valueAndDone (output : Value ty)
  | wait (waitable : Waitable) (next : SourceState)
  | done
  | error (message : String)

namespace SourceResult

/-- Whether a source result produces an input for downstream processing. -/
def ProducesValue {ty : Ty} : SourceResult ty → Prop
  | .value _ _ => True
  | .valueAndDone _ => True
  | .wait _ _ => False
  | .done => False
  | .error _ => False

end SourceResult

/--
One source small-step. Polling for VALUE, VALUE_AND_DONE, or WAIT requires
positive demand but does not consume it; downstream `EmitStep` consumes it.
Terminal source results are not demand-gated.
-/
inductive SourceStep {sourceTy outputTy : Ty} :
    Config sourceTy outputTy → SourceResult sourceTy →
      Config sourceTy outputTy → Prop where
  | value {before : Config sourceTy outputTy} {output : Value sourceTy}
      {next : SourceState}
      (running : before.runtime.terminal = .running)
      (sourceOpen : before.runtime.sourceTerminal = .active)
      (ready : before.runtime.wait = .ready)
      (available : before.runtime.demand.Available)
      (readable : ContextReadable before.runtime.ownership output) :
      SourceStep before (.value output next)
        { before with
          source := next }
  | valueAndDone {before : Config sourceTy outputTy}
      {output : Value sourceTy}
      (running : before.runtime.terminal = .running)
      (sourceOpen : before.runtime.sourceTerminal = .active)
      (ready : before.runtime.wait = .ready)
      (available : before.runtime.demand.Available)
      (readable : ContextReadable before.runtime.ownership output) :
      SourceStep before (.valueAndDone output)
        { before with
          runtime := {
            before.runtime with
            sourceTerminal := .done
            drain := .draining
          } }
  | wait {before : Config sourceTy outputTy}
      {waitable : Waitable} {next : SourceState}
      (running : before.runtime.terminal = .running)
      (sourceOpen : before.runtime.sourceTerminal = .active)
      (ready : before.runtime.wait = .ready)
      (available : before.runtime.demand.Available)
      (safe : SuspendSafe before.runtime.ownership next.liveRoots) :
      SourceStep before (.wait waitable next)
        { before with
          source := next
          runtime := { before.runtime with wait := .pendingArm waitable } }
  | done {before : Config sourceTy outputTy}
      (running : before.runtime.terminal = .running)
      (sourceOpen : before.runtime.sourceTerminal = .active)
      (ready : before.runtime.wait = .ready) :
      SourceStep before .done
        { before with
          runtime := {
            before.runtime with
            sourceTerminal := .done
            drain := .draining
          } }
  | error {before : Config sourceTy outputTy} {message : String}
      (running : before.runtime.terminal = .running)
      (sourceOpen : before.runtime.sourceTerminal = .active)
      (ready : before.runtime.wait = .ready) :
      SourceStep before (.error message)
        { before with
          runtime := {
            before.runtime with
            sourceTerminal := .error message
            terminal := .error message
          } }

/--
Arming is lossless by construction: a signal observed before or concurrently
with registration returns directly to READY instead of entering SUSPENDED.
-/
inductive ArmStep {sourceTy outputTy : Ty} :
    Config sourceTy outputTy → Waitable → WaitToken → ArmTiming →
      Config sourceTy outputTy → Prop where
  | quiet {before : Config sourceTy outputTy} {waitable : Waitable}
      (running : before.runtime.terminal = .running)
      (pending : before.runtime.wait = .pendingArm waitable) :
      ArmStep before waitable before.runtime.nextWakeToken .noSignal
        { before with
          runtime := {
            before.runtime with
            wait := .suspended waitable before.runtime.nextWakeToken
            nextWakeGeneration := before.runtime.nextWakeGeneration + 1
          } }
  | signaledBefore {before : Config sourceTy outputTy} {waitable : Waitable}
      (running : before.runtime.terminal = .running)
      (pending : before.runtime.wait = .pendingArm waitable) :
      ArmStep before waitable before.runtime.nextWakeToken .signalBeforeArm
        { before with
          runtime := {
            before.runtime with
            wait := .ready
            nextWakeGeneration := before.runtime.nextWakeGeneration + 1
          } }
  | signaledConcurrent {before : Config sourceTy outputTy}
      {waitable : Waitable}
      (running : before.runtime.terminal = .running)
      (pending : before.runtime.wait = .pendingArm waitable) :
      ArmStep before waitable before.runtime.nextWakeToken .signalConcurrentWithArm
        { before with
          runtime := {
            before.runtime with
            wait := .ready
            nextWakeGeneration := before.runtime.nextWakeGeneration + 1
          } }

/-- Only the token stored by a quiet arm can resume a suspended run. -/
inductive WakeStep {sourceTy outputTy : Ty} :
    Config sourceTy outputTy → WaitToken → Config sourceTy outputTy → Prop where
  | wake {before : Config sourceTy outputTy} {token : WaitToken}
      {waitable : Waitable}
      (running : before.runtime.terminal = .running)
      (suspended : before.runtime.wait = .suspended waitable token) :
      WakeStep before token
        { before with runtime := { before.runtime with wait := .ready } }

/-- Cancellation clears any registration and establishes the terminal fact. -/
inductive CancelStep {sourceTy outputTy : Ty} :
    Config sourceTy outputTy → Config sourceTy outputTy → Prop where
  | cancel {before : Config sourceTy outputTy}
      (running : before.runtime.terminal = .running) :
      CancelStep before
        { before with
          runtime := {
            before.runtime with
            wait := .ready
            terminal := .cancelled
          } }

/-- A downstream Sink emission, distinct from consuming a source value. -/
inductive EmitStep {sourceTy outputTy : Ty} :
    Config sourceTy outputTy → Value outputTy →
      Config sourceTy outputTy → Prop where
  | emit {before : Config sourceTy outputTy} {output : Value outputTy}
      {remaining : Demand}
      (running : before.runtime.terminal = .running)
      (ready : before.runtime.wait = .ready)
      (notDrained : before.runtime.drain ≠ .drained)
      (readable : ContextReadable before.runtime.ownership output)
      (consume : before.runtime.demand.consume = some remaining) :
      EmitStep before output
        { before with runtime := { before.runtime with demand := remaining } }

/-- Abstract completion of downstream work after source termination. -/
inductive DrainStep {sourceTy outputTy : Ty} :
    Config sourceTy outputTy → Config sourceTy outputTy → Prop where
  | complete {before : Config sourceTy outputTy}
      (running : before.runtime.terminal = .running)
      (ready : before.runtime.wait = .ready)
      (sourceDone : before.runtime.sourceTerminal = .done)
      (draining : before.runtime.drain = .draining) :
      DrainStep before
        { before with runtime := { before.runtime with drain := .drained } }

/-- Whole-run DONE is legal only after downstream drain completion. -/
inductive FinishStep {sourceTy outputTy : Ty} :
    Config sourceTy outputTy → Config sourceTy outputTy → Prop where
  | done {before : Config sourceTy outputTy}
      (running : before.runtime.terminal = .running)
      (ready : before.runtime.wait = .ready)
      (sourceDone : before.runtime.sourceTerminal = .done)
      (drained : before.runtime.drain = .drained) :
      FinishStep before
        { before with runtime := { before.runtime with terminal := .done } }

/-- Kernel-level labels for the unified small-step relation. -/
inductive Event (sourceTy outputTy : Ty) where
  | source (result : SourceResult sourceTy)
  | arm (waitable : Waitable) (token : WaitToken) (timing : ArmTiming)
  | wake (token : WaitToken)
  | emit (output : Value outputTy)
  | drain
  | finish
  | cancel

namespace Event

def EmitsValue {sourceTy outputTy : Ty} : Event sourceTy outputTy → Prop
  | .source _ => False
  | .arm _ _ _ => False
  | .wake _ => False
  | .emit _ => True
  | .drain => False
  | .finish => False
  | .cancel => False

end Event

/-- Source, registration, wake, and cancellation steps under one relation. -/
inductive Step {sourceTy outputTy : Ty} :
    Config sourceTy outputTy → Event sourceTy outputTy →
      Config sourceTy outputTy → Prop where
  | source {before after : Config sourceTy outputTy}
      {result : SourceResult sourceTy}
      (step : SourceStep before result after) :
      Step before (.source result) after
  | arm {before after : Config sourceTy outputTy} {waitable : Waitable}
      {token : WaitToken} {timing : ArmTiming}
      (step : ArmStep before waitable token timing after) :
      Step before (.arm waitable token timing) after
  | wake {before after : Config sourceTy outputTy} {token : WaitToken}
      (step : WakeStep before token after) :
      Step before (.wake token) after
  | emit {before after : Config sourceTy outputTy} {output : Value outputTy}
      (step : EmitStep before output after) :
      Step before (.emit output) after
  | drain {before after : Config sourceTy outputTy}
      (step : DrainStep before after) :
      Step before .drain after
  | finish {before after : Config sourceTy outputTy}
      (step : FinishStep before after) :
      Step before .finish after
  | cancel {before after : Config sourceTy outputTy}
      (step : CancelStep before after) :
      Step before .cancel after

end CMetaCFlowCalculus.CFlow
