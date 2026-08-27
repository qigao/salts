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

theorem exitSortLe_eq_document {stateUniverse : List Nat}
    {ancestors : Nat → List Nat} {documentOrder : Nat → Nat}
    (preorder : HierarchyPreorder stateUniverse ancestors documentOrder)
    (left right : Nat) :
    exitSortLe stateUniverse ancestors documentOrder left right =
      decide (documentOrder right ≤ documentOrder left) := by
  by_cases leftMember : left ∈ stateUniverse
  · by_cases rightMember : right ∈ stateUniverse
    · have leftContains : stateUniverse.contains left = true := by
        simpa using leftMember
      have rightContains : stateUniverse.contains right = true := by
        simpa using rightMember
      rw [exitSortLe]
      simp only [leftContains, rightContains, Bool.true_and, if_true]
      by_cases same : left = right
      · subst right
        simp
      by_cases leftDescends : right ∈ ancestors left
      · have earlier := preorder.ancestorEarlier
          left leftMember right rightMember leftDescends
        simp [same, exitPrecedes, leftDescends, Nat.le_of_lt earlier]
      by_cases rightDescends : left ∈ ancestors right
      · have earlier := preorder.ancestorEarlier
          right rightMember left leftMember rightDescends
        have notLe : ¬documentOrder right ≤ documentOrder left :=
          Nat.not_le_of_lt earlier
        simp [same, exitPrecedes, leftDescends, rightDescends, notLe]
      · have documentNe : documentOrder right ≠ documentOrder left := by
          intro equal
          apply same
          exact (preorder.documentUnique
            right rightMember left leftMember equal).symm
        simp [same, exitPrecedes, leftDescends, rightDescends,
              Nat.lt_iff_le_and_ne, documentNe]
    · have rightContains : stateUniverse.contains right = false := by
        simpa using rightMember
      simp [exitSortLe, leftMember, rightMember]
  · have leftContains : stateUniverse.contains left = false := by
      simpa using leftMember
    simp [exitSortLe, leftMember]

theorem entrySortLe_eq_document {stateUniverse : List Nat}
    {ancestors : Nat → List Nat} {documentOrder : Nat → Nat}
    (preorder : HierarchyPreorder stateUniverse ancestors documentOrder)
    (left right : Nat) :
    entrySortLe stateUniverse ancestors documentOrder left right =
      decide (documentOrder left ≤ documentOrder right) := by
  by_cases leftMember : left ∈ stateUniverse
  · by_cases rightMember : right ∈ stateUniverse
    · have leftContains : stateUniverse.contains left = true := by
        simpa using leftMember
      have rightContains : stateUniverse.contains right = true := by
        simpa using rightMember
      rw [entrySortLe]
      simp only [leftContains, rightContains, Bool.true_and, if_true]
      by_cases same : left = right
      · subst right
        simp
      by_cases rightDescends : left ∈ ancestors right
      · have earlier := preorder.ancestorEarlier
          right rightMember left leftMember rightDescends
        simp [same, entryPrecedes, rightDescends, Nat.le_of_lt earlier]
      by_cases leftDescends : right ∈ ancestors left
      · have earlier := preorder.ancestorEarlier
          left leftMember right rightMember leftDescends
        have notLe : ¬documentOrder left ≤ documentOrder right :=
          Nat.not_le_of_lt earlier
        simp [same, entryPrecedes, rightDescends, leftDescends, notLe]
      · have documentNe : documentOrder left ≠ documentOrder right := by
          intro equal
          apply same
          exact preorder.documentUnique left leftMember right rightMember equal
        simp [same, entryPrecedes, rightDescends, leftDescends,
              Nat.lt_iff_le_and_ne, documentNe]
    · have rightContains : stateUniverse.contains right = false := by
        simpa using rightMember
      simp [entrySortLe, leftMember, rightMember]
  · have leftContains : stateUniverse.contains left = false := by
      simpa using leftMember
    simp [entrySortLe, leftMember]

theorem exitOrder_ordered {stateUniverse : List Nat}
    {ancestors : Nat → List Nat} {documentOrder : Nat → Nat}
    (preorder : HierarchyPreorder stateUniverse ancestors documentOrder)
    (states : List Nat)
    (statesInUniverse : ∀ state ∈ states, state ∈ stateUniverse) :
    ExitOrdered ancestors documentOrder
      (exitOrder stateUniverse ancestors documentOrder states) := by
  unfold ExitOrdered exitOrder
  have sorted : List.Pairwise
      (fun left right =>
        exitSortLe stateUniverse ancestors documentOrder left right = true)
      (states.mergeSort
        (exitSortLe stateUniverse ancestors documentOrder)) := by
    apply List.pairwise_mergeSort
    · intro first second third firstSecond secondThird
      rw [exitSortLe_eq_document preorder] at firstSecond secondThird ⊢
      simp only [decide_eq_true_eq] at firstSecond secondThird ⊢
      exact Nat.le_trans secondThird firstSecond
    · intro left right
      rw [exitSortLe_eq_document preorder,
          exitSortLe_eq_document preorder]
      simp only [Bool.or_eq_true, decide_eq_true_eq]
      exact Nat.le_total (documentOrder right) (documentOrder left)
  apply List.Pairwise.imp_of_mem (p := sorted)
  intro left right leftMember rightMember ordered
  have leftStateUniverse : left ∈ stateUniverse := by
    apply statesInUniverse left
    simpa using leftMember
  have rightStateUniverse : right ∈ stateUniverse := by
    apply statesInUniverse right
    simpa using rightMember
  simpa [exitSortLe, leftStateUniverse, rightStateUniverse] using ordered

theorem entryOrder_ordered {stateUniverse : List Nat}
    {ancestors : Nat → List Nat} {documentOrder : Nat → Nat}
    (preorder : HierarchyPreorder stateUniverse ancestors documentOrder)
    (states : List Nat)
    (statesInUniverse : ∀ state ∈ states, state ∈ stateUniverse) :
    EntryOrdered ancestors documentOrder
      (entryOrder stateUniverse ancestors documentOrder states) := by
  unfold EntryOrdered entryOrder
  have sorted : List.Pairwise
      (fun left right =>
        entrySortLe stateUniverse ancestors documentOrder left right = true)
      (states.mergeSort
        (entrySortLe stateUniverse ancestors documentOrder)) := by
    apply List.pairwise_mergeSort
    · intro first second third firstSecond secondThird
      rw [entrySortLe_eq_document preorder] at firstSecond secondThird ⊢
      simp only [decide_eq_true_eq] at firstSecond secondThird ⊢
      exact Nat.le_trans firstSecond secondThird
    · intro left right
      rw [entrySortLe_eq_document preorder,
          entrySortLe_eq_document preorder]
      simp only [Bool.or_eq_true, decide_eq_true_eq]
      exact Nat.le_total (documentOrder left) (documentOrder right)
  apply List.Pairwise.imp_of_mem (p := sorted)
  intro left right leftMember rightMember ordered
  have leftStateUniverse : left ∈ stateUniverse := by
    apply statesInUniverse left
    simpa using leftMember
  have rightStateUniverse : right ∈ stateUniverse := by
    apply statesInUniverse right
    simpa using rightMember
  simpa [entrySortLe, leftStateUniverse, rightStateUniverse] using ordered

theorem validateConfiguration_sound {model : ConfigurationModel}
    {active : List Nat}
    (validated : validateConfiguration model active = true) :
    LegalConfiguration model active := by
  simpa [validateConfiguration, LegalConfiguration, DocumentOrdered] using
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

theorem runMacrostep_quanta_bounded (execute : RuntimeQuantum)
    (fuel : Nat) (allowExternal : Bool) (queues : RuntimeQueues) :
    (runMacrostep execute fuel allowExternal queues).quanta ≤ fuel := by
  induction fuel generalizing allowExternal queues with
  | zero => simp [runMacrostep]
  | succ fuel inductionHypothesis =>
      simp only [runMacrostep]
      cases popRuntimeTrigger allowExternal queues with
      | none => simp
      | some pair =>
          rcases pair with ⟨trigger, remaining⟩
          simp only
          have bounded := inductionHypothesis
            (allowExternal && !trigger.isExternal)
            (execute trigger remaining)
          omega

theorem popRuntimeTrigger_priority {allowExternal : Bool}
    {queues remaining : RuntimeQueues} {trigger : RuntimeTrigger}
    (popped : popRuntimeTrigger allowExternal queues =
      some (trigger, remaining)) :
    RuntimePriorityStep allowExternal queues trigger remaining := by
  rcases queues with ⟨eventless, internal, completions, external⟩
  cases eventless with
  | cons id tail =>
      have pair_eq :
          (RuntimeTrigger.eventless id,
            RuntimeQueues.mk tail internal completions external) =
          (trigger, remaining) := by
        simpa [popRuntimeTrigger] using popped
      cases pair_eq
      exact .eventless _ _ _ _ _ _
  | nil =>
      cases internal with
      | cons id tail =>
          have pair_eq :
              (RuntimeTrigger.internal id,
                RuntimeQueues.mk [] tail completions external) =
              (trigger, remaining) := by
            simpa [popRuntimeTrigger] using popped
          cases pair_eq
          exact .internal _ _ _ _ _
      | nil =>
          cases completions with
          | cons id tail =>
              have pair_eq :
                  (RuntimeTrigger.completion id,
                    RuntimeQueues.mk [] [] tail external) =
                  (trigger, remaining) := by
                simpa [popRuntimeTrigger] using popped
              cases pair_eq
              exact .completion _ _ _ _
          | nil =>
              cases allowExternal with
              | false => simp [popRuntimeTrigger] at popped
              | true =>
                  cases external with
                  | nil => simp [popRuntimeTrigger] at popped
                  | cons id tail =>
                      have pair_eq :
                          (RuntimeTrigger.external id,
                            RuntimeQueues.mk [] [] [] tail) =
                          (trigger, remaining) := by
                        simpa [popRuntimeTrigger] using popped
                      cases pair_eq
                      exact .external _ _

theorem runMacrostep_trace_length (execute : RuntimeQuantum)
    (fuel : Nat) (allowExternal : Bool) (queues : RuntimeQueues) :
    (runMacrostep execute fuel allowExternal queues).trace.length =
      (runMacrostep execute fuel allowExternal queues).quanta := by
  induction fuel generalizing allowExternal queues with
  | zero => simp [runMacrostep]
  | succ fuel inductionHypothesis =>
      simp only [runMacrostep]
      cases popRuntimeTrigger allowExternal queues with
      | none => simp
      | some pair =>
          rcases pair with ⟨trigger, remaining⟩
          simp only [List.length_cons]
          rw [inductionHypothesis]

theorem runMacrostep_no_external_when_disallowed
    (execute : RuntimeQuantum) (fuel : Nat) (queues : RuntimeQueues) :
    externalCardinality (runMacrostep execute fuel false queues).trace = 0 := by
  induction fuel generalizing queues with
  | zero => simp [runMacrostep, externalCardinality]
  | succ fuel inductionHypothesis =>
      simp only [runMacrostep]
      cases popped : popRuntimeTrigger false queues with
      | none => simp [externalCardinality]
      | some pair =>
          rcases pair with ⟨trigger, remaining⟩
          have priority := popRuntimeTrigger_priority popped
          cases priority with
          | eventless _ id tail internal completions external =>
              simp [externalCardinality, RuntimeTrigger.isExternal,
                    inductionHypothesis]
          | internal _ id tail completions external =>
              simp [externalCardinality, RuntimeTrigger.isExternal,
                    inductionHypothesis]
          | completion _ id tail external =>
              simp [externalCardinality, RuntimeTrigger.isExternal,
                    inductionHypothesis]

theorem runMacrostep_external_cardinality_le_one
    (execute : RuntimeQuantum) (fuel : Nat) (allowExternal : Bool)
    (queues : RuntimeQueues) :
    externalCardinality
      (runMacrostep execute fuel allowExternal queues).trace ≤ 1 := by
  induction fuel generalizing allowExternal queues with
  | zero => simp [runMacrostep, externalCardinality]
  | succ fuel inductionHypothesis =>
      simp only [runMacrostep]
      cases popped : popRuntimeTrigger allowExternal queues with
      | none => simp [externalCardinality]
      | some pair =>
          rcases pair with ⟨trigger, remaining⟩
          have priority := popRuntimeTrigger_priority popped
          cases priority with
          | eventless allow id tail internal completions external =>
              simpa [externalCardinality, RuntimeTrigger.isExternal] using
                inductionHypothesis allowExternal
                  (execute (.eventless id)
                    ⟨tail, internal, completions, external⟩)
          | internal allow id tail completions external =>
              simpa [externalCardinality, RuntimeTrigger.isExternal] using
                inductionHypothesis allowExternal
                  (execute (.internal id)
                    ⟨[], tail, completions, external⟩)
          | completion allow id tail external =>
              simpa [externalCardinality, RuntimeTrigger.isExternal] using
                inductionHypothesis allowExternal
                  (execute (.completion id) ⟨[], [], tail, external⟩)
          | external id tail =>
              simp [externalCardinality, RuntimeTrigger.isExternal,
                    runMacrostep_no_external_when_disallowed]

/-- The trace of a nonempty macrostep is the current quantum's committed trace
    followed by the remaining finite macrostep trace. -/
theorem runMacrostep_trace_cons (execute : RuntimeQuantum) (fuel : Nat)
    (allowExternal : Bool) (queues remaining next : RuntimeQueues)
    (trigger : RuntimeTrigger)
    (popped : popRuntimeTrigger allowExternal queues =
      some (trigger, remaining))
    (executed : execute trigger remaining = next) :
    (runMacrostep execute (fuel + 1) allowExternal queues).trace =
      trigger ::
        (runMacrostep execute fuel
          (allowExternal && !trigger.isExternal) next).trace := by
  simp [runMacrostep, popped, executed]

/-- Validation-gated C correspondence, analogous to Machine runtime trace
    refinement: the Boolean checker recomputes the finite macrostep rather than
    accepting a circular refinement assumption. -/
theorem c_macrostep_row_sound {execute : RuntimeQuantum}
    {row : CMacrostepRow}
    (checked : checkCMacrostepRow execute row = true) :
    CMacrostepRowRefines execute row := by
  simpa [checkCMacrostepRow, CMacrostepRowRefines] using checked

end CMetaCFlowCalculus.CFlow.Statechart
