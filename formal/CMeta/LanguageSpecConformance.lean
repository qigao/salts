import CMeta.LanguageSpec

/-!
# CMeta language rule conformance

This module treats the proved CMeta semantics as a small formal language.  It
requires a stable rule facade over the existing implementation theorems, then
checks that static judgments, dynamic lowering, observational equivalence and
mutation congruence compose without exposing representation details.
-/

namespace CMeta
namespace Producer

/-- Static candidate formation followed by backend-independent canonical lowering. -/
theorem CLanguageSpecConformance.staticAndDynamic
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (query : BackendQuery)
    (ir : ReplayIR)
    (hentry : backend ∈ registry.entries)
    (hmatch : LanguageSpec.Matches backend query)
    (hsupport : LanguageSpec.Supports backend ir) :
    LanguageSpec.Candidate registry query ir backend ∧
      lowerReplayBackendPlan backend.replayCapability ir =
        some (ReplayBackendPlan.fromIR ir) := by
  constructor
  · exact LanguageSpec.Rule.candidate_intro
      registry backend query ir hentry hmatch hsupport
  · exact LanguageSpec.Rule.lower_intro backend ir hsupport

/-- Candidate elimination exposes exactly registry membership, query match and support. -/
theorem CLanguageSpecConformance.candidateElim
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (query : BackendQuery)
    (ir : ReplayIR)
    (hcandidate : LanguageSpec.Candidate registry query ir backend) :
    backend ∈ registry.entries ∧
      LanguageSpec.Matches backend query ∧
      LanguageSpec.Supports backend ir := by
  exact LanguageSpec.Rule.candidate_elim
    registry backend query ir hcandidate

/-- Registry Setoid equivalence transports the complete read-side semantic surface. -/
theorem CLanguageSpecConformance.equivalenceAndCongruence
    (wellFormed : WellFormedSelectionPolicy)
    (left right : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (heq : left ≈ right) :
    (left.supportingCandidates query ir).Perm
        (right.supportingCandidates query ir) ∧
      (left.selectSupporting wellFormed.policy query ir).map
          CertifiedPreprocessorBackend.key =
        (right.selectSupporting wellFormed.policy query ir).map
          CertifiedPreprocessorBackend.key ∧
      (left.selectSupporting wellFormed.policy query ir).map (fun backend =>
          lowerReplayBackendPlan backend.replayCapability ir) =
        (right.selectSupporting wellFormed.policy query ir).map (fun backend =>
          lowerReplayBackendPlan backend.replayCapability ir) := by
  constructor
  · exact LanguageSpec.Rule.candidates_congr heq query ir
  constructor
  · exact LanguageSpec.Rule.selection_congr wellFormed heq query ir
  · exact LanguageSpec.Rule.lowering_congr wellFormed heq query ir

/-- The equivalence rules form the expected reflexive/symmetric/transitive calculus. -/
theorem CLanguageSpecConformance.equivalenceRules
    (first second third : PreprocessorBackendRegistry)
    (h12 : first ≈ second)
    (h23 : second ≈ third) :
    first ≈ first ∧ second ≈ first ∧ first ≈ third := by
  exact ⟨LanguageSpec.Rule.eq_refl first,
    LanguageSpec.Rule.eq_symm h12,
    LanguageSpec.Rule.eq_trans h12 h23⟩

/-- Total and partial mutations use their audited codomain relations. -/
theorem CLanguageSpecConformance.mutationRules
    (left right : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (key : BackendKey)
    (heq : left ≈ right) :
    left.remove key ≈ right.remove key ∧
      PreprocessorBackendRegistry.MutationResultEquivalent
        (left.insert backend) (right.insert backend) ∧
      PreprocessorBackendRegistry.MutationResultEquivalent
        (left.replace backend) (right.replace backend) := by
  exact ⟨LanguageSpec.Rule.remove_congr heq key,
    LanguageSpec.Rule.insert_congr heq backend,
    LanguageSpec.Rule.replace_congr heq backend⟩

end Producer
end CMeta
