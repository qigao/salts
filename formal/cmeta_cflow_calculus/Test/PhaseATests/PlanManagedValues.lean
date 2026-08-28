import CMetaCFlowCalculus.CFlow.PlanManagedValues
import CMetaCFlowCalculus.Proofs.PlanManagedValues

namespace CMetaCFlowCalculus.Tests.PlanManagedValues

open CMetaCFlowCalculus.CFlow.PlanManagedValues

def copiedThree : State :=
  copyOne (copyOne (copyOne empty true) true) true

example : empty.Safe := empty_safe

example : copyOne copiedThree false = copiedThree :=
  copy_failure_preserves_state copiedThree

theorem copiedThree_balanced : copiedThree.Balanced := by
  exact copy_success_preserves_balanced _
    (copy_success_preserves_balanced _
      (copy_success_preserves_balanced _ empty_safe.1))

example :
    ∃ failed,
      failCleanup copiedThree = some failed ∧
      failed.owner = .none ∧
      failed.phase = .failed ∧
      failed.live = 0 ∧
      failed.destroyed = copiedThree.destroyed + copiedThree.live ∧
      failed.constructed = failed.destroyed ∧
      failed.Safe ∧
      failCleanup failed = none := by
  exact failure_cleanup_disposes_every_live_value
    copiedThree copiedThree_balanced rfl rfl

def committedThree : State :=
  { copiedThree with owner := .result, phase := .committed }

theorem committedThree_balanced : committedThree.Balanced :=
  copiedThree_balanced

example :
    ∃ released,
      release committedThree = some released ∧
      released.owner = .none ∧
      released.phase = .released ∧
      released.live = 0 ∧
      released.destroyed = committedThree.destroyed + committedThree.live ∧
      released.constructed = released.destroyed ∧
      released.Safe ∧
      release released = none := by
  exact release_disposes_every_live_value_once
    committedThree committedThree_balanced rfl rfl

end CMetaCFlowCalculus.Tests.PlanManagedValues
