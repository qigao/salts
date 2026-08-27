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

theorem stateHierarchyPreorder :
    HierarchyPreorder [1, 2, 3, 4] stateAncestors stateDocumentOrder := by
  constructor
  · simp [stateAncestors]
  · intro descendant middle ancestor descendantMember middleMember
      ancestorMember middleAncestor ancestorAncestor
    have descendantCases : descendant = 1 ∨ descendant = 2 ∨
        descendant = 3 ∨ descendant = 4 := by simpa using descendantMember
    have middleCases : middle = 1 ∨ middle = 2 ∨
        middle = 3 ∨ middle = 4 := by simpa using middleMember
    have ancestorCases : ancestor = 1 ∨ ancestor = 2 ∨
        ancestor = 3 ∨ ancestor = 4 := by simpa using ancestorMember
    rcases descendantCases with rfl | rfl | rfl | rfl <;>
      rcases middleCases with rfl | rfl | rfl | rfl <;>
      rcases ancestorCases with rfl | rfl | rfl | rfl <;>
      simp [stateAncestors] at middleAncestor ancestorAncestor ⊢
  · intro left leftMember right rightMember equal
    have leftCases : left = 1 ∨ left = 2 ∨ left = 3 ∨ left = 4 := by
      simpa using leftMember
    have rightCases : right = 1 ∨ right = 2 ∨ right = 3 ∨ right = 4 := by
      simpa using rightMember
    rcases leftCases with rfl | rfl | rfl | rfl <;>
      rcases rightCases with rfl | rfl | rfl | rfl <;>
      simp [stateDocumentOrder] at equal ⊢
  · intro descendant descendantMember ancestor ancestorMember ancestorOf
    have descendantCases : descendant = 1 ∨ descendant = 2 ∨
        descendant = 3 ∨ descendant = 4 := by simpa using descendantMember
    have ancestorCases : ancestor = 1 ∨ ancestor = 2 ∨
        ancestor = 3 ∨ ancestor = 4 := by simpa using ancestorMember
    rcases descendantCases with rfl | rfl | rfl | rfl <;>
      rcases ancestorCases with rfl | rfl | rfl | rfl <;>
      simp [stateAncestors, stateDocumentOrder] at ancestorOf ⊢
  · intro ancestor ancestorMember descendant descendantMember between
      betweenMember ancestorOf ancestorBeforeBetween betweenBeforeDescendant
    have ancestorCases : ancestor = 1 ∨ ancestor = 2 ∨
        ancestor = 3 ∨ ancestor = 4 := by simpa using ancestorMember
    have descendantCases : descendant = 1 ∨ descendant = 2 ∨
        descendant = 3 ∨ descendant = 4 := by simpa using descendantMember
    have betweenCases : between = 1 ∨ between = 2 ∨
        between = 3 ∨ between = 4 := by simpa using betweenMember
    rcases ancestorCases with rfl | rfl | rfl | rfl <;>
      rcases descendantCases with rfl | rfl | rfl | rfl <;>
      rcases betweenCases with rfl | rfl | rfl | rfl <;>
      simp [stateAncestors, stateDocumentOrder] at ancestorOf ancestorBeforeBetween betweenBeforeDescendant ⊢

def reversedAncestorDocumentOrder : Nat → Nat
  | 1 => 2
  | 2 => 1
  | 3 => 0
  | 4 => 3
  | _ => 99

example : ¬HierarchyPreorder [1, 2, 3, 4] stateAncestors
    reversedAncestorDocumentOrder := by
  intro invalid
  have contradiction := invalid.ancestorEarlier 2 (by simp) 1 (by simp) (by simp [stateAncestors])
  simp [reversedAncestorDocumentOrder] at contradiction

example : exitOrder [1, 2, 3, 4] stateAncestors stateDocumentOrder
    [2, 4, 1, 3] = [4, 3, 2, 1] := by
  native_decide

example : ExitOrdered stateAncestors stateDocumentOrder
    (exitOrder [1, 2, 3, 4] stateAncestors stateDocumentOrder
      [2, 4, 1, 3]) := by
  exact exitOrder_ordered stateHierarchyPreorder _ (by simp)

example : entryOrder [1, 2, 3, 4] stateAncestors stateDocumentOrder
    [3, 1, 4, 2] = [1, 2, 3, 4] := by
  native_decide

example : EntryOrdered stateAncestors stateDocumentOrder
    (entryOrder [1, 2, 3, 4] stateAncestors stateDocumentOrder
      [3, 1, 4, 2]) := by
  exact entryOrder_ordered stateHierarchyPreorder _ (by simp)

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
  · simp [LegalConfiguration, configurationModel, DocumentOrdered,
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

def macroQueues : RuntimeQueues :=
  { eventless := [1]
    internal := [2]
    completions := [3]
    external := [4, 5] }

def recordTrigger (_trigger : RuntimeTrigger)
    (queues : RuntimeQueues) : RuntimeQueues := queues

example : runMacrostep recordTrigger 4 true macroQueues =
    { queues := { eventless := [], internal := [], completions := [],
                  external := [5] }
      trace := [.eventless 1, .internal 2, .completion 3, .external 4]
      quanta := 4
      limited := false } := by
  native_decide

example : (runMacrostep recordTrigger 3 true macroQueues).limited = true := by
  native_decide

example : (runMacrostep recordTrigger 4 true macroQueues).quanta ≤ 4 := by
  exact runMacrostep_quanta_bounded _ _ _ _

example : externalCardinality
    (runMacrostep recordTrigger 8 true macroQueues).trace = 1 := by
  native_decide

example : (runMacrostep recordTrigger 4 true macroQueues).trace.length =
    (runMacrostep recordTrigger 4 true macroQueues).quanta := by
  exact runMacrostep_trace_length _ _ _ _

example : RuntimePriorityStep true macroQueues (.eventless 1)
    { macroQueues with eventless := [] } := by
  exact popRuntimeTrigger_priority (by native_decide)

def cMacrostepRow : CMacrostepRow :=
  { fuel := 4
    allowExternal := true
    before := macroQueues
    after := { eventless := [], internal := [], completions := [],
               external := [5] }
    trace := [.eventless 1, .internal 2, .completion 3, .external 4]
    quanta := 4
    limited := false }

example : checkCMacrostepRow recordTrigger cMacrostepRow = true := by
  native_decide

example : CMacrostepRowRefines recordTrigger cMacrostepRow := by
  exact c_macrostep_row_sound (by native_decide)

example : checkCMacrostepRow recordTrigger
    { cMacrostepRow with
      trace := [.internal 2, .eventless 1, .completion 3, .external 4] } =
    false := by
  native_decide

end CMetaCFlowCalculus.Tests.Statechart
