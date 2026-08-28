import CMetaCFlowCalculus.CFlow.PlanManagedValues

namespace CMetaCFlowCalculus.CFlow.PlanManagedValues

namespace State

theorem empty_safe : empty.Safe := by
  simp [empty, Safe, Balanced, OwnershipValid]

theorem copy_failure_preserves_state (state : State) :
    copyOne state false = state := by
  rcases state with ⟨owner, phase, constructed, destroyed, live⟩
  cases owner <;> cases phase <;> rfl

theorem copy_success_preserves_balanced (state : State)
    (balanced : state.Balanced) :
    (copyOne state true).Balanced := by
  rcases state with ⟨owner, phase, constructed, destroyed, live⟩
  cases owner <;> cases phase <;>
    simp_all [copyOne, Balanced] <;> omega

theorem move_preserves_balanced (state moved : State)
    (balanced : state.Balanced) (transition : moveOne state = some moved) :
    moved.Balanced := by
  rcases state with ⟨owner, phase, constructed, destroyed, live⟩
  cases owner <;> cases phase <;> cases live <;>
    simp [moveOne] at transition
  subst moved
  simp [Balanced] at balanced ⊢
  omega

theorem discard_preserves_balanced (state discarded : State) (count : Nat)
    (balanced : state.Balanced)
    (transition : discard state count = some discarded) :
    discarded.Balanced := by
  by_cases admitted :
      state.owner = .vector ∧ state.phase = .building ∧ count ≤ state.live
  · simp [discard, admitted] at transition
    subst discarded
    simp [Balanced] at balanced ⊢
    omega
  · simp [discard, admitted] at transition

theorem failure_cleanup_disposes_every_live_value (state : State)
    (balanced : state.Balanced)
    (owner : state.owner = .vector)
    (phase : state.phase = .building) :
    ∃ failed,
      failCleanup state = some failed ∧
      failed.owner = .none ∧
      failed.phase = .failed ∧
      failed.live = 0 ∧
      failed.destroyed = state.destroyed + state.live ∧
      failed.constructed = failed.destroyed ∧
      failed.Safe ∧
      failCleanup failed = none := by
  refine ⟨{ state with
    owner := .none
    phase := .failed
    destroyed := state.destroyed + state.live
    live := 0 }, ?_⟩
  simp [failCleanup, owner, phase, Safe, Balanced, OwnershipValid]
  simp [Balanced] at balanced
  omega

theorem commit_preserves_balanced (state committed : State)
    (balanced : state.Balanced)
    (transition : commit state = some committed) :
    committed.Balanced := by
  rcases state with ⟨owner, phase, constructed, destroyed, live⟩
  cases owner <;> cases phase <;>
    simp [commit] at transition
  subst committed
  exact balanced

theorem release_disposes_every_live_value_once (state : State)
    (balanced : state.Balanced)
    (owner : state.owner = .result)
    (phase : state.phase = .committed) :
    ∃ released,
      release state = some released ∧
      released.owner = .none ∧
      released.phase = .released ∧
      released.live = 0 ∧
      released.destroyed = state.destroyed + state.live ∧
      released.constructed = released.destroyed ∧
      released.Safe ∧
      release released = none := by
  refine ⟨{ state with
    owner := .none
    phase := .released
    destroyed := state.destroyed + state.live
    live := 0 }, ?_⟩
  simp [release, owner, phase, Safe, Balanced, OwnershipValid]
  simp [Balanced] at balanced
  omega

end State

export State
  (commit_preserves_balanced copy_failure_preserves_state
   copy_success_preserves_balanced discard_preserves_balanced empty_safe
   failure_cleanup_disposes_every_live_value move_preserves_balanced
   release_disposes_every_live_value_once)

end CMetaCFlowCalculus.CFlow.PlanManagedValues
