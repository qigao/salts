import CMetaCFlowCalculus.CFlow.MachineHierarchy

namespace CMetaCFlowCalculus.CFlow.MachineHierarchy

theorem selectFlat_enumerate_equivalent
    (guards : Nat → Bool) (priority : Nat) (candidates : List Candidate) :
    selectFlat guards (enumerate priority candidates) =
      firstEnabled guards candidates := by
  induction candidates generalizing priority with
  | nil => rfl
  | cons candidate remaining inductionHypothesis =>
      simp only [enumerate, selectFlat, firstEnabled]
      by_cases enabled : guards candidate.guard
      · simp [enabled]
      · simp [enabled, inductionHypothesis]

theorem exitPath_excludes_lca (sourceToRoot : List Nat) (lca : Nat) :
    lca ∉ exitPath sourceToRoot lca := by
  induction sourceToRoot with
  | nil => simp [exitPath]
  | cons head remaining inductionHypothesis =>
      by_cases same : head = lca
      · simp [exitPath, same]
      · have reverse : lca ≠ head := fun equality => same equality.symm
        have tail : lca ∉ remaining.takeWhile (· != lca) := by
          simpa [exitPath] using inductionHypothesis
        simp [exitPath, same, reverse, tail]

theorem entryPath_excludes_lca (targetToRoot : List Nat) (lca : Nat) :
    lca ∉ entryPath targetToRoot lca := by
  simpa [entryPath, exitPath] using
    exitPath_excludes_lca targetToRoot lca

theorem flattenedTargetKind_preserved
    (resolvedLeaf : _root_.CMetaCFlowCalculus.CFlow.Machine.StateDecl) :
    flattenedTargetKind resolvedLeaf = resolvedLeaf.kind := rfl

end CMetaCFlowCalculus.CFlow.MachineHierarchy
