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

theorem descendant_precedes_ancestor_on_exit
    (ancestors : Nat → List Nat) (documentOrder : Nat → Nat)
    {descendant ancestor : Nat}
    (descends : (ancestors descendant).contains ancestor = true) :
    exitPrecedes ancestors documentOrder descendant ancestor = true := by
  have member : ancestor ∈ ancestors descendant := by simpa using descends
  simp [exitPrecedes, member]

theorem ancestor_precedes_descendant_on_entry
    (ancestors : Nat → List Nat) (documentOrder : Nat → Nat)
    {ancestor descendant : Nat}
    (descends : (ancestors descendant).contains ancestor = true) :
    entryPrecedes ancestors documentOrder ancestor descendant = true := by
  have member : ancestor ∈ ancestors descendant := by simpa using descends
  simp [entryPrecedes, member]

theorem exitOrder_ordered (documentOrder : Nat → Nat) (states : List Nat) :
    ExitOrdered documentOrder (exitOrder documentOrder states) := by
  unfold ExitOrdered exitOrder
  simpa only [decide_eq_true_eq] using
    (List.pairwise_mergeSort
      (le := fun left right => decide (documentOrder right ≤ documentOrder left))
      (fun _ _ _ leftRight rightThird => by
        simp only [decide_eq_true_eq] at leftRight rightThird ⊢
        exact Nat.le_trans rightThird leftRight)
      (fun left right => by
        simp only [Bool.or_eq_true, decide_eq_true_eq]
        exact Nat.le_total (documentOrder right) (documentOrder left))
      states)

theorem entryOrder_ordered (documentOrder : Nat → Nat) (states : List Nat) :
    EntryOrdered documentOrder (entryOrder documentOrder states) := by
  unfold EntryOrdered entryOrder
  simpa only [decide_eq_true_eq] using
    (List.pairwise_mergeSort
      (le := fun left right => decide (documentOrder left ≤ documentOrder right))
      (fun _ _ _ leftRight rightThird => by
        simp only [decide_eq_true_eq] at leftRight rightThird ⊢
        exact Nat.le_trans leftRight rightThird)
      (fun left right => by
        simp only [Bool.or_eq_true, decide_eq_true_eq]
        exact Nat.le_total (documentOrder left) (documentOrder right))
      states)

theorem validateConfiguration_sound {model : ConfigurationModel}
    {active : List Nat}
    (validated : validateConfiguration model active = true) :
    LegalConfiguration model active := by
  simpa [validateConfiguration, LegalConfiguration, EntryOrdered] using
    validated

/-- The C correspondence theorem is validation-gated: construction has no
    legality witness field, and the executable validator checks the fully
    resolved staged list before publication. -/
theorem constructed_microstep_preserves_legality
    {model : ConfigurationModel} {published : List Nat}
    (plan : MicrostepPlan)
    (_publishedLegal : LegalConfiguration model published)
    (validated :
      validateConfiguration model (constructNext model published plan) = true) :
    LegalConfiguration model (constructNext model published plan) := by
  exact validateConfiguration_sound validated

theorem failed_commit_preserves (published staged : List Nat) :
    commitConfiguration false published staged = published := by
  rfl

theorem successful_commit_publishes_staged
    (published staged : List Nat) :
    commitConfiguration true published staged = staged := by
  rfl

theorem restoreShallow_satisfies
    (remembered defaultTarget : List Nat)
    (defaultBelow : Nat → List Nat) :
    ShallowRestored remembered defaultTarget defaultBelow
      (restoreShallow remembered defaultTarget defaultBelow) := by
  cases remembered with
  | nil => simp [ShallowRestored, restoreShallow]
  | cons head tail =>
      simp only [ShallowRestored, restoreShallow]
      constructor
      · intro child childMember
        exact List.mem_flatMap.mpr ⟨child, childMember, by simp⟩
      · intro child childMember state stateMember
        exact List.mem_flatMap.mpr ⟨child, childMember, by simp [stateMember]⟩

theorem restoreDeep_satisfies
    (rememberedLeaves : List Nat)
    (ancestorsIncludingSelf : Nat → List Nat) :
    DeepRestored rememberedLeaves ancestorsIncludingSelf
      (restoreDeep rememberedLeaves ancestorsIncludingSelf) := by
  intro leaf leafMember state stateMember
  simp only [restoreDeep, List.mem_eraseDups, List.mem_flatMap]
  exact ⟨leaf, leafMember, stateMember⟩

end CMetaCFlowCalculus.CFlow.Statechart
