module
public import CMeta.PreprocessorBackendRegistryEquivalence
public import CMeta.PreprocessorBackendSelection
import all CMeta.PreprocessorBackendRegistryEquivalence
import all CMeta.PreprocessorBackendSelection
import all CMeta.PreprocessorBackend

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
theorem supportingCandidates_nodup
    (registry : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR) :
    (registry.supportingCandidates query ir).Nodup := by
  have hentries : registry.entries.Nodup :=
    entries_nodup_of_keys_nodup registry.entries registry.uniqueKeys
  exact hentries.filter _

/-- A nonempty registry is exactly a fresh insertion of its head into the tail
    registry. This lets lookup correctness reuse the verified public mutation
    laws instead of unfolding the private list lookup implementation. -/
private theorem insert_head_tail
    (head : CertifiedPreprocessorBackend)
    (tail : List CertifiedPreprocessorBackend)
    (hkeys : (List.map CertifiedPreprocessorBackend.key (head :: tail)).Nodup) :
    ({ entries := tail
       uniqueKeys := (List.nodup_cons.mp hkeys).2 } : PreprocessorBackendRegistry).insert head =
      some { entries := head :: tail, uniqueKeys := hkeys } := by
  have hfresh := (List.nodup_cons.mp hkeys).1
  simp [insert, hfresh]

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
      let tailRegistry : PreprocessorBackendRegistry :=
        { entries := tail, uniqueKeys := hparts.2 }
      let currentRegistry : PreprocessorBackendRegistry :=
        { entries := head :: tail, uniqueKeys := hkeys }
      have hinsert : tailRegistry.insert head = some currentRegistry := by
        exact insert_head_tail head tail hkeys
      rcases List.mem_cons.mp hmem with hhere | htail
      · subst backend
        exact lookup_insert_self_exact tailRegistry currentRegistry head hinsert
      · have hne : head.key ≠ backend.key := by
          intro heq
          apply hparts.1
          have hbackendKey :
              backend.key ∈ tail.map CertifiedPreprocessorBackend.key :=
            List.mem_map.mpr ⟨backend, htail, rfl⟩
          simpa [heq] using hbackendKey
        have htailLookup : tailRegistry.lookup backend.key = some backend :=
          ih backend hparts.2 htail
        calc
          currentRegistry.lookup backend.key = tailRegistry.lookup backend.key :=
            lookup_insert_ne_exact
              tailRegistry head currentRegistry backend.key hinsert hne
          _ = some backend := htailLookup

/-- Exact lookup can only return an entry already stored in the registry. -/
private theorem mem_entries_of_lookup_eq_some
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (hlookup : registry.lookup backend.key = some backend) :
    backend ∈ registry.entries := by
  rcases registry with ⟨entries, hkeys⟩
  induction entries generalizing backend with
  | nil =>
      have hnone :
          ({ entries := [], uniqueKeys := hkeys } : PreprocessorBackendRegistry).lookup
              backend.key = none :=
        lookup_eq_none_of_key_not_mem
          { entries := [], uniqueKeys := hkeys } backend.key (by simp)
      rw [hlookup] at hnone
      cases hnone
  | cons head tail ih =>
      have hparts := List.nodup_cons.mp hkeys
      let tailRegistry : PreprocessorBackendRegistry :=
        { entries := tail, uniqueKeys := hparts.2 }
      let currentRegistry : PreprocessorBackendRegistry :=
        { entries := head :: tail, uniqueKeys := hkeys }
      have hinsert : tailRegistry.insert head = some currentRegistry := by
        exact insert_head_tail head tail hkeys
      by_cases hkey : head.key = backend.key
      · have hheadLookup : currentRegistry.lookup head.key = some head :=
          lookup_insert_self_exact tailRegistry currentRegistry head hinsert
        have hsome : some head = some backend := by
          calc
            some head = currentRegistry.lookup head.key := hheadLookup.symm
            _ = currentRegistry.lookup backend.key := congrArg currentRegistry.lookup hkey
            _ = some backend := hlookup
        have heq : head = backend := Option.some.inj hsome
        subst backend
        exact List.mem_cons_self
      · have hframe :
            currentRegistry.lookup backend.key = tailRegistry.lookup backend.key :=
          lookup_insert_ne_exact
            tailRegistry head currentRegistry backend.key hinsert hkey
        have htailLookup : tailRegistry.lookup backend.key = some backend := by
          rw [← hframe]
          exact hlookup
        exact List.mem_cons_of_mem head (ih backend hparts.2 htailLookup)

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

/-- One direction of candidate substitutability. It is kept non-recursive so
    symmetry of registry equivalence stays a theorem application rather than a
    recursive definition. -/
private theorem mem_supportingCandidates_of_equivalent
    (left right : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (heq : Equivalent left right)
    (backend : CertifiedPreprocessorBackend)
    (hleftCandidate : backend ∈ left.supportingCandidates query ir) :
    backend ∈ right.supportingCandidates query ir := by
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
        unfold observe at hrightObserve
        rw [hrightLookup] at hrightObserve
        simp only [Option.map_some, Option.some.injEq] at hrightObserve
        exact hrightObserve
      have hcertified : rightBackend = backend :=
        certified_eq_of_backend_eq rightBackend backend hpayload
      subst rightBackend
      have hrightMem : backend ∈ right.entries :=
        mem_entries_of_lookup_eq_some right backend hrightLookup
      exact (right.mem_supportingCandidates_iff query ir backend).2
        ⟨hrightMem, hleftParts.2⟩

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
  · exact mem_supportingCandidates_of_equivalent
      left right query ir heq backend
  · exact mem_supportingCandidates_of_equivalent
      right left query ir (equivalent_symm heq) backend

/-- Two duplicate-free lists with the same membership relation are permutations.
    This is the Lean 4.30-compatible extensional permutation principle used by
    registry candidate discovery. -/
private theorem perm_of_nodup_mem_iff
    {α : Type}
    {left right : List α}
    (hleft : left.Nodup)
    (hright : right.Nodup)
    (hmem : ∀ value, value ∈ left ↔ value ∈ right) :
    left.Perm right := by
  induction left generalizing right with
  | nil =>
      cases right with
      | nil => exact List.Perm.nil
      | cons head tail =>
          have himpossible : head ∈ ([] : List α) :=
            (hmem head).2 (by simp)
          simp at himpossible
  | cons head tail ih =>
      have hleftParts := List.nodup_cons.mp hleft
      have hheadRight : head ∈ right := (hmem head).1 (by simp)
      obtain ⟨before, after, rfl⟩ := List.append_of_mem hheadRight
      have hmiddle :
          (before ++ head :: after).Perm (head :: (before ++ after)) :=
        List.perm_middle
      have hrightReorderedNodup : (head :: (before ++ after)).Nodup :=
        hmiddle.nodup hright
      have hrightParts := List.nodup_cons.mp hrightReorderedNodup
      have htailMem : ∀ value, value ∈ tail ↔ value ∈ before ++ after := by
        intro value
        by_cases hvalue : value = head
        · subst value
          simp [hleftParts.1, hrightParts.1]
        · have hfull := hmem value
          simpa [hvalue] using hfull
      have htailPerm : tail.Perm (before ++ after) :=
        ih hleftParts.2 hrightParts.2 htailMem
      exact (htailPerm.cons head).trans hmiddle.symm

/-- Observationally equivalent finite maps expose the same supporting certified
    candidate multiset. List order remains a representation detail. -/
theorem supportingCandidates_perm_of_equivalent
    (left right : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (heq : Equivalent left right) :
    (left.supportingCandidates query ir).Perm
      (right.supportingCandidates query ir) := by
  exact perm_of_nodup_mem_iff
    (supportingCandidates_nodup left query ir)
    (supportingCandidates_nodup right query ir)
    (mem_supportingCandidates_iff_of_equivalent left right query ir heq)

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
  · intro backend hcandidate
    exact (left.mem_supportingCandidates_iff query ir backend).1 hcandidate |>.2.1
  · intro backend hcandidate
    exact (right.mem_supportingCandidates_iff query ir backend).1 hcandidate |>.2.1

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
