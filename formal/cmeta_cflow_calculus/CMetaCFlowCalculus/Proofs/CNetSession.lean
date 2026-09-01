import CMetaCFlowCalculus.CNet.Session

namespace CMetaCFlowCalculus.CNet.Session

theorem terminal_outcome_exclusive (outcome : TerminalOutcome) :
    ¬(IsClosed outcome ∧ IsFailed outcome) := by
  cases outcome <;> simp [IsClosed, IsFailed]

theorem terminal_has_no_lifecycle_step (after : State) :
    ¬LifecycleStep .terminal after := by
  intro step
  cases step

theorem retired_has_no_lifecycle_step (after : State) :
    ¬LifecycleStep .retired after := by
  intro step
  cases step

theorem reuse_increases_generation {limit before after : Nat}
    {afterState : State}
    (recycle : Recycle limit .terminal before afterState after)
    (reused : afterState = .free) : before < after := by
  cases recycle with
  | reuse available => exact Nat.lt_succ_self before
  | retire => simp at reused

theorem exhausted_generation_retires {limit after : Nat}
    {afterState : State}
    (recycle : Recycle limit .terminal limit afterState after) :
    afterState = .retired ∧ after = limit := by
  cases recycle with
  | reuse available => simp at available
  | retire => exact ⟨rfl, rfl⟩

theorem released_reservation_increases_generation {limit before after : Nat}
    {afterState : State}
    (release : ReleaseReservation limit .reserved before afterState after)
    (reused : afterState = .free) : before < after := by
  cases release with
  | reuse available => exact Nat.lt_succ_self before
  | retire => simp at reused

theorem exhausted_reservation_retires {limit after : Nat}
    {afterState : State}
    (release : ReleaseReservation limit .reserved limit afterState after) :
    afterState = .retired ∧ after = limit := by
  cases release with
  | reuse available => simp at available
  | retire => exact ⟨rfl, rfl⟩

end CMetaCFlowCalculus.CNet.Session
