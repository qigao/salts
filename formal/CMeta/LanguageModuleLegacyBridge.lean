module
public import CMeta.PreprocessorBackend
public import CMeta.PreprocessorBackendSelection
import all CMeta.PreprocessorBackend
import all CMeta.PreprocessorBackendSelection

/-!
# Temporary legacy module bridge

This file exists only while the remaining pre-module conformance files still
consume proof theorems that must not be re-exported through `LanguageSpec`.
M7g moduleizes those consumers and deletes this bridge.
-/

namespace CMeta
namespace Producer
namespace LanguageModuleLegacyBridge

/-- TEMP-MODULE-BRIDGE(M7g): legacy `CPreprocessorBackendConformance` candidate proof. -/
public theorem mem_supportingCandidates_iff
    (registry : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (backend : CertifiedPreprocessorBackend) :
    backend ∈ registry.supportingCandidates query ir ↔
      backend ∈ registry.entries ∧
      backend.matchesQuery query ∧
      backend.supportsReplay ir :=
  PreprocessorBackendRegistry.mem_supportingCandidates_iff
    registry query ir backend

/-- TEMP-MODULE-BRIDGE(M7g): legacy `CPreprocessorBackendConformance` selection proof. -/
public theorem select_mem
    (policy : BackendSelectionPolicy)
    (candidates : List CertifiedPreprocessorBackend)
    (backend : CertifiedPreprocessorBackend)
    (h : policy.select candidates = some backend) :
    backend ∈ candidates :=
  BackendSelectionPolicy.select_mem policy candidates backend h

end LanguageModuleLegacyBridge
end Producer
end CMeta
