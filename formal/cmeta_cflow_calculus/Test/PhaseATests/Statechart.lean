import CMetaCFlowCalculus.Proofs.Statechart

namespace CMetaCFlowCalculus.Tests.Statechart

open CMetaCFlowCalculus.CFlow.Statechart

def ancestor : Candidate :=
  { transition := 10, source := 1, sourceAncestors := [], leafOrder := 0,
    exitSet := [2, 3] }

def descendant : Candidate :=
  { transition := 11, source := 3, sourceAncestors := [2, 1], leafOrder := 1,
    exitSet := [3] }

def independent : Candidate :=
  { transition := 12, source := 4, sourceAncestors := [1], leafOrder := 2,
    exitSet := [4] }

def rejectedMiddle : Candidate :=
  { transition := 13, source := 4, sourceAncestors := [], leafOrder := 1,
    exitSet := [2, 4] }

def mixedConflict : Candidate :=
  { transition := 14, source := 3, sourceAncestors := [2, 1], leafOrder := 2,
    exitSet := [3, 4] }

def targetlessDuplicate : Candidate :=
  { transition := 15, source := 1, sourceAncestors := [], leafOrder := 0,
    exitSet := [] }

example : select [ancestor, descendant, independent] =
    [descendant, independent] := by native_decide

example : ConflictFree (select [ancestor, descendant, independent]) := by
  exact select_conflict_free _

example : descendant ∈ select [ancestor, descendant, independent] := by
  native_decide

example : select [ancestor, descendant, independent] =
    select [ancestor, descendant, independent] := by
  exact select_deterministic rfl

example : select [ancestor, descendant] = [descendant] := by
  exact descendant_preempts_pair ancestor descendant
    (by native_decide) (by native_decide) (by native_decide)

/-- A candidate rejected by an ancestor is not resurrected after preemption. -/
example : select [ancestor, rejectedMiddle, descendant] = [descendant] := by
  native_decide

/-- Candidate IDs are globally and stably deduplicated before conflict work. -/
example : select [targetlessDuplicate, targetlessDuplicate] =
    [targetlessDuplicate] := by
  native_decide

example : descendant ∈ insertCandidate [ancestor, independent] descendant ∧
    ancestor ∉ insertCandidate [ancestor, independent] descendant := by
  exact accepted_descendant_removes_conflicting_ancestor
    (selected := [ancestor, independent])
    (ancestor := ancestor) (candidate := descendant)
    (by
      have same : select [ancestor, independent] =
          [ancestor, independent] := by native_decide
      rw [← same]
      exact select_conflict_free _)
    (by native_decide) (by native_decide)
    (by native_decide) (by native_decide) (by native_decide)

/-- A descendant does not partially delete its ancestor when an unrelated
    selected candidate also conflicts. -/
example : insertCandidate [ancestor, independent] mixedConflict =
    [ancestor, independent] := by
  exact unrelated_conflict_rejects_unchanged
    (selected := [ancestor, independent])
    (candidate := mixedConflict) (unrelated := independent)
    (by
      have same : select [ancestor, independent] =
          [ancestor, independent] := by native_decide
      rw [← same]
      exact select_conflict_free _)
    (by native_decide) (by native_decide) (by native_decide)

example : evaluateFrom initialSelection ([ancestor] ++ [descendant]) =
    evaluateFrom (evaluateFrom initialSelection [ancestor]) [descendant] := by
  exact evaluateFrom_append _ _ _

end CMetaCFlowCalculus.Tests.Statechart
