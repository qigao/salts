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

private theorem selectFrom_conflict_free (remaining : List Candidate)
    {selected : List Candidate} (pairwise : ConflictFree selected) :
    ConflictFree (selectFrom selected remaining) := by
  induction remaining generalizing selected with
  | nil => exact pairwise
  | cons candidate tail inductionHypothesis =>
      exact inductionHypothesis (insert_conflict_free candidate pairwise)

theorem select_conflict_free (candidates : List Candidate) :
    ConflictFree (select candidates) := by
  exact selectFrom_conflict_free candidates (by simp [ConflictFree])

theorem descendant_preempts_pair (ancestor descendant : Candidate)
    (conflict : conflicts ancestor descendant = true)
    (descends : properDescendantSource descendant ancestor = true) :
    select [ancestor, descendant] = [descendant] := by
  simp [select, selectFrom, insertCandidate, rejectedBySelected,
    retainNonconflicting, conflict, descends]

theorem select_deterministic {candidates first second : List Candidate}
    (firstRun : select candidates = first)
    (secondRun : select candidates = second) : first = second := by
  exact firstRun.symm.trans secondRun

end CMetaCFlowCalculus.CFlow.Statechart
