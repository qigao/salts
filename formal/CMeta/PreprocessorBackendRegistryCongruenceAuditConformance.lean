module
import all CMeta.PreprocessorBackendRegistrySetoid

/-!
# Registry congruence API audit conformance

The concrete registry remains executable. This audit fixes the relation used by
each public semantic operation instead of assuming ordinary equality everywhere:

* exact payload observation -> `Eq`
* candidate discovery -> `List.Perm`
* well-formed selected identity / lowering -> `Eq`
* total removal -> registry `≈`
* partial insert / replace -> `MutationResultEquivalent`

Raw `entries` order and arbitrary order-sensitive selection policies are
intentionally outside this congruence surface.
-/

namespace CMeta
namespace Producer

/-- Total removal is a proper operation on the registry Setoid. -/
theorem CPreprocessorBackendRegistryCongruenceAuditConformance.remove
    (left right : PreprocessorBackendRegistry)
    (key : BackendKey)
    (heq : left ≈ right) :
    left.remove key ≈ right.remove key := by
  exact PreprocessorBackendRegistry.remove_congr heq key

/-- Partial insertion uses a lifted relation on `Option Registry`: both sides
    reject together, or both succeed with observationally equivalent registries. -/
theorem CPreprocessorBackendRegistryCongruenceAuditConformance.insert
    (left right : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (heq : left ≈ right) :
    PreprocessorBackendRegistry.MutationResultEquivalent
      (left.insert backend) (right.insert backend) := by
  exact PreprocessorBackendRegistry.insert_congr heq backend

/-- Replacement has the same lifted partial-result semantics as insertion. -/
theorem CPreprocessorBackendRegistryCongruenceAuditConformance.replace
    (left right : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (heq : left ≈ right) :
    PreprocessorBackendRegistry.MutationResultEquivalent
      (left.replace backend) (right.replace backend) := by
  exact PreprocessorBackendRegistry.replace_congr heq backend

/-- Read-side relations stay intentionally heterogeneous: candidates are a
    permutation while selected identity and lowering are ordinary equality. -/
theorem CPreprocessorBackendRegistryCongruenceAuditConformance.readSide
    (wellFormed : WellFormedSelectionPolicy)
    (left right : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (key : BackendKey)
    (heq : left ≈ right) :
    left.observe key = right.observe key ∧
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
  · exact PreprocessorBackendRegistry.observe_congr heq key
  constructor
  · exact PreprocessorBackendRegistry.supportingCandidates_congr heq query ir
  constructor
  · exact PreprocessorBackendRegistry.selectSupporting_key_congr
      wellFormed heq query ir
  · exact PreprocessorBackendRegistry.selectSupporting_lowering_congr
      wellFormed heq query ir

end Producer
end CMeta
