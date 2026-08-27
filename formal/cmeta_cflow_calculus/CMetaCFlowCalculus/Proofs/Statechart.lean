import CMetaCFlowCalculus.CFlow.Statechart

namespace CMetaCFlowCalculus.CFlow.Statechart

private theorem retain_pairwise (candidate : Candidate)
    {candidates : List Candidate} (pairwise : ConflictFree candidates) :
    ConflictFree (retainNonconflicting candidate candidates) := by
  exact List.Pairwise.filter _ pairwise

private theorem retained_disjoint (candidate : Candidate)
    (candidates : List Candidate) :
    ∀ current ∈ retainNonconflicting candidate candidates,
      conflicts current candidate = false := by
  intro current member
  have filtered : current ∈ candidates ∧
      (!conflicts current candidate) = true := by
    simpa [retainNonconflicting] using member
  simpa using filtered.2

private theorem insert_conflict_free (candidate : Candidate)
    {selected : List Candidate} (pairwise : ConflictFree selected) :
    ConflictFree (insertCandidate selected candidate) := by
  simp only [insertCandidate]
  split
  · exact pairwise
  · simp only [ConflictFree, List.pairwise_append]
    refine ⟨retain_pairwise candidate pairwise, by simp, ?_⟩
    intro current currentMember last lastMember
    have lastIsCandidate : last = candidate := by simpa using lastMember
    subst last
    exact retained_disjoint candidate selected current currentMember

private theorem step_conflict_free (state : Selection) (candidate : Candidate)
    (pairwise : ConflictFree state.selected) :
    ConflictFree (stepCandidate state candidate).selected := by
  simp only [stepCandidate]
  split
  · exact pairwise
  · exact insert_conflict_free candidate pairwise

private theorem evaluateFrom_conflict_free (remaining : List Candidate)
    {state : Selection} (pairwise : ConflictFree state.selected) :
    ConflictFree (evaluateFrom state remaining).selected := by
  induction remaining generalizing state with
  | nil => exact pairwise
  | cons candidate tail inductionHypothesis =>
      exact inductionHypothesis (step_conflict_free state candidate pairwise)

theorem select_conflict_free (candidates : List Candidate) :
    ConflictFree (select candidates) := by
  exact evaluateFrom_conflict_free candidates (by simp [ConflictFree, initialSelection])

theorem descendant_preempts_pair (ancestor descendant : Candidate)
    (conflict : conflicts ancestor descendant = true)
    (descends : properDescendantSource descendant ancestor = true)
    (differentTransition : descendant.transition ≠ ancestor.transition) :
    select [ancestor, descendant] = [descendant] := by
  simp [select, evaluateFrom, initialSelection, stepCandidate,
    insertCandidate, rejectedBySelected, retainNonconflicting,
    conflict, descends, differentTransition]

theorem accepted_descendant_removes_conflicting_ancestor
    {selected : List Candidate} {ancestor candidate : Candidate}
    (_prefixFree : ConflictFree selected)
    (accepted : rejectedBySelected candidate selected = false)
    (_ancestorMember : ancestor ∈ selected)
    (conflict : conflicts ancestor candidate = true)
    (_descends : properDescendantSource candidate ancestor = true)
    (distinct : ancestor ≠ candidate) :
    candidate ∈ insertCandidate selected candidate ∧
      ancestor ∉ insertCandidate selected candidate := by
  simp [insertCandidate, accepted, retainNonconflicting, conflict, distinct]

theorem unrelated_conflict_rejects_unchanged
    {selected : List Candidate} {candidate unrelated : Candidate}
    (_prefixFree : ConflictFree selected)
    (unrelatedMember : unrelated ∈ selected)
    (conflict : conflicts unrelated candidate = true)
    (notDescendant : properDescendantSource candidate unrelated = false) :
    insertCandidate selected candidate = selected := by
  have rejected : rejectedBySelected candidate selected = true := by
    unfold rejectedBySelected
    apply List.any_eq_true.mpr
    exact ⟨unrelated, unrelatedMember, by simp [conflict, notDescendant]⟩
  simp [insertCandidate, rejected]

theorem evaluateFrom_append (state : Selection)
    (first second : List Candidate) :
    evaluateFrom state (first ++ second) =
      evaluateFrom (evaluateFrom state first) second := by
  induction first generalizing state with
  | nil => rfl
  | cons candidate remaining inductionHypothesis =>
      simp only [List.cons_append, evaluateFrom]
      exact inductionHypothesis (stepCandidate state candidate)

theorem select_deterministic {first second : List Candidate}
    (sameOrderedInput : first = second) : select first = select second := by
  exact congrArg select sameOrderedInput

end CMetaCFlowCalculus.CFlow.Statechart
