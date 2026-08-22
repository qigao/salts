module
import CMeta.PreprocessorBackend
public meta import CMeta.PreprocessorBackend
import all CMeta.PreprocessorBackend
import CMeta.NestedReplayBackendPlan
public meta import CMeta.NestedReplayBackendPlan
import all CMeta.NestedReplayBackendPlan
import CMeta.PreprocessorBackendSelection
public meta import CMeta.PreprocessorBackendSelection
import all CMeta.PreprocessorBackendSelection
import CMeta.NestedReplayGccGeneratedC
public meta import CMeta.NestedReplayGccGeneratedC
import CMeta.NestedReplayClangGeneratedC
public meta import CMeta.NestedReplayClangGeneratedC

/-!
# Preprocessor backend identity conformance

The nested-replay certificate must be attached to explicit compiler backends
rather than treated as a universal C11 fact. GCC and Clang are independently
witnessed and registered through the shared replay lowering abstraction.
-/

namespace CMeta
namespace Producer

private def gccBackend : PreprocessorBackend :=
  { compilerFamily := .gcc
    compilerMajorVersion := NestedReplayGccGeneratedC.compilerMajorVersion
    languageMode := .c11
    directSameProducerAccepted := NestedReplayGccGeneratedC.directSameProducerAccepted
    deferredSameProducerAccepted := NestedReplayGccGeneratedC.deferredSameProducerAccepted
    certifiedSameProducerDepth := NestedReplayGccGeneratedC.certifiedSameProducerDepth }

private def clangBackend : PreprocessorBackend :=
  { compilerFamily := .clang
    compilerMajorVersion := NestedReplayClangGeneratedC.compilerMajorVersion
    languageMode := .c11
    directSameProducerAccepted := NestedReplayClangGeneratedC.directSameProducerAccepted
    deferredSameProducerAccepted := NestedReplayClangGeneratedC.deferredSameProducerAccepted
    certifiedSameProducerDepth := NestedReplayClangGeneratedC.certifiedSameProducerDepth }

private def gccCertified : CertifiedPreprocessorBackend :=
  ⟨gccBackend, by native_decide⟩

private def clangCertified : CertifiedPreprocessorBackend :=
  ⟨clangBackend, by native_decide⟩

private def certifiedRegistry : PreprocessorBackendRegistry :=
  { entries := [gccCertified, clangCertified]
    uniqueKeys := by native_decide }

private def reentrantIR : ReplayIR :=
  .replay 1 (.replay 2 (.replay 1 .emit))

private def gccQuery : BackendQuery :=
  { family := .gcc
    languageMode := .c11 }

private def clangQuery : BackendQuery :=
  { family := .clang
    languageMode := .c11 }

private def replaySelectionPolicy : BackendSelectionPolicy :=
  BackendSelectionPolicy.preferGreaterCertifiedDepth.thenBy
    BackendSelectionPolicy.preferNewerVersion

/-- The generated GCC witness identifies its compiler family explicitly. -/
theorem CPreprocessorBackendConformance.gcc_family :
    NestedReplayGccGeneratedC.compilerFamilyTag = CompilerFamily.gcc.tag := by
  native_decide

/-- The generated Clang witness identifies its compiler family explicitly. -/
theorem CPreprocessorBackendConformance.clang_family :
    NestedReplayClangGeneratedC.compilerFamilyTag = CompilerFamily.clang.tag := by
  native_decide

/-- Both compiler certificates record concrete major versions. -/
theorem CPreprocessorBackendConformance.compiler_versions_are_concrete :
    0 < NestedReplayGccGeneratedC.compilerMajorVersion ∧
    0 < NestedReplayClangGeneratedC.compilerMajorVersion := by
  native_decide

/-- Exact strict-C11 mode is part of both backend identities. -/
theorem CPreprocessorBackendConformance.language_modes_are_c11 :
    NestedReplayGccGeneratedC.languageStandard = LanguageMode.c11.standardValue ∧
    NestedReplayClangGeneratedC.languageStandard = LanguageMode.c11.standardValue := by
  native_decide

/-- The registry contains only replay-certified backends by construction. -/
theorem CPreprocessorBackendConformance.registry_contains_two_certified_backends :
    certifiedRegistry.entries.length = 2 := by
  native_decide

/-- Certification exposes the replay capability without losing the surrounding
    compiler identity. -/
theorem CPreprocessorBackendConformance.certified_capability_projection :
    gccCertified.replayCapability = gccBackend.replayCapability ∧
    clangCertified.replayCapability = clangBackend.replayCapability := by
  constructor <;> rfl

/-- Both currently registered real compiler backends require deferred replay
    for active same-ID re-entry under strict C11. -/
theorem CPreprocessorBackendConformance.both_require_deferred :
    gccCertified.backend.requiresDeferred = true ∧
    clangCertified.backend.requiresDeferred = true := by
  native_decide

/-- The currently witnessed GCC and Clang entries expose the same certified
    replay capability envelope. -/
theorem CPreprocessorBackendConformance.capabilities_agree :
    gccCertified.replayCapability = clangCertified.replayCapability := by
  native_decide

/-- Successful lowering normalizes to the one canonical plan determined only by
    the replay IR once the selected backend supports that IR. -/
theorem CPreprocessorBackendConformance.reentry_normalizes_to_canonical_plan :
    lowerReplayBackendPlan gccCertified.replayCapability reentrantIR =
      some (ReplayBackendPlan.fromIR reentrantIR) := by
  exact lowerReplayBackendPlan_eq_canonical_of_supports
    gccCertified.replayCapability reentrantIR (by native_decide)

/-- Portability depends only on both certified backends supporting this IR, not
    on equality of their complete capability records. -/
theorem CPreprocessorBackendConformance.reentry_lowering_is_portable :
    lowerReplayBackendPlan gccCertified.replayCapability reentrantIR =
      lowerReplayBackendPlan clangCertified.replayCapability reentrantIR := by
  exact certifiedReplayLowering_eq_of_both_supports
    gccCertified clangCertified reentrantIR
    (by native_decide) (by native_decide)

/-- Registry lookup is keyed by compiler identity rather than list position. -/
theorem CPreprocessorBackendConformance.gcc_lookup_round_trip :
    (certifiedRegistry.lookup gccCertified.key).map
        CertifiedPreprocessorBackend.key = some gccCertified.key := by
  native_decide

/-- Resolving a registered backend and a supported replay IR returns the same
    canonical plan used by direct lowering. -/
theorem CPreprocessorBackendConformance.gcc_registry_resolve :
    certifiedRegistry.resolveReplay gccCertified.key reentrantIR =
      some (ReplayBackendPlan.fromIR reentrantIR) := by
  native_decide

/-- The registry resolver has one semantic contract: successful resolution is
    equivalent to finding the keyed certified backend, proving support for the
    requested IR, and returning its canonical replay plan. -/
theorem CPreprocessorBackendConformance.registry_resolve_contract :
    certifiedRegistry.resolveReplay gccCertified.key reentrantIR =
        some (ReplayBackendPlan.fromIR reentrantIR) ↔
      ∃ backend,
        certifiedRegistry.lookup gccCertified.key = some backend ∧
        backend.supportsReplay reentrantIR ∧
        ReplayBackendPlan.fromIR reentrantIR = ReplayBackendPlan.fromIR reentrantIR := by
  exact PreprocessorBackendRegistry.resolveReplay_eq_some_iff
    certifiedRegistry gccCertified.key reentrantIR (ReplayBackendPlan.fromIR reentrantIR)

/-- Capability discovery is deliberately broader than exact lookup: the query
    fixes compiler family and language mode, while version remains part of the
    returned backend identity. -/
theorem CPreprocessorBackendConformance.gcc_supporting_candidates :
    (certifiedRegistry.supportingCandidates gccQuery reentrantIR).map
        CertifiedPreprocessorBackend.key = [gccCertified.key] := by
  native_decide

/-- The same candidate mechanism is backend-family neutral. -/
theorem CPreprocessorBackendConformance.clang_supporting_candidates :
    (certifiedRegistry.supportingCandidates clangQuery reentrantIR).map
        CertifiedPreprocessorBackend.key = [clangCertified.key] := by
  native_decide

/-- Candidate discovery has no hidden selection policy: membership means exactly
    registry membership plus query match plus support for this replay IR. -/
theorem CPreprocessorBackendConformance.supporting_candidates_contract
    (backend : CertifiedPreprocessorBackend) :
    backend ∈ certifiedRegistry.supportingCandidates gccQuery reentrantIR ↔
      backend ∈ certifiedRegistry.entries ∧
      backend.matchesQuery gccQuery ∧
      backend.supportsReplay reentrantIR := by
  exact PreprocessorBackendRegistry.mem_supportingCandidates_iff
    certifiedRegistry gccQuery reentrantIR backend

/-- Selection policy cannot manufacture a backend: any successful result must
    come from the candidate list supplied by the registry. -/
theorem CPreprocessorBackendConformance.selection_result_is_candidate
    (backend : CertifiedPreprocessorBackend)
    (h : replaySelectionPolicy.select
      (certifiedRegistry.supportingCandidates gccQuery reentrantIR) = some backend) :
    backend ∈ certifiedRegistry.supportingCandidates gccQuery reentrantIR := by
  exact BackendSelectionPolicy.select_mem replaySelectionPolicy _ backend h

/-- The concrete replay policy uses compiler version only as a tie-break after
    certified replay depth. GCC and Clang currently expose equal depth four, so
    Clang 18 wins this deliberately cross-family policy fixture over GCC 13. -/
theorem CPreprocessorBackendConformance.newer_version_breaks_equal_depth_tie :
    (replaySelectionPolicy.select [gccCertified, clangCertified]).map
        CertifiedPreprocessorBackend.key = some clangCertified.key := by
  native_decide

/-- Registry-level selection preserves the support guarantee of candidate
    discovery, so selected lowering still normalizes to the IR's canonical plan. -/
theorem CPreprocessorBackendConformance.selected_backend_lowers_canonically
    (backend : CertifiedPreprocessorBackend)
    (h : certifiedRegistry.selectSupporting replaySelectionPolicy gccQuery reentrantIR =
      some backend) :
    lowerReplayBackendPlan backend.replayCapability reentrantIR =
      some (ReplayBackendPlan.fromIR reentrantIR) := by
  exact PreprocessorBackendRegistry.selectSupporting_lowering_canonical
    certifiedRegistry replaySelectionPolicy gccQuery reentrantIR backend h

end Producer
end CMeta
