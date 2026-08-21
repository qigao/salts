module
public import CMeta.PreprocessorBackendSelection
import all CMeta.PreprocessorBackendSelection
import all CMeta.PreprocessorBackend

/-!
# Certified preprocessor backend registry mutation laws

The primitive finite-map updates live with `PreprocessorBackendRegistry` itself.
This module states their cross-layer consequences for compatibility queries and
selection, keeping mutation mechanics separate from discovery and ranking.
-/

namespace CMeta
namespace Producer
namespace PreprocessorBackendRegistry

/-- Inserting a certified backend outside one compatibility class leaves that
    class's supporting candidate list exactly unchanged. This is stronger than a
    permutation statement: the observable candidate representation is identical. -/
-- TEMP-MODULE-BRIDGE(M7g): legacy PreprocessorBackendRegistryMutationConformance
public theorem supportingCandidates_insert_irrelevant
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (inserted : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (hinsert : registry.insert backend = some inserted)
    (hirrelevant : ¬ backend.matchesQuery query) :
    inserted.supportingCandidates query ir =
      registry.supportingCandidates query ir := by
  unfold insert at hinsert
  split at hinsert
  · simp at hinsert
  · simp only [Option.some.injEq] at hinsert
    subst inserted
    have hfamily : backend.backend.compilerFamily ≠ query.family := by
      intro hsame
      apply hirrelevant
      refine ⟨hsame, ?_⟩
      cases backend.backend.languageMode
      cases query.languageMode
      rfl
    simp [supportingCandidates, hfamily]

/-- Selection has no mutation-specific behavior: if insertion leaves candidate
    discovery unchanged, every selection policy receives the same input and must
    therefore return the same result. -/
-- TEMP-MODULE-BRIDGE(M7g): legacy PreprocessorBackendRegistryMutationConformance
public theorem selectSupporting_insert_irrelevant
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (inserted : PreprocessorBackendRegistry)
    (policy : BackendSelectionPolicy)
    (query : BackendQuery)
    (ir : ReplayIR)
    (hinsert : registry.insert backend = some inserted)
    (hirrelevant : ¬ backend.matchesQuery query) :
    inserted.selectSupporting policy query ir =
      registry.selectSupporting policy query ir := by
  unfold selectSupporting
  rw [supportingCandidates_insert_irrelevant
    registry backend inserted query ir hinsert hirrelevant]

end PreprocessorBackendRegistry
end Producer
end CMeta
