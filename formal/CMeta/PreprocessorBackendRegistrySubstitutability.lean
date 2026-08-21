import CMeta.PreprocessorBackendRegistryEquivalence

/-!
# Registry observational substitutability

Observational equivalence is the semantic boundary of the certified backend
registry. This module proves that policy-free candidate discovery, well-formed
selection, and replay lowering cannot distinguish two registries that agree at
every exact backend key.
-/

namespace CMeta
namespace Producer
namespace PreprocessorBackendRegistry

/-- Key uniqueness implies ordinary entry uniqueness. -/
private theorem entries_nodup_of_keys_nodup
    (entries : List CertifiedPreprocessorBackend)
    (hkeys : (entries.map CertifiedPreprocessorBackend.key).Nodup) :
    entries.Nodup := by
  induction entries with
  | nil =>
      exact List.nodup_nil
  | cons head tail ih =>
      have hparts := List.nodup_cons.mp hkeys
      apply List.nodup_cons.mpr
      constructor
      · intro hmem
        apply hparts.1
        exact List.mem_map.mpr ⟨head, hmem, rfl⟩
      · exact ih hparts.2

/-- Candidate filtering preserves the registry's entry uniqueness. -/
private theorem supportingCandidates_nodup
    (registry : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR) :
    (registry.supportingCandidates query ir).Nodup := by
  have hentries : registry.entries.Nodup :=
    entries_nodup_of_keys_nodup registry.entries registry.uniqueKeys
  exact hentries.filter _

/-- Exact lookup returns an entry already present in the registry list. -/
private theorem mem_entries_of_lookup_eq_some
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (hlookup : registry.lookup backend.key = some backend) :
    backend ∈ registry.entries := by
  rcases registry with ⟨entries, hkeys⟩
  induction entries with
  | nil =>
      simp [PreprocessorBackendRegistry.lookup] at hlookup
  | cons head tail ih =>
      have hparts := List.nodup_cons.mp hkeys
      by_cases hkey : head.key = backend.key
      · have heq : head = backend := by
          have hsome : some head = some backend := by
            simpa [PreprocessorBackendRegistry.lookup, hkey] using hlookup
          exact Option.some.inj hsome
        subst backend
        exact List.mem_cons_self
      · have htailLookup :
          (PreprocessorBackendRegistry.lookup
            { entries := tail, uniqueKeys := hparts.2 } backend.key) =
            some backend := by
          simpa [PreprocessorBackendRegistry.lookup, hkey] using hlookup
        exact List.mem_cons_of_mem head (ih hparts.2 htailLookup)

/-- Every stored entry resolves at its own exact key. -/
private theorem lookup_eq_some_of_mem_entries
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (hmem : backend ∈ registry.entries) :
    registry.lookup backend.key = some backend := by
  rcases registry with ⟨entries, hkeys⟩
  induction entries generalizing backend with
  | nil =>
      simp at hmem
  | cons head tail ih =>
      have hparts := List.nodup_cons.mp hkeys
      rcases List.mem_cons.mp hmem with hhere | htail
      · subst backend
        simp [PreprocessorBackendRegistry.lookup]
      · have hne : head.key ≠ backend.key := by
          intro heq
          apply hparts.1
          have hbackendKey :
              backend.key ∈ tail.map CertifiedPreprocessorBackend.key :=
            List.mem_map.mpr ⟨backend, htail, rfl⟩
          simpa [heq] using hbackendKey
        have htailLookup :
            (PreprocessorBackendRegistry.lookup
              { entries := tail, uniqueKeys := hparts.2 } backend.key) =
              some backend :=
          ih hparts.2 backend htail
        simpa [PreprocessorBackendRegistry.lookup, hne] using htailLookup

/-- Certification proof identity is not semantic identity: equal observable
    backend payloads determine equal certified entries by proof irrelevance. -/
private theorem certified_eq_of_backend_eq
    (left right : CertifiedPreprocessorBackend)
    (hbackend : left.backend = right.backend) : left = right := by
  cases left with
  | mk leftBackend leftCertificate =>
      cases right with
      | mk rightBackend rightCertificate =>
          cases hbackend
          congr

/-- Equivalent registries have the same membership relation on supporting
    certified candidates. -/
private theorem mem_supportingCandidates_iff_of_equivalent
    (left right : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (heq : Equivalent left right)
    (backend : CertifiedPreprocessorBackend) :
    backend ∈ left.supportingCandidates query ir ↔
      backend ∈ right.supportingCandidates query ir := by
  constructor
  · intro hleftCandidate
    have hleftParts :=
      (left.mem_supportingCandidates_iff query ir backend).1 hleftCandidate
    have hleftLookup : left.lookup backend.key = some backend :=
      lookup_eq_some_of_mem_entries left backend hleftParts.1
    have hrightObserve : right.observe backend.key = some backend.backend := by
      rw [← heq backend.key]
      simp [observe, hleftLookup]
    cases hrightLookup : right.lookup backend.key with
    | none =>
        simp [observe, hrightLookup] at hrightObserve
    | some rightBackend =>
        have hpayload : rightBackend.backend = backend.backend := by
          simp [observe, hrightLookup] at hrightObserve
          exact hrightObserve
        have hcertified : rightBackend = backend :=
          certified_eq_of_backend_eq rightBackend backend hpayload
        have hrightMem : rightBackend ∈ right.entries :=
          mem_entries_of_lookup_eq_some right rightBackend hrightLookup
        subst rightBackend
        exact (right.mem_supportingCandidates_iff query ir backend).2
          ⟨hrightMem, hleftParts.2⟩
  · intro hrightCandidate
    exact (mem_supportingCandidates_iff_of_equivalent
      right left query ir (equivalent_symm heq) backend).1 hrightCandidate

/-- Observationally equivalent finite maps expose the same supporting certified
    candidate multiset. List order remains a representation detail. -/
theorem supportingCandidates_perm_of_equivalent
    (left right : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (heq : Equivalent left right) :
    (left.supportingCandidates query ir).Perm
      (right.supportingCandidates query ir) := by
  apply (List.perm_ext_iff_of_nodup
    (supportingCandidates_nodup left query ir)
    (supportingCandidates_nodup right query ir)).2
  intro backend
  exact mem_supportingCandidates_iff_of_equivalent
    left right query ir heq backend

/-- A well-formed policy cannot distinguish equivalent registries by selected
    backend identity. -/
theorem selectSupporting_key_eq_of_equivalent
    (wellFormed : WellFormedSelectionPolicy)
    (left right : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (heq : Equivalent left right) :
    (left.selectSupporting wellFormed.policy query ir).map
        CertifiedPreprocessorBackend.key =
      (right.selectSupporting wellFormed.policy query ir).map
        CertifiedPreprocessorBackend.key := by
  have hcandidates :=
    supportingCandidates_perm_of_equivalent left right query ir heq
  apply BackendSelectionPolicy.select_key_eq_of_perm_of_matches
    wellFormed query hcandidates
  · intro backend hmem
    exact (left.mem_supportingCandidates_iff query ir backend).1 hmem |>.2.1
  · intro backend hmem
    exact (right.mem_supportingCandidates_iff query ir backend).1 hmem |>.2.1

/-- Equivalent registries are indistinguishable by selected replay lowering.
    Empty candidate sets agree, while every successful selection lowers to the
    same canonical plan for the requested IR. -/
theorem selectSupporting_lowering_eq_of_equivalent
    (wellFormed : WellFormedSelectionPolicy)
    (left right : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (heq : Equivalent left right) :
    (left.selectSupporting wellFormed.policy query ir).map (fun backend =>
        lowerReplayBackendPlan backend.replayCapability ir) =
      (right.selectSupporting wellFormed.policy query ir).map (fun backend =>
        lowerReplayBackendPlan backend.replayCapability ir) := by
  have hkeys := selectSupporting_key_eq_of_equivalent
    wellFormed left right query ir heq
  cases hleft : left.selectSupporting wellFormed.policy query ir with
  | none =>
      cases hright : right.selectSupporting wellFormed.policy query ir with
      | none => rfl
      | some rightBackend =>
          rw [hleft, hright] at hkeys
          simp at hkeys
  | some leftBackend =>
      cases hright : right.selectSupporting wellFormed.policy query ir with
      | none =>
          rw [hleft, hright] at hkeys
          simp at hkeys
      | some rightBackend =>
          simp only [Option.map_some]
          rw [left.selectSupporting_lowering_canonical
            wellFormed.policy query ir leftBackend hleft]
          rw [right.selectSupporting_lowering_canonical
            wellFormed.policy query ir rightBackend hright]

end PreprocessorBackendRegistry
end Producer
end CMeta
