import CMetaCFlowCalculus.CFlow.Readiness

namespace CMetaCFlowCalculus.CFlow.Readiness

theorem next_generation_strict_and_bounded {current limit next : Nat}
    (prepared : nextGeneration current limit = some next) :
    current < next ∧ next ≤ limit := by
  simp [nextGeneration] at prepared
  rcases prepared with ⟨available, rfl⟩
  exact ⟨Nat.lt_succ_self current, Nat.succ_le_of_lt available⟩

theorem exhausted_generation_retires (limit : Nat) :
    nextGeneration limit limit = none := by
  simp [nextGeneration]

theorem zero_valid : Slot.zero.Valid := by
  simp [Slot.Valid, Slot.zero, Slot.inactive]

/-- Every modeled Platform transition preserves all orthogonal slot facts. -/
theorem Step.preservesValid {before after : Slot}
    (step : Step before after) (valid : before.Valid) : after.Valid := by
  cases step <;>
    simp_all [Slot.Valid, Slot.inactive, Slot.beginRegister,
      Slot.commitRegister, Slot.beginArm, Slot.commitArm, Slot.rollbackArm,
      Slot.dispatch, Slot.completeCallback, Slot.reserveTerminal,
      Slot.dispatchTerminal, Slot.completeTerminal, Slot.beginUnarm,
      Slot.commitUnarm, Slot.rollbackUnarmArmed, Slot.rollbackUnarmIdle,
      Slot.beginClose, Slot.rollbackClose, Slot.commitNativeClose,
      Slot.finishClose,
      Slot.markOrphan, Slot.retryOrphan, Slot.commitOrphanClose,
      Slot.reclaim, Slot.retire, Slot.acquireApiBorrow,
      Slot.releaseApiBorrow, Slot.addArmWaiter, Slot.removeArmWaiter,
      Slot.zero]

/-- Safety is inductive, so it holds for every finite callback/control trace. -/
theorem Trace.preservesValid {before after : Slot} {states : List Slot}
    (trace : Trace before states after) (valid : before.Valid) : after.Valid := by
  induction trace with
  | nil => exact valid
  | cons step remaining inductionHypothesis =>
      exact inductionHypothesis (step.preservesValid valid)

theorem ordinary_terminal_dispatch_exclusive (slot : Slot) :
    ¬(OrdinaryDispatchEnabled slot ∧ TerminalDispatchEnabled slot) := by
  rintro ⟨ordinary, terminal⟩
  have impossible : Terminal.none = Terminal.reserved :=
    ordinary.2.2.2.1.symm.trans terminal.2.2.2.1
  cases impossible

/-- After ordinary delivery consumes ARMED, the same arm cannot dispatch a
    second time without a separate successful arm transition. -/
theorem ordinary_dispatch_is_one_shot (before : Slot) :
    ¬OrdinaryDispatchEnabled before.dispatch := by
  simp [OrdinaryDispatchEnabled, Slot.dispatch]

theorem self_rearm_during_callback_is_busy (slot : Slot)
    (callback : slot.delivery = .callback) :
    decideArm true true slot = .busy := by
  simp [decideArm, callback]

theorem external_rearm_during_callback_waits (slot : Slot)
    (lifecycle : slot.lifecycle = .open)
    (callback : slot.delivery = .callback)
    (terminal : slot.terminal = .none) :
    decideArm false true slot = .wait := by
  simp [decideArm, lifecycle, callback, terminal]

theorem rearm_after_callback_can_proceed (slot : Slot)
    (lifecycle : slot.lifecycle = .open)
    (interest : slot.interest = .idle)
    (terminal : slot.terminal = .none)
    (control : slot.control = .none) :
    decideArm false true slot.completeCallback = .proceed := by
  simp [decideArm, Slot.completeCallback, lifecycle, interest, terminal,
    control]

theorem shutdown_wins_external_rearm (slot : Slot)
    (lifecycle : slot.lifecycle = .open) :
    decideArm false false slot = .shutdown := by
  simp [decideArm, lifecycle]

theorem close_precedes_shutdown_for_admitted_rearm (slot : Slot)
    (closing : slot.lifecycle = .closing) :
    decideArm false false slot = .busy := by
  simp [decideArm, closing]

theorem close_wins_external_rearm (slot : Slot)
    (closing : slot.lifecycle = .closing) :
    decideArm false true slot = .busy := by
  simp [decideArm, closing]

/-- Shutdown from any callback owned by this reactor fails immediately instead
    of waiting for terminalization or joining the callback's backend thread. -/
theorem callback_shutdown_is_busy (terminalizing shutdownComplete
    shutdownInflight : Bool) :
    decideShutdown true terminalizing shutdownComplete shutdownInflight =
      .busy := by
  simp [decideShutdown]

/-- Ordinary readiness consumes the one-shot interest before callback code. -/
theorem normal_dispatch_consumes_arm {before : Slot}
    (enabled : OrdinaryDispatchEnabled before) :
    Step before before.dispatch ∧ before.dispatch.interest = .idle ∧
      before.dispatch.delivery = .callback ∧
      before.dispatch.hasArmToken = false := by
  rcases enabled with ⟨lifecycle, interest, delivery, terminal, control⟩
  exact ⟨Step.normalDispatch before lifecycle interest delivery terminal control,
    by simp [Slot.dispatch]⟩

/-- A reserved terminal delivery cannot also be an ordinary dispatched state. -/
theorem terminal_reserved_excludes_callback (slot : Slot)
    (valid : slot.Valid) (reserved : slot.terminal = .reserved) :
    slot.interest = .armed ∧ slot.delivery = .idle := by
  have facts := valid.2.2.2.1 reserved
  exact ⟨facts.2.1, facts.2.2⟩

/-- Reclaim is impossible while a callback, API borrow, waiter, native watch,
    terminal reservation, or orphan cleanup owner remains. -/
theorem reclaim_requires_quiescence {before after : Slot}
    (step : Step before after) (valid : before.Valid)
    (free : after.lifecycle = .free) :
    before.delivery = .idle ∧ before.terminal = .none ∧
      before.nativeRegistered = false ∧ before.orphaned = false ∧
      before.armWaiters = 0 ∧ before.apiBorrows = 0 := by
  cases step <;> simp_all [Slot.Valid, Slot.inactive, Slot.reclaim,
    Slot.retire, Slot.beginRegister,
    Slot.commitRegister, Slot.beginArm, Slot.commitArm, Slot.rollbackArm,
    Slot.dispatch, Slot.completeCallback, Slot.reserveTerminal,
    Slot.dispatchTerminal, Slot.completeTerminal, Slot.beginUnarm,
    Slot.commitUnarm, Slot.rollbackUnarmArmed, Slot.rollbackUnarmIdle,
    Slot.beginClose, Slot.rollbackClose, Slot.commitNativeClose,
    Slot.finishClose,
    Slot.markOrphan, Slot.retryOrphan, Slot.commitOrphanClose,
    Slot.acquireApiBorrow, Slot.releaseApiBorrow, Slot.addArmWaiter,
    Slot.removeArmWaiter, Slot.zero]

theorem Admission.nonzero_entrant_blocks_register (state : Admission)
    (entered : state.entrants ≠ 0) : state.reserveRegister = none := by
  cases state with
  | mk closed entrants epoch => simp_all [Admission.reserveRegister]

/-- Once register owns its construction window, no public control can enter. -/
theorem Admission.register_reservation_blocks_controls
    {before reserved : Admission} {limit : Nat}
    (reservation : before.reserveRegister = some reserved) :
    reserved.enter limit = none := by
  simp [Admission.reserveRegister] at reservation
  rcases reservation with ⟨reservedFacts, rfl⟩
  simp [Admission.enter]

theorem Admission.closed_gate_blocks_controls (state : Admission)
    (closed : state.closed = true) (limit : Nat) :
    state.enter limit = none := by
  simp [Admission.enter, closed]

theorem Admission.close_failure_retains_epoch {before after : Admission}
    (reopen : before.reopenAfterFailure = some after) :
    after.slotEpoch = before.slotEpoch ∧ after.entrants = before.entrants := by
  simp [Admission.reopenAfterFailure] at reopen
  rcases reopen with ⟨closed, rfl⟩
  simp

/-- Publishing is possible only while the register construction gate is
    closed and no old entrant exists. -/
theorem Admission.publish_has_exclusive_window
    {before after : Admission} {epoch : Nat}
    (publish : before.publishRegister epoch = some after) :
    before.closed = true ∧ before.entrants = 0 ∧
      after.closed = true ∧ after.slotEpoch = some epoch := by
  simp [Admission.publishRegister] at publish
  rcases publish with ⟨window, rfl⟩
  exact ⟨window.1, window.2.1, window.1, rfl⟩

/-- A successful close consumes the old epoch and returns a true zero state. -/
theorem Admission.close_returns_zero {closed consumed reset : Admission}
    (consume : closed.consumeByClose = some consumed)
    (finish : consumed.reset = some reset) : reset = Admission.zero := by
  simp [Admission.consumeByClose] at consume
  rcases consume with ⟨window, rfl⟩
  simp [Admission.reset, window] at finish
  exact finish.symm

/-- The precise ABA counterexample is rejected: an old entrant admitted
    against a zero handle prevents reservation and therefore publication of a
    new registration generation. -/
theorem Admission.paused_old_entrant_blocks_reuse
    {entered : Admission} {limit : Nat}
    (entry : Admission.zero.enter limit = some entered) :
    entered.reserveRegister = none := by
  simp [Admission.zero, Admission.enter] at entry
  rcases entry with ⟨positive, rfl⟩
  simp [Admission.reserveRegister]

theorem Admission.full_count_fails_without_closing
    (limit : Nat) (epoch : Option Nat) :
    ({ closed := false, entrants := limit, slotEpoch := epoch } : Admission).enter
      limit = none := by
  simp [Admission.enter]

end CMetaCFlowCalculus.CFlow.Readiness
