import CMetaCFlowCalculus.CFlow.Execution
import CMetaCFlowCalculus.Proofs.Execution

open CMetaCFlowCalculus.CMeta
open CMetaCFlowCalculus.CFlow

namespace CMetaCFlowCalculus.Tests.PhaseB

def scalarTy : Ty := .named "Scalar"

example : (Demand.finite 0).consume = none := rfl
example : (Demand.finite 2).consume = some (.finite 1) := rfl
example : SourceResult scalarTy := .done
example : Terminal := .cancelled

variable {before after : Config scalarTy scalarTy}
  {event : Event scalarTy scalarTy}

example (step : Step before event after) (emits : event.EmitsValue) :
    ∃ n, before.runtime.demand = .finite (n + 1) ∧
      after.runtime.demand = .finite n :=
  step_value_decrements_demand step emits

example (terminal : before.runtime.terminal ≠ .running) :
    ¬Step before event after :=
  terminal_no_step terminal

example (zero : before.runtime.demand = .finite 0)
    (step : Step before event after) :
    ¬event.EmitsValue :=
  zero_demand_no_value zero step

def liveValue : Value scalarTy where
  token := 7

def ownedContext : OwnershipContext := fun candidate =>
  if candidate = liveValue.token then
    some { ty := scalarTy, ownership := .owned }
  else none

theorem ownedLiveSafe : SuspendSafe ownedContext [liveValue.pack] := by
  intro value member
  simp only [List.mem_cons, List.not_mem_nil, or_false] at member
  subst value
  simp [ownedContext, liveValue, Value.pack]

theorem liveValueOwned : HasOwnership ownedContext liveValue .owned := by
  rfl

theorem liveValueReadable : ContextReadable ownedContext liveValue :=
  ⟨.owned, liveValueOwned, .owned⟩

def readyRuntime : RuntimeState where
  ownership := ownedContext
  demand := .finite 1
  wait := .ready
  nextWakeGeneration := 0
  sourceTerminal := .active
  drain := .accepting
  terminal := .running

def initialSource : SourceState where
  cursor := 3
  liveRoots := []

def continuedSource : SourceState where
  cursor := 4
  liveRoots := [liveValue.pack]

def readyConfig : Config scalarTy scalarTy where
  source := initialSource
  runtime := readyRuntime

def reusableWaitable : Waitable where
  id := 11

def otherWaitable : Waitable where
  id := 12

def firstToken : WaitToken where
  generation := 0

def wrongToken : WaitToken where
  generation := 12

/- Source production and downstream emission are deliberately separate. -/

def afterSourceValueConfig : Config scalarTy scalarTy :=
  { readyConfig with source := continuedSource }

theorem sourceValueWitness :
    SourceStep readyConfig (.value liveValue continuedSource)
      afterSourceValueConfig := by
  apply SourceStep.value
  · rfl
  · rfl
  · rfl
  · exact ⟨.finite 0, rfl⟩
  · exact liveValueReadable

theorem sourceKernelWitness :
    Step readyConfig (.source (.value liveValue continuedSource))
      afterSourceValueConfig :=
  .source sourceValueWitness

example : afterSourceValueConfig.runtime.demand =
    readyConfig.runtime.demand :=
  source_value_preserves_demand sourceValueWitness

example : ¬(Event.source (.value liveValue continuedSource) :
    Event scalarTy scalarTy).EmitsValue := by simp [Event.EmitsValue]

def afterEmitConfig : Config scalarTy scalarTy :=
  { afterSourceValueConfig with
    runtime := { afterSourceValueConfig.runtime with demand := .finite 0 } }

theorem emitWitness : EmitStep afterSourceValueConfig liveValue afterEmitConfig := by
  apply EmitStep.emit
  · rfl
  · rfl
  · decide
  · exact liveValueReadable
  · rfl

theorem kernelEmitWitness :
    Step afterSourceValueConfig (.emit liveValue) afterEmitConfig :=
  .emit emitWitness

example : ∃ n, afterSourceValueConfig.runtime.demand = .finite (n + 1) ∧
    afterEmitConfig.runtime.demand = .finite n :=
  step_value_decrements_demand kernelEmitWitness (by trivial)

/- VALUE_AND_DONE begins drain; pending downstream output may still emit. -/

def valueDoneConfig : Config scalarTy scalarTy :=
  { readyConfig with
    runtime := {
      readyConfig.runtime with
      sourceTerminal := .done
      drain := .draining
    } }

theorem valueAndDoneWitness :
    SourceStep readyConfig (.valueAndDone liveValue) valueDoneConfig := by
  apply SourceStep.valueAndDone
  · rfl
  · rfl
  · rfl
  · exact ⟨.finite 0, rfl⟩
  · exact liveValueReadable

example : valueDoneConfig.runtime.demand = readyConfig.runtime.demand ∧
    valueDoneConfig.runtime.sourceTerminal = .done ∧
    valueDoneConfig.runtime.drain = .draining ∧
    valueDoneConfig.runtime.terminal = .running :=
  source_value_and_done_starts_drain valueAndDoneWitness

def valueDoneAfterEmitConfig : Config scalarTy scalarTy :=
  { valueDoneConfig with
    runtime := { valueDoneConfig.runtime with demand := .finite 0 } }

theorem valueDoneEmitWitness :
    EmitStep valueDoneConfig liveValue valueDoneAfterEmitConfig := by
  apply EmitStep.emit
  · rfl
  · rfl
  · decide
  · exact liveValueReadable
  · rfl

def drainedAfterFinalValueConfig : Config scalarTy scalarTy :=
  { valueDoneAfterEmitConfig with
    runtime := { valueDoneAfterEmitConfig.runtime with drain := .drained } }

theorem drainAfterFinalValueWitness :
    DrainStep valueDoneAfterEmitConfig drainedAfterFinalValueConfig := by
  apply DrainStep.complete <;> rfl

def finishedAfterFinalValueConfig : Config scalarTy scalarTy :=
  { drainedAfterFinalValueConfig with
    runtime := { drainedAfterFinalValueConfig.runtime with terminal := .done } }

theorem finishAfterFinalValueWitness :
    FinishStep drainedAfterFinalValueConfig finishedAfterFinalValueConfig := by
  apply FinishStep.done <;> rfl

example {later : Config scalarTy scalarTy}
    {laterEvent : Event scalarTy scalarTy} :
    ¬Step finishedAfterFinalValueConfig laterEvent later :=
  terminal_no_step (by
    simp [finishedAfterFinalValueConfig, drainedAfterFinalValueConfig,
      valueDoneAfterEmitConfig, valueDoneConfig, readyConfig, readyRuntime])

/- DONE and ERROR are not demand-gated. -/

def zeroDemandConfig : Config scalarTy scalarTy :=
  { readyConfig with runtime := { readyConfig.runtime with demand := .finite 0 } }

theorem zeroDemandUnavailable :
    ¬zeroDemandConfig.runtime.demand.Available := by
  intro available
  obtain ⟨remaining, consume⟩ := available
  simp [zeroDemandConfig, readyConfig, readyRuntime, Demand.consume] at consume

example {after : Config scalarTy scalarTy} :
    ¬SourceStep zeroDemandConfig (.value liveValue continuedSource) after := by
  intro step
  cases step with
  | value _ _ _ available _ => exact zeroDemandUnavailable available

example {after : Config scalarTy scalarTy} :
    ¬SourceStep zeroDemandConfig (.valueAndDone liveValue) after := by
  intro step
  cases step with
  | valueAndDone _ _ _ available _ => exact zeroDemandUnavailable available

def sourceDoneAtZeroConfig : Config scalarTy scalarTy :=
  { zeroDemandConfig with
    runtime := {
      zeroDemandConfig.runtime with
      sourceTerminal := .done
      drain := .draining
    } }

theorem sourceDoneAtZeroWitness :
    SourceStep zeroDemandConfig .done sourceDoneAtZeroConfig := by
  apply SourceStep.done <;> rfl

def drainedAtZeroConfig : Config scalarTy scalarTy :=
  { sourceDoneAtZeroConfig with
    runtime := { sourceDoneAtZeroConfig.runtime with drain := .drained } }

theorem drainAtZeroWitness : DrainStep sourceDoneAtZeroConfig drainedAtZeroConfig := by
  apply DrainStep.complete <;> rfl

def doneAtZeroConfig : Config scalarTy scalarTy :=
  { drainedAtZeroConfig with
    runtime := { drainedAtZeroConfig.runtime with terminal := .done } }

theorem doneAtZeroWitness : FinishStep drainedAtZeroConfig doneAtZeroConfig := by
  apply FinishStep.done <;> rfl

example {later : Config scalarTy scalarTy}
    {laterEvent : Event scalarTy scalarTy} :
    ¬Step doneAtZeroConfig laterEvent later :=
  terminal_no_step (by
    simp [doneAtZeroConfig, drainedAtZeroConfig, sourceDoneAtZeroConfig,
      zeroDemandConfig, readyConfig, readyRuntime])

def errorAtZeroConfig : Config scalarTy scalarTy :=
  { zeroDemandConfig with
    runtime := {
      zeroDemandConfig.runtime with
      sourceTerminal := .error "boom"
      terminal := .error "boom"
    } }

theorem errorAtZeroWitness :
    SourceStep zeroDemandConfig (.error "boom") errorAtZeroConfig := by
  apply SourceStep.error <;> rfl

example {later : Config scalarTy scalarTy}
    {laterEvent : Event scalarTy scalarTy} :
    ¬Step errorAtZeroConfig laterEvent later :=
  terminal_no_step (by
    simp [errorAtZeroConfig, zeroDemandConfig, readyConfig, readyRuntime])

example {after : Config scalarTy scalarTy} :
    ¬SourceStep zeroDemandConfig (.wait reusableWaitable continuedSource) after := by
  intro step
  cases step with
  | wait _ _ _ available _ =>
      exact zeroDemandUnavailable available

/- WAIT uses continuation-owned roots and fresh Kernel-issued tokens. -/

def pendingConfig : Config scalarTy scalarTy :=
  { readyConfig with
    source := continuedSource
    runtime := { readyConfig.runtime with wait := .pendingArm reusableWaitable } }

theorem waitWitness :
    SourceStep readyConfig (.wait reusableWaitable continuedSource)
      pendingConfig := by
  apply SourceStep.wait
  · rfl
  · rfl
  · rfl
  · exact ⟨.finite 0, rfl⟩
  · exact ownedLiveSafe

example {after : Config scalarTy scalarTy} :
    ¬ArmStep pendingConfig otherWaitable firstToken .noSignal after := by
  intro step
  cases step with
  | quiet _ pending =>
      simp [pendingConfig, readyConfig, reusableWaitable, otherWaitable] at pending

def suspendedConfig : Config scalarTy scalarTy :=
  { pendingConfig with
    runtime := {
      pendingConfig.runtime with
      wait := .suspended reusableWaitable firstToken
      nextWakeGeneration := 1
    } }

def resumedConfig : Config scalarTy scalarTy :=
  { suspendedConfig with
    runtime := { suspendedConfig.runtime with wait := .ready } }

theorem quietArmWitness :
    ArmStep pendingConfig reusableWaitable firstToken .noSignal
      suspendedConfig := by
  apply ArmStep.quiet <;> rfl

theorem wakeWitness : WakeStep suspendedConfig firstToken resumedConfig := by
  apply WakeStep.wake <;> rfl

example : resumedConfig.source = continuedSource ∧
    resumedConfig.runtime.wait = .ready ∧
    resumedConfig.runtime.ownership = readyConfig.runtime.ownership ∧
    resumedConfig.runtime.demand = readyConfig.runtime.demand ∧
    resumedConfig.runtime.sourceTerminal = readyConfig.runtime.sourceTerminal ∧
    resumedConfig.runtime.drain = readyConfig.runtime.drain ∧
    resumedConfig.runtime.terminal = readyConfig.runtime.terminal ∧
    resumedConfig.runtime.nextWakeGeneration =
      readyConfig.runtime.nextWakeGeneration + 1 :=
  wait_arm_wake_preserves_source waitWitness quietArmWitness wakeWitness

example {after : Config scalarTy scalarTy} :
    ¬WakeStep suspendedConfig wrongToken after := by
  intro step
  cases step with
  | wake _ suspended =>
      simp [suspendedConfig, pendingConfig, readyConfig, readyRuntime,
        firstToken, wrongToken] at suspended

def racedReadyConfig : Config scalarTy scalarTy :=
  { pendingConfig with
    runtime := {
      pendingConfig.runtime with
      wait := .ready
      nextWakeGeneration := 1
    } }

theorem beforeArmWitness :
    ArmStep pendingConfig reusableWaitable firstToken .signalBeforeArm
      racedReadyConfig := by
  apply ArmStep.signaledBefore <;> rfl

theorem concurrentArmWitness :
    ArmStep pendingConfig reusableWaitable firstToken .signalConcurrentWithArm
      racedReadyConfig := by
  apply ArmStep.signaledConcurrent <;> rfl

example : racedReadyConfig.runtime.wait = .ready :=
  signal_before_arm_is_ready beforeArmWitness

example : racedReadyConfig.runtime.wait = .ready :=
  signal_concurrent_with_arm_is_ready concurrentArmWitness

example : firstToken = pendingConfig.runtime.nextWakeToken ∧
    suspendedConfig.runtime.nextWakeGeneration =
      pendingConfig.runtime.nextWakeGeneration + 1 :=
  arm_issues_fresh_token quietArmWitness

def secondSource : SourceState where
  cursor := 5
  liveRoots := [liveValue.pack]

def secondPendingConfig : Config scalarTy scalarTy :=
  { resumedConfig with
    source := secondSource
    runtime := { resumedConfig.runtime with wait := .pendingArm reusableWaitable } }

def secondToken : WaitToken where
  generation := 1

def secondSuspendedConfig : Config scalarTy scalarTy :=
  { secondPendingConfig with
    runtime := {
      secondPendingConfig.runtime with
      wait := .suspended reusableWaitable secondToken
      nextWakeGeneration := 2
    } }

theorem secondWaitWitness :
    SourceStep resumedConfig (.wait reusableWaitable secondSource)
      secondPendingConfig := by
  apply SourceStep.wait
  · rfl
  · rfl
  · rfl
  · exact ⟨.finite 0, rfl⟩
  · exact ownedLiveSafe

theorem secondArmWitness :
    ArmStep secondPendingConfig reusableWaitable secondToken .noSignal
      secondSuspendedConfig := by
  apply ArmStep.quiet <;> rfl

example : firstToken ≠ secondToken := by decide

example {after : Config scalarTy scalarTy} :
    ¬WakeStep secondSuspendedConfig firstToken after := by
  intro step
  cases step with
  | wake _ suspended =>
      simp [secondSuspendedConfig, secondPendingConfig, secondToken,
        firstToken, resumedConfig, suspendedConfig] at suspended

/- Cancellation unarms both pending and suspended registrations. -/

def pendingCancelledConfig : Config scalarTy scalarTy :=
  { pendingConfig with
    runtime := {
      pendingConfig.runtime with
      wait := .ready
      terminal := .cancelled
    } }

theorem pendingCancelWitness : CancelStep pendingConfig pendingCancelledConfig := by
  apply CancelStep.cancel
  rfl

def suspendedCancelledConfig : Config scalarTy scalarTy :=
  { suspendedConfig with
    runtime := {
      suspendedConfig.runtime with
      wait := .ready
      terminal := .cancelled
    } }

theorem suspendedCancelWitness :
    CancelStep suspendedConfig suspendedCancelledConfig := by
  apply CancelStep.cancel
  rfl

example : pendingCancelledConfig.runtime.wait = .ready ∧
    pendingCancelledConfig.runtime.terminal = .cancelled :=
  cancel_unarms_and_terminates pendingCancelWitness

example : suspendedCancelledConfig.runtime.wait = .ready ∧
    suspendedCancelledConfig.runtime.terminal = .cancelled :=
  cancel_unarms_and_terminates suspendedCancelWitness

/- Borrowed roots cannot be omitted because they are fields of SourceState. -/

def borrowedContext : OwnershipContext := fun candidate =>
  if candidate = liveValue.token then
    some { ty := scalarTy, ownership := .borrowed }
  else none

def borrowedRuntime : RuntimeState :=
  { readyRuntime with ownership := borrowedContext }

def borrowedConfig : Config scalarTy scalarTy :=
  { readyConfig with runtime := borrowedRuntime }

example {after : Config scalarTy scalarTy} :
    ¬SourceStep borrowedConfig (.wait reusableWaitable continuedSource) after := by
  intro step
  have safe := source_wait_requires_suspend_safe step
  have owned := safe liveValue.pack (by simp [continuedSource])
  simp [borrowedConfig, borrowedRuntime, borrowedContext, liveValue,
    Value.pack] at owned

end CMetaCFlowCalculus.Tests.PhaseB
