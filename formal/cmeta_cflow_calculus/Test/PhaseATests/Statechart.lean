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

example : select [ancestor, descendant, independent] =
    [descendant, independent] := by native_decide

example : ConflictFree (select [ancestor, descendant, independent]) := by
  exact select_conflict_free _

example : descendant ∈ select [ancestor, descendant, independent] := by
  native_decide

example : select [ancestor, descendant, independent] =
    select [ancestor, descendant, independent] := by
  exact select_deterministic rfl rfl

example : select [ancestor, descendant] = [descendant] := by
  exact descendant_preempts_pair ancestor descendant
    (by native_decide) (by native_decide)

/-- A candidate rejected by an ancestor is not resurrected after preemption. -/
example : select [ancestor, rejectedMiddle, descendant] = [descendant] := by
  native_decide

end CMetaCFlowCalculus.Tests.Statechart
