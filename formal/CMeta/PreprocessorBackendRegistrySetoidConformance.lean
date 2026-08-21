import CMeta.PreprocessorBackendRegistrySubstitutability

/-!
# Registry Setoid / congruence conformance

The registry remains a concrete finite-map structure, but upper formal layers
should reason through the established observational equivalence using standard
Setoid syntax. This conformance file intentionally does not construct a Quotient.
-/

namespace CMeta
namespace Producer

local instance registrySetoid : Setoid PreprocessorBackendRegistry :=
  PreprocessorBackendRegistry.registrySetoid

/-- The standard `≈` syntax is exactly the existing registry observational
    equivalence; registering a Setoid must not introduce a second relation. -/
theorem CPreprocessorBackendRegistrySetoidConformance.syntax
    (left right : PreprocessorBackendRegistry)
    (heq : PreprocessorBackendRegistry.Equivalent left right) :
    left ≈ right := by
  exact heq

/-- Exact-key payload observation is a congruent operation on registry Setoid
    values. -/
theorem CPreprocessorBackendRegistrySetoidConformance.observe
    (left right : PreprocessorBackendRegistry)
    (key : BackendKey)
    (heq : left ≈ right) :
    left.observe key = right.observe key := by
  exact PreprocessorBackendRegistry.observe_congr heq key

/-- Candidate discovery consumes only the registry Setoid class and therefore
    preserves candidate multisets up to permutation. -/
theorem CPreprocessorBackendRegistrySetoidConformance.candidates
    (left right : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (heq : left ≈ right) :
    (left.supportingCandidates query ir).Perm
      (right.supportingCandidates query ir) := by
  exact PreprocessorBackendRegistry.supportingCandidates_congr heq query ir

/-- Well-formed selection can be reasoned about directly from `≈`; callers do
    not need to unfold `Equivalent`. -/
theorem CPreprocessorBackendRegistrySetoidConformance.selection
    (wellFormed : WellFormedSelectionPolicy)
    (left right : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (heq : left ≈ right) :
    (left.selectSupporting wellFormed.policy query ir).map
        CertifiedPreprocessorBackend.key =
      (right.selectSupporting wellFormed.policy query ir).map
        CertifiedPreprocessorBackend.key := by
  exact PreprocessorBackendRegistry.selectSupporting_key_congr
    wellFormed heq query ir

/-- Selected replay lowering is a congruent observation of equivalent concrete
    registries. No quotient-backed runtime representation is required. -/
theorem CPreprocessorBackendRegistrySetoidConformance.lowering
    (wellFormed : WellFormedSelectionPolicy)
    (left right : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (heq : left ≈ right) :
    (left.selectSupporting wellFormed.policy query ir).map (fun backend =>
        lowerReplayBackendPlan backend.replayCapability ir) =
      (right.selectSupporting wellFormed.policy query ir).map (fun backend =>
        lowerReplayBackendPlan backend.replayCapability ir) := by
  exact PreprocessorBackendRegistry.selectSupporting_lowering_congr
    wellFormed heq query ir

end Producer
end CMeta
