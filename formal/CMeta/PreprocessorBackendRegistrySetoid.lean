import CMeta.PreprocessorBackendRegistrySubstitutability

/-!
# Registry Setoid / congruence interface

The concrete certified-backend registry remains the runtime/formal data
structure. This module only registers its already-proved observational
`Equivalent` relation as the standard Lean `Setoid` relation and exposes the
substitutability results through `≈`-based congruence theorems.

No `Quotient` representation is introduced here.
-/

namespace CMeta
namespace Producer
namespace PreprocessorBackendRegistry

/-- The standard Setoid relation for concrete registries is exactly the existing
    exact-key observational equivalence. This instance adds syntax, not a second
    semantic equality. -/
instance registrySetoid : Setoid PreprocessorBackendRegistry where
  r := Equivalent
  iseqv := ⟨equivalent_refl, equivalent_symm, equivalent_trans⟩

/-- Exact-key backend-payload observation respects registry Setoid equality. -/
theorem observe_congr
    {left right : PreprocessorBackendRegistry}
    (heq : left ≈ right)
    (key : BackendKey) :
    left.observe key = right.observe key := by
  exact heq key

/-- Policy-free candidate discovery respects registry Setoid equality up to
    permutation, which is the correct observation for list representation. -/
theorem supportingCandidates_congr
    {left right : PreprocessorBackendRegistry}
    (heq : left ≈ right)
    (query : BackendQuery)
    (ir : ReplayIR) :
    (left.supportingCandidates query ir).Perm
      (right.supportingCandidates query ir) := by
  exact supportingCandidates_perm_of_equivalent left right query ir heq

/-- Well-formed backend selection respects registry Setoid equality at the
    selected exact backend identity. -/
theorem selectSupporting_key_congr
    (wellFormed : WellFormedSelectionPolicy)
    {left right : PreprocessorBackendRegistry}
    (heq : left ≈ right)
    (query : BackendQuery)
    (ir : ReplayIR) :
    (left.selectSupporting wellFormed.policy query ir).map
        CertifiedPreprocessorBackend.key =
      (right.selectSupporting wellFormed.policy query ir).map
        CertifiedPreprocessorBackend.key := by
  exact selectSupporting_key_eq_of_equivalent
    wellFormed left right query ir heq

/-- Selected replay lowering respects registry Setoid equality. The result is
    still computed from concrete registry values; Setoid equality only provides
    the reasoning interface. -/
theorem selectSupporting_lowering_congr
    (wellFormed : WellFormedSelectionPolicy)
    {left right : PreprocessorBackendRegistry}
    (heq : left ≈ right)
    (query : BackendQuery)
    (ir : ReplayIR) :
    (left.selectSupporting wellFormed.policy query ir).map (fun backend =>
        lowerReplayBackendPlan backend.replayCapability ir) =
      (right.selectSupporting wellFormed.policy query ir).map (fun backend =>
        lowerReplayBackendPlan backend.replayCapability ir) := by
  exact selectSupporting_lowering_eq_of_equivalent
    wellFormed left right query ir heq

/-- Semantic relation for partial registry mutations. Failure is related only to
    failure; successful results are compared with registry observational
    equivalence rather than concrete list/proof-field equality. -/
def MutationResultEquivalent :
    Option PreprocessorBackendRegistry →
      Option PreprocessorBackendRegistry → Prop
  | none, none => True
  | some left, some right => left ≈ right
  | _, _ => False

/-- Exact key presence is preserved by observationally equivalent registries.
    We derive presence from the already-proved candidate permutation at the
    zero-depth `emit` IR, so no concrete list ordering enters the proof. -/
private theorem key_mem_of_equivalent
    {left right : PreprocessorBackendRegistry}
    (heq : Equivalent left right)
    (key : BackendKey)
    (hmem : key ∈ left.entries.map CertifiedPreprocessorBackend.key) :
    key ∈ right.entries.map CertifiedPreprocessorBackend.key := by
  obtain ⟨backend, hentry, hkey⟩ := List.mem_map.mp hmem
  subst key
  let query : BackendQuery :=
    { family := backend.backend.compilerFamily
      languageMode := backend.backend.languageMode }
  have hsupports : backend.supportsReplay .emit := by
    change ReplayIR.sameProducerDepth .emit ≤
      backend.replayCapability.certifiedSameProducerDepth
    change 0 ≤ backend.replayCapability.certifiedSameProducerDepth
    exact Nat.zero_le _
  have hcandidate :
      backend ∈ left.supportingCandidates query .emit := by
    exact (left.mem_supportingCandidates_iff query .emit backend).2
      ⟨hentry, ⟨rfl, rfl⟩, hsupports⟩
  have hperm :
      (left.supportingCandidates query .emit).Perm
        (right.supportingCandidates query .emit) :=
    supportingCandidates_perm_of_equivalent left right query .emit heq
  have hrightCandidate :
      backend ∈ right.supportingCandidates query .emit :=
    (List.Perm.mem_iff hperm).1 hcandidate
  have hrightEntry :=
    (right.mem_supportingCandidates_iff query .emit backend).1 hrightCandidate
  exact List.mem_map.mpr ⟨backend, hrightEntry.1, rfl⟩

private theorem key_mem_congr
    {left right : PreprocessorBackendRegistry}
    (heq : left ≈ right)
    (key : BackendKey) :
    key ∈ left.entries.map CertifiedPreprocessorBackend.key ↔
      key ∈ right.entries.map CertifiedPreprocessorBackend.key := by
  constructor
  · exact key_mem_of_equivalent heq key
  · exact key_mem_of_equivalent (equivalent_symm heq) key

/-- Equivalent registries agree on whether exact lookup is absent. Replacement
    only needs this shape property; the existing payload is not consumed by the
    replacement operation. -/
private theorem lookup_none_congr
    {left right : PreprocessorBackendRegistry}
    (heq : left ≈ right)
    (key : BackendKey) :
    left.lookup key = none ↔ right.lookup key = none := by
  have hobserve := heq key
  constructor
  · intro hleft
    cases hright : right.lookup key with
    | none => exact hright
    | some backend =>
        unfold observe at hobserve
        rw [hleft, hright] at hobserve
        simp at hobserve
  · intro hright
    cases hleft : left.lookup key with
    | none => exact hleft
    | some backend =>
        unfold observe at hobserve
        rw [hleft, hright] at hobserve
        simp at hobserve

/-- Total removal is a proper operation on the registry Setoid. -/
theorem remove_congr
    {left right : PreprocessorBackendRegistry}
    (heq : left ≈ right)
    (key : BackendKey) :
    left.remove key ≈ right.remove key := by
  intro observedKey
  rw [observe_remove left key observedKey,
      observe_remove right key observedKey]
  by_cases htarget : key = observedKey
  · simp [htarget]
  · simp only [if_neg htarget]
    exact heq observedKey

/-- Fresh insertion is congruent as a partial mutation: equivalent registries
    reject the same duplicate exact keys, and successful inserts are equivalent
    finite-map updates at the inserted key. -/
theorem insert_congr
    {left right : PreprocessorBackendRegistry}
    (heq : left ≈ right)
    (backend : CertifiedPreprocessorBackend) :
    MutationResultEquivalent (left.insert backend) (right.insert backend) := by
  have hkeys := key_mem_congr heq backend.key
  cases hleft : left.insert backend with
  | none =>
      have hleftPresent :
          backend.key ∈ left.entries.map CertifiedPreprocessorBackend.key :=
        (insert_eq_none_iff left backend).1 hleft
      have hrightPresent :
          backend.key ∈ right.entries.map CertifiedPreprocessorBackend.key :=
        hkeys.1 hleftPresent
      have hright : right.insert backend = none :=
        (insert_eq_none_iff right backend).2 hrightPresent
      rw [hleft, hright]
      trivial
  | some leftInserted =>
      have hleftFresh :
          backend.key ∉ left.entries.map CertifiedPreprocessorBackend.key := by
        intro hmem
        have hnone : left.insert backend = none :=
          (insert_eq_none_iff left backend).2 hmem
        rw [hleft] at hnone
        cases hnone
      have hrightFresh :
          backend.key ∉ right.entries.map CertifiedPreprocessorBackend.key := by
        intro hmem
        exact hleftFresh (hkeys.2 hmem)
      cases hright : right.insert backend with
      | none =>
          have hrightPresent :
              backend.key ∈ right.entries.map CertifiedPreprocessorBackend.key :=
            (insert_eq_none_iff right backend).1 hright
          exact (hrightFresh hrightPresent).elim
      | some rightInserted =>
          rw [hleft, hright]
          change leftInserted ≈ rightInserted
          intro observedKey
          by_cases htarget : backend.key = observedKey
          · subst observedKey
            rw [observe_insert_self left leftInserted backend hleft,
                observe_insert_self right rightInserted backend hright]
          · calc
              leftInserted.observe observedKey = left.observe observedKey :=
                observe_insert_ne
                  left leftInserted backend observedKey hleft htarget
              _ = right.observe observedKey := heq observedKey
              _ = rightInserted.observe observedKey :=
                (observe_insert_ne
                  right rightInserted backend observedKey hright htarget).symm

/-- Replacement is the composition of a shape-preserving exact lookup check,
    total removal, and fresh insertion. Therefore it respects the same lifted
    partial-mutation relation as insertion. -/
theorem replace_congr
    {left right : PreprocessorBackendRegistry}
    (heq : left ≈ right)
    (backend : CertifiedPreprocessorBackend) :
    MutationResultEquivalent (left.replace backend) (right.replace backend) := by
  have hnone := lookup_none_congr heq backend.key
  unfold replace
  cases hleft : left.lookup backend.key with
  | none =>
      have hright : right.lookup backend.key = none := hnone.1 hleft
      rw [hleft, hright]
      trivial
  | some existing =>
      have hrightNotNone : right.lookup backend.key ≠ none := by
        intro hright
        have hleftNone : left.lookup backend.key = none := hnone.2 hright
        rw [hleft] at hleftNone
        cases hleftNone
      cases hright : right.lookup backend.key with
      | none => exact (hrightNotNone hright).elim
      | some rightExisting =>
          rw [hleft, hright]
          exact insert_congr (remove_congr heq backend.key) backend

end PreprocessorBackendRegistry
end Producer
end CMeta
