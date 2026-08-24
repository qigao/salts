import CMetaCFlowCalculus.CFlow.ManagedMachineState

namespace CMetaCFlowCalculus.CFlow.ManagedMachineState

open CMetaCFlowCalculus.CFlow.MachineRuntime

namespace ManagedControl

theorem copy_failure_preserves_control (source target : ManagedValue) :
    stageCopy (ready source) target false = ready source := by
  rfl

theorem ready_balanced (source : ManagedValue) :
    (ready source).Balanced := by
  simp [ready, Balanced, liveCount, occupied]

private def expectedStaged (source target : ManagedValue) : ManagedControl :=
  { lifecycle := .open
    worker := .executing
    source := some source
    staged := some target
    ledger := (ready source).ledger.set target .live
    constructed := 2
    destroyed := 0
    completed := 0
    cancelled := 0 }

theorem fresh_stage_eq (source target : ManagedValue)
    (distinct : source.token ≠ target.token) :
    stageCopy (ready source) target true = expectedStaged source target := by
  have reverse : target.token ≠ source.token := Ne.symm distinct
  simp [stageCopy, ready, expectedStaged, reverse]

theorem occupied_token_stage_preserves_control (source target : ManagedValue)
    (same : target.token = source.token) :
    stageCopy (ready source) target true = ready source := by
  simp [stageCopy, ready, same]

theorem cancel_before_begin_commit (source target : ManagedValue) :
    beginCommit
      (requestCancel (stageCopy (ready source) target true)) = none := by
  by_cases same : target.token = source.token
  · rw [occupied_token_stage_preserves_control source target same]
    rfl
  · have distinct : source.token ≠ target.token := Ne.symm same
    rw [fresh_stage_eq source target distinct]
    rfl

theorem cancel_path_preserves_source_and_destroys_target
    (source target : ManagedValue) (distinct : source.token ≠ target.token) :
    ∃ settled,
      cancelPath source target = some settled ∧
      settled.source = some source ∧
      settled.staged = none ∧
      settled.ledger source.token = some (ResourceRecord.live source.ty) ∧
      settled.ledger target.token =
        some (ResourceRecord.destroyed target.ty) ∧
      settled.completed = 0 ∧
      settled.cancelled = 1 ∧
      settled.Balanced := by
  unfold cancelPath
  rw [fresh_stage_eq source target distinct]
  simp [expectedStaged, ready, requestCancel, discardCancelled,
    ResourceLedger.set, ResourceRecord.live, ResourceRecord.destroyed,
    distinct, Balanced, liveCount, occupied]

theorem commit_path_moves_target_and_destroys_source
    (source target : ManagedValue) (distinct : source.token ≠ target.token) :
    ∃ settled,
      commitPath source target = some settled ∧
      settled.source = some target ∧
      settled.staged = none ∧
      settled.ledger source.token =
        some (ResourceRecord.destroyed source.ty) ∧
      settled.ledger target.token = some (ResourceRecord.live target.ty) ∧
      settled.completed = 1 ∧
      settled.cancelled = 0 ∧
      settled.Balanced := by
  have reverse : target.token ≠ source.token := Ne.symm distinct
  unfold commitPath
  rw [fresh_stage_eq source target distinct]
  simp [expectedStaged, ready, beginCommit, requestCancel, commit,
    ResourceLedger.set, ResourceRecord.live, ResourceRecord.destroyed,
    reverse, Balanced, liveCount, occupied]

theorem cancel_path_disposes_every_value_once
    (source target : ManagedValue) (distinct : source.token ≠ target.token) :
    ∃ disposed,
      cancelPath source target >>= dispose = some disposed ∧
      disposed.source = none ∧
      disposed.staged = none ∧
      disposed.ledger source.token =
        some (ResourceRecord.destroyed source.ty) ∧
      disposed.ledger target.token =
        some (ResourceRecord.destroyed target.ty) ∧
      disposed.liveCount = 0 ∧
      disposed.Balanced ∧
      dispose disposed = none := by
  have reverse : target.token ≠ source.token := Ne.symm distinct
  unfold cancelPath
  rw [fresh_stage_eq source target distinct]
  simp [expectedStaged, ready, requestCancel, discardCancelled, dispose,
    ResourceLedger.set, ResourceRecord.live, ResourceRecord.destroyed,
    reverse, Balanced, liveCount, occupied]

theorem commit_path_disposes_every_value_once
    (source target : ManagedValue) (distinct : source.token ≠ target.token) :
    ∃ disposed,
      commitPath source target >>= dispose = some disposed ∧
      disposed.source = none ∧
      disposed.staged = none ∧
      disposed.ledger source.token =
        some (ResourceRecord.destroyed source.ty) ∧
      disposed.ledger target.token =
        some (ResourceRecord.destroyed target.ty) ∧
      disposed.liveCount = 0 ∧
      disposed.Balanced ∧
      dispose disposed = none := by
  have reverse : target.token ≠ source.token := Ne.symm distinct
  unfold commitPath
  rw [fresh_stage_eq source target distinct]
  simp [expectedStaged, ready, beginCommit, requestCancel, commit, dispose,
    ResourceLedger.set, ResourceRecord.live, ResourceRecord.destroyed,
    distinct, Balanced, liveCount, occupied]

end ManagedControl

export ManagedControl
  (cancel_before_begin_commit cancel_path_disposes_every_value_once
   cancel_path_preserves_source_and_destroys_target
   commit_path_disposes_every_value_once
   commit_path_moves_target_and_destroys_source
   copy_failure_preserves_control fresh_stage_eq
   occupied_token_stage_preserves_control ready_balanced)

end CMetaCFlowCalculus.CFlow.ManagedMachineState
