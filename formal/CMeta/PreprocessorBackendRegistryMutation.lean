import CMeta.PreprocessorBackendSelection

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
theorem supportingCandidates_insert_irrelevant
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
    simp [supportingCandidates, hirrelevant]

/-- Selection has no mutation-specific behavior: if insertion leaves candidate
    discovery unchanged, every selection policy receives the same input and must
    therefore return the same result. -/
theorem selectSupporting_insert_irrelevant
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
