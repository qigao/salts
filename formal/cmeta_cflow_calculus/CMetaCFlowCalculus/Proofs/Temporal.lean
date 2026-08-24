import CMetaCFlowCalculus.CFlow.Temporal

namespace CMetaCFlowCalculus.CFlow.Temporal

theorem retainedCount_bounded (state : State α) :
    retainedCount state ≤ 1 := by
  cases state.pending <;> simp [retainedCount]

theorem delayDeadline_monotonic (now duration : Nat) :
    now ≤ delayDeadline now duration := by
  simp [delayDeadline]

theorem debounceReplace_keeps_newest (state : State α) (value : α) :
    (debounceReplace state value).pending = some value := rfl

theorem timeout_timer_is_unique_error (cause : ReadyCause) :
    timeoutTerminal cause = .error ↔ cause = .timer := by
  cases cause <;> simp [timeoutTerminal]

theorem zeroDelay_same_deadline (now : Nat) :
    delayDeadline now 0 = now := by simp [delayDeadline]

end CMetaCFlowCalculus.CFlow.Temporal
