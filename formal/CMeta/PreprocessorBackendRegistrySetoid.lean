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

end PreprocessorBackendRegistry
end Producer
end CMeta
