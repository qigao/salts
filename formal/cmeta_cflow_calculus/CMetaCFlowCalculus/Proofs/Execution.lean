import CMetaCFlowCalculus.CFlow.Execution

namespace CMetaCFlowCalculus.CFlow

namespace Demand

theorem consume_eq_some {demand remaining : Demand}
    (consume : demand.consume = some remaining) :
    ∃ n, demand = .finite (n + 1) ∧ remaining = .finite n := by
  cases demand with
  | finite available =>
      cases available with
      | zero => simp [Demand.consume] at consume
      | succ n =>
          simp [Demand.consume] at consume
          subst remaining
          exact ⟨n, by simp⟩

end Demand

theorem source_step_requires_running {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy}
    {result : SourceResult sourceTy}
    (step : SourceStep before result after) :
    before.runtime.terminal = .running := by
  cases step <;> assumption

theorem arm_step_requires_running {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy}
    {waitable : Waitable} {token : WaitToken} {timing : ArmTiming}
    (step : ArmStep before waitable token timing after) :
    before.runtime.terminal = .running := by
  cases step <;> assumption

theorem wake_step_requires_running {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy} {token : WaitToken}
    (step : WakeStep before token after) :
    before.runtime.terminal = .running := by
  cases step <;> assumption

theorem emit_step_requires_running {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy} {output : CMeta.Value outputTy}
    (step : EmitStep before output after) :
    before.runtime.terminal = .running := by
  cases step <;> assumption

theorem drain_step_requires_running {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy} (step : DrainStep before after) :
    before.runtime.terminal = .running := by
  cases step <;> assumption

theorem finish_step_requires_running {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy} (step : FinishStep before after) :
    before.runtime.terminal = .running := by
  cases step <;> assumption

theorem cancel_step_requires_running {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy} (step : CancelStep before after) :
    before.runtime.terminal = .running := by
  cases step <;> assumption

theorem step_requires_running {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy}
    {event : Event sourceTy outputTy}
    (step : Step before event after) :
    before.runtime.terminal = .running := by
  cases step with
  | source sourceStep => exact source_step_requires_running sourceStep
  | arm armStep => exact arm_step_requires_running armStep
  | wake wakeStep => exact wake_step_requires_running wakeStep
  | emit emitStep => exact emit_step_requires_running emitStep
  | drain drainStep => exact drain_step_requires_running drainStep
  | finish finishStep => exact finish_step_requires_running finishStep
  | cancel cancelStep => exact cancel_step_requires_running cancelStep

/-- Every downstream Sink emission consumes exactly one demand unit. -/
theorem step_value_decrements_demand {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy}
    {event : Event sourceTy outputTy}
    (step : Step before event after) (emits : event.EmitsValue) :
    ∃ n, before.runtime.demand = .finite (n + 1) ∧
      after.runtime.demand = .finite n := by
  cases step with
  | source => simp [Event.EmitsValue] at emits
  | arm => simp [Event.EmitsValue] at emits
  | wake => simp [Event.EmitsValue] at emits
  | emit emitStep =>
      cases emitStep with
      | emit _ _ _ _ consume =>
          obtain ⟨n, beforeDemand, remaining⟩ :=
            Demand.consume_eq_some consume
          exact ⟨n, beforeDemand, by simp [remaining]⟩
  | drain => simp [Event.EmitsValue] at emits
  | finish => simp [Event.EmitsValue] at emits
  | cancel => simp [Event.EmitsValue] at emits

/-- Consuming a source value does not consume downstream demand. -/
theorem source_value_preserves_demand {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy}
    {output : CMeta.Value sourceTy} {next : SourceState}
    (step : SourceStep before (.value output next) after) :
    after.runtime.demand = before.runtime.demand := by
  cases step
  rfl

/-- Source VALUE_AND_DONE starts drain but does not terminate the run. -/
theorem source_value_and_done_starts_drain {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy}
    {output : CMeta.Value sourceTy}
    (step : SourceStep before (.valueAndDone output) after) :
    after.runtime.demand = before.runtime.demand ∧
      after.runtime.sourceTerminal = .done ∧
      after.runtime.drain = .draining ∧
      after.runtime.terminal = .running := by
  cases step with
  | valueAndDone running => exact ⟨rfl, rfl, rfl, running⟩

/-- Zero demand cannot precede a downstream value-emitting step. -/
theorem zero_demand_no_value {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy}
    {event : Event sourceTy outputTy}
    (zero : before.runtime.demand = .finite 0)
    (step : Step before event after) :
    ¬event.EmitsValue := by
  intro emits
  obtain ⟨n, positive, _⟩ := step_value_decrements_demand step emits
  rw [zero] at positive
  simp at positive

/-- Terminal states are absorbing: no kernel small-step can start from one. -/
theorem terminal_no_step {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy}
    {event : Event sourceTy outputTy}
    (terminal : before.runtime.terminal ≠ .running) :
    ¬Step before event after := by
  intro step
  exact terminal (step_requires_running step)

/-- A source may return WAIT only with its continuation roots suspension-safe. -/
theorem source_wait_requires_suspend_safe {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy}
    {waitable : Waitable} {next : SourceState}
    (step : SourceStep before (.wait waitable next) after) :
    CMeta.SuspendSafe before.runtime.ownership next.liveRoots := by
  cases step with
  | wait _ _ _ _ safe => exact safe

/--
A quiet arm followed by the matching wake resumes the exact saved
continuation and preserves every payload field; only generation advances.
-/
theorem wait_arm_wake_preserves_source {sourceTy outputTy : CMeta.Ty}
    {before pending suspended resumed : Config sourceTy outputTy}
    {waitable : Waitable} {token : WaitToken} {next : SourceState}
    (waitStep : SourceStep before (.wait waitable next) pending)
    (armStep : ArmStep pending waitable token .noSignal suspended)
    (wakeStep : WakeStep suspended token resumed) :
    resumed.source = next ∧
      resumed.runtime.wait = .ready ∧
      resumed.runtime.ownership = before.runtime.ownership ∧
      resumed.runtime.demand = before.runtime.demand ∧
      resumed.runtime.sourceTerminal = before.runtime.sourceTerminal ∧
      resumed.runtime.drain = before.runtime.drain ∧
      resumed.runtime.terminal = before.runtime.terminal ∧
      resumed.runtime.nextWakeGeneration =
        before.runtime.nextWakeGeneration + 1 := by
  cases waitStep
  cases armStep
  cases wakeStep
  exact ⟨rfl, rfl, rfl, rfl, rfl, rfl, rfl, rfl⟩

/-- Readiness observed before registration cannot become a suspension. -/
theorem signal_before_arm_is_ready {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy}
    {waitable : Waitable} {token : WaitToken}
    (step : ArmStep before waitable token .signalBeforeArm after) :
    after.runtime.wait = .ready := by
  cases step
  rfl

/-- Readiness racing with registration cannot become a suspension. -/
theorem signal_concurrent_with_arm_is_ready {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy}
    {waitable : Waitable} {token : WaitToken}
    (step : ArmStep before waitable token .signalConcurrentWithArm after) :
    after.runtime.wait = .ready := by
  cases step
  rfl

/-- Every arm issues the current generation and advances it exactly once. -/
theorem arm_issues_fresh_token {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy}
    {waitable : Waitable} {token : WaitToken} {timing : ArmTiming}
    (step : ArmStep before waitable token timing after) :
    token = before.runtime.nextWakeToken ∧
      after.runtime.nextWakeGeneration =
        before.runtime.nextWakeGeneration + 1 := by
  cases step <;> exact ⟨rfl, rfl⟩

/-- Cancellation unarms WAIT and enters the unique cancelled terminal state. -/
theorem cancel_unarms_and_terminates {sourceTy outputTy : CMeta.Ty}
    {before after : Config sourceTy outputTy}
    (step : CancelStep before after) :
    after.runtime.wait = .ready ∧ after.runtime.terminal = .cancelled := by
  cases step
  exact ⟨rfl, rfl⟩

end CMetaCFlowCalculus.CFlow
