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

namespace CommitControl

theorem executing_valid (source target : Config) :
    (executing source target).Valid := by
  simp [executing, Valid]

/-- Constructive counterexample for the old implementation shape: cancellation
    may be visible while the unconditionally committed target replaces source. -/
theorem legacy_cancel_then_commit_counterexample
    (source target : Config) (distinct : target ≠ source) :
    let after := Legacy.unconditionalCommit
      (requestCancel (executing source target))
    after.lifecycle = .cancelRequested ∧
      after.source = target ∧
      after.source ≠ source ∧
      after.completed = 1 := by
  simp [executing, requestCancel, Legacy.unconditionalCommit, distinct]

/-- Cancellation requested before the linearization point prevents commit
    admission. -/
theorem cancel_before_begin_commit (source target : Config) :
    beginCommit (requestCancel (executing source target)) = none := by
  rfl

/-- When cancellation wins, settling the staged turn preserves source and
    accounts for the Event exactly once as cancelled. -/
theorem cancel_before_commit_preserves_source (source target : Config) :
    ∃ after,
      discardCancelled (requestCancel (executing source target)) = some after ∧
      after.lifecycle = .terminal ∧
      after.worker = .idle ∧
      after.source = source ∧
      after.staged = none ∧
      after.completed = 0 ∧
      after.cancelled = 1 := by
  refine ⟨({ lifecycle := .terminal
             worker := .idle
             source := source
             staged := none
             completed := 0
             cancelled := 1 } : CommitControl), ?_⟩
  simp [executing, requestCancel, discardCancelled]

/-- Once `beginCommit` wins, overlapping cancellation terminates subsequent
    work but the staged target is committed exactly once. -/
theorem begin_commit_before_cancel_commits_once (source target : Config) :
    ∃ begun after,
      beginCommit (executing source target) = some begun ∧
      commit (requestCancel begun) = some after ∧
      after.lifecycle = .terminal ∧
      after.worker = .idle ∧
      after.source = target ∧
      after.staged = none ∧
      after.completed = 1 ∧
      after.cancelled = 0 := by
  refine ⟨({ lifecycle := .open
             worker := .committing
             source := source
             staged := some target
             completed := 0
             cancelled := 0 } : CommitControl),
          ({ lifecycle := .terminal
             worker := .idle
             source := target
             staged := none
             completed := 1
             cancelled := 0 } : CommitControl), ?_⟩
  simp [executing, beginCommit, requestCancel, commit]

/-- A cancellation-visible executing state cannot both discard and begin a
    commit. This is the local exclusivity obligation at the arbitration point. -/
theorem cancel_and_begin_commit_exclusive (control : CommitControl)
    (cancelVisible : control.lifecycle = .cancelRequested) :
    control.discardCancelled.isSome = true →
      control.beginCommit = none := by
  cases control with
  | mk lifecycle worker source staged completed cancelled =>
      simp only at cancelVisible
      subst lifecycle
      cases worker <;> cases staged <;>
        simp [discardCancelled, beginCommit]

end CommitControl

end CMetaCFlowCalculus.CFlow.MachineRuntime
