import CMeta.PreprocessorBackendRegistrySubstitutability

/-!
# Registry observational substitutability conformance

Observationally equivalent registries must be interchangeable above the exact
lookup layer. Candidate discovery may differ in list representation order, but
it must preserve the same certified candidate multiset; a well-formed selection
policy must therefore choose the same backend identity, and selected replay
lowering must remain observationally identical.
-/

namespace CMeta
namespace Producer

/-- Equivalent finite maps expose the same supporting candidates up to list
    permutation. This is the bridge from exact lookup equivalence to discovery. -/
theorem CPreprocessorBackendRegistrySubstitutabilityConformance.candidates_perm
    (left right : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (heq : PreprocessorBackendRegistry.Equivalent left right) :
    (left.supportingCandidates query ir).Perm
      (right.supportingCandidates query ir) := by
  exact PreprocessorBackendRegistry.supportingCandidates_perm_of_equivalent
    left right query ir heq

/-- A well-formed policy cannot distinguish observationally equivalent
    registries by selected backend identity. -/
theorem CPreprocessorBackendRegistrySubstitutabilityConformance.selection_key
    (wellFormed : WellFormedSelectionPolicy)
    (left right : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (heq : PreprocessorBackendRegistry.Equivalent left right) :
    (left.selectSupporting wellFormed.policy query ir).map
        CertifiedPreprocessorBackend.key =
      (right.selectSupporting wellFormed.policy query ir).map
        CertifiedPreprocessorBackend.key := by
  exact PreprocessorBackendRegistry.selectSupporting_key_eq_of_equivalent
    wellFormed left right query ir heq

/-- Equivalent registries are indistinguishable by the selected replay lowering
    result. `none` remains `none`; every successful selection lowers to the same
    canonical plan for the requested IR. -/
theorem CPreprocessorBackendRegistrySubstitutabilityConformance.lowering
    (wellFormed : WellFormedSelectionPolicy)
    (left right : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (heq : PreprocessorBackendRegistry.Equivalent left right) :
    (left.selectSupporting wellFormed.policy query ir).map (fun backend =>
        lowerReplayBackendPlan backend.replayCapability ir) =
      (right.selectSupporting wellFormed.policy query ir).map (fun backend =>
        lowerReplayBackendPlan backend.replayCapability ir) := by
  exact PreprocessorBackendRegistry.selectSupporting_lowering_eq_of_equivalent
    wellFormed left right query ir heq

end Producer
end CMeta