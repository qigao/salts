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

def stateAncestors : Nat → List Nat
  | 3 => [2, 1]
  | 2 => [1]
  | _ => []

def stateDocumentOrder : Nat → Nat
  | 1 => 0
  | 2 => 1
  | 3 => 2
  | 4 => 3
  | _ => 99

example : exitOrder stateDocumentOrder [2, 4, 1, 3] = [4, 3, 2, 1] := by
  native_decide

example : ExitOrdered stateDocumentOrder
    (exitOrder stateDocumentOrder [2, 4, 1, 3]) := by
  exact exitOrder_ordered _ _

example : entryOrder stateDocumentOrder [3, 1, 4, 2] = [1, 2, 3, 4] := by
  native_decide

example : EntryOrdered stateDocumentOrder
    (entryOrder stateDocumentOrder [3, 1, 4, 2]) := by
  exact entryOrder_ordered _ _

def configurationModel : ConfigurationModel where
  stateUniverse := [1, 2, 3, 4]
  ancestors
    | 2 => [1]
    | 3 => [2, 1]
    | 4 => [2, 1]
    | _ => []
  children
    | 1 => [2]
    | 2 => [3, 4]
    | _ => []
  isReal := fun state => decide (state ∈ [1, 2, 3, 4])
  isCompound := fun state => decide (state = 1 ∨ state = 2)
  isParallel := fun _ => false
  isLeaf := fun state => decide (state = 3 ∨ state = 4)
  documentOrder := stateDocumentOrder

def microstepPlan : MicrostepPlan where
  exitSet := [3]
  directTargets := [4]
  defaultTargets := []
  historyTargets := []

example : constructNext configurationModel [1, 2, 3] microstepPlan =
    [1, 2, 4] := by
  native_decide

example : LegalConfiguration configurationModel
    (constructNext configurationModel [1, 2, 3] microstepPlan) := by
  apply constructed_microstep_preserves_legality
  · simp [LegalConfiguration, configurationModel, EntryOrdered,
      stateDocumentOrder]
  · native_decide

example : commitConfiguration false [1, 2, 3] [1, 4] = [1, 2, 3] := by
  exact failed_commit_preserves _ _

def shallowDefaults : Nat → List Nat
  | 2 => [3]
  | _ => []

example : restoreShallow [2] [4] shallowDefaults = [2, 3] := by
  native_decide

example : restoreShallow [] [4] shallowDefaults = [4] := by
  native_decide

def deepAncestors : Nat → List Nat
  | 3 => [1, 2, 3]
  | 4 => [1, 2, 4]
  | _ => []

example : restoreDeep [3, 4] deepAncestors = [1, 2, 3, 4] := by
  native_decide

example : ShallowRestored [2] [4] shallowDefaults [2, 3] := by
  exact restoreShallow_satisfies _ _ _

example : DeepRestored [3, 4] deepAncestors [1, 2, 3, 4] := by
  exact restoreDeep_satisfies _ _

end CMetaCFlowCalculus.Tests.Statechart
