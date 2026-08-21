import CMeta.PreprocessorBackendRegistrySetoid

/-!
# CMeta formal language specification

This module is the stable inference-rule facade over the executable CMeta
semantics. The first rule slice names the candidate/support judgments, canonical
lowering, observational equivalence, and the already-audited congruence surface.
No second implementation of registry or replay semantics is introduced here.
-/

namespace CMeta
namespace Producer
namespace LanguageSpec

/-- Compatibility-class judgment. -/
abbrev Matches
    (backend : CertifiedPreprocessorBackend) (query : BackendQuery) : Prop :=
  backend.matchesQuery query

/-- Replay-capability judgment. -/
abbrev Supports
    (backend : CertifiedPreprocessorBackend) (ir : ReplayIR) : Prop :=
  backend.supportsReplay ir

/-- Supporting-candidate judgment. -/
abbrev Candidate
    (registry : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (backend : CertifiedPreprocessorBackend) : Prop :=
  backend ∈ registry.supportingCandidates query ir

namespace Rule

/-- `[T-CANDIDATE]`: membership, query match and replay support form a candidate. -/
theorem candidate_intro
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (query : BackendQuery)
    (ir : ReplayIR)
    (hentry : backend ∈ registry.entries)
    (hmatch : Matches backend query)
    (hsupport : Supports backend ir) :
    Candidate registry query ir backend := by
  exact (registry.mem_supportingCandidates_iff query ir backend).2
    ⟨hentry, hmatch, hsupport⟩

/-- `[T-CANDIDATE-ELIM]`: candidate formation is an exact iff. -/
theorem candidate_elim
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (query : BackendQuery)
    (ir : ReplayIR)
    (h : Candidate registry query ir backend) :
    backend ∈ registry.entries ∧
      Matches backend query ∧
      Supports backend ir := by
  exact (registry.mem_supportingCandidates_iff query ir backend).1 h

/-- `[T-LOWER]`: every supporting backend lowers to the one canonical plan. -/
theorem lower_intro
    (backend : CertifiedPreprocessorBackend)
    (ir : ReplayIR)
    (hsupport : Supports backend ir) :
    lowerReplayBackendPlan backend.replayCapability ir =
      some (ReplayBackendPlan.fromIR ir) := by
  exact lowerReplayBackendPlan_eq_canonical_of_supports
    backend.replayCapability ir hsupport

/-- `[E-REFL]`. -/
theorem eq_refl (registry : PreprocessorBackendRegistry) : registry ≈ registry :=
  PreprocessorBackendRegistry.equivalent_refl registry

/-- `[E-SYM]`. -/
theorem eq_symm
    {left right : PreprocessorBackendRegistry}
    (h : left ≈ right) : right ≈ left :=
  PreprocessorBackendRegistry.equivalent_symm h

/-- `[E-TRANS]`. -/
theorem eq_trans
    {first second third : PreprocessorBackendRegistry}
    (h12 : first ≈ second)
    (h23 : second ≈ third) : first ≈ third :=
  PreprocessorBackendRegistry.equivalent_trans h12 h23

/-- `[E-CANDIDATES]`: discovery is congruent up to permutation. -/
theorem candidates_congr
    {left right : PreprocessorBackendRegistry}
    (heq : left ≈ right)
    (query : BackendQuery)
    (ir : ReplayIR) :
    (left.supportingCandidates query ir).Perm
      (right.supportingCandidates query ir) :=
  PreprocessorBackendRegistry.supportingCandidates_congr heq query ir

/-- `[E-SELECT]`: well-formed selection preserves selected exact identity. -/
theorem selection_congr
    (wellFormed : WellFormedSelectionPolicy)
    {left right : PreprocessorBackendRegistry}
    (heq : left ≈ right)
    (query : BackendQuery)
    (ir : ReplayIR) :
    (left.selectSupporting wellFormed.policy query ir).map
        CertifiedPreprocessorBackend.key =
      (right.selectSupporting wellFormed.policy query ir).map
        CertifiedPreprocessorBackend.key :=
  PreprocessorBackendRegistry.selectSupporting_key_congr
    wellFormed heq query ir

/-- `[E-LOWER]`: selected replay lowering is a congruent observation. -/
theorem lowering_congr
    (wellFormed : WellFormedSelectionPolicy)
    {left right : PreprocessorBackendRegistry}
    (heq : left ≈ right)
    (query : BackendQuery)
    (ir : ReplayIR) :
    (left.selectSupporting wellFormed.policy query ir).map (fun backend =>
        lowerReplayBackendPlan backend.replayCapability ir) =
      (right.selectSupporting wellFormed.policy query ir).map (fun backend =>
        lowerReplayBackendPlan backend.replayCapability ir) :=
  PreprocessorBackendRegistry.selectSupporting_lowering_congr
    wellFormed heq query ir

/-- `[E-REMOVE]`: total removal is a proper registry-Setoid operation. -/
theorem remove_congr
    {left right : PreprocessorBackendRegistry}
    (heq : left ≈ right)
    (key : BackendKey) :
    left.remove key ≈ right.remove key :=
  PreprocessorBackendRegistry.remove_congr heq key

/-- `[E-INSERT]`: insertion preserves partial-result semantics. -/
theorem insert_congr
    {left right : PreprocessorBackendRegistry}
    (heq : left ≈ right)
    (backend : CertifiedPreprocessorBackend) :
    PreprocessorBackendRegistry.MutationResultEquivalent
      (left.insert backend) (right.insert backend) :=
  PreprocessorBackendRegistry.insert_congr heq backend

/-- `[E-REPLACE]`: replacement preserves partial-result semantics. -/
theorem replace_congr
    {left right : PreprocessorBackendRegistry}
    (heq : left ≈ right)
    (backend : CertifiedPreprocessorBackend) :
    PreprocessorBackendRegistry.MutationResultEquivalent
      (left.replace backend) (right.replace backend) :=
  PreprocessorBackendRegistry.replace_congr heq backend

end Rule
end LanguageSpec
end Producer
end CMeta
