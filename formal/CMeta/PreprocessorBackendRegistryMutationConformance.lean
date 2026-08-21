import CMeta.PreprocessorBackendRegistryMutation

/-!
# Preprocessor backend registry mutation conformance

Registry mutation must preserve the finite-map contract carried by `uniqueKeys`.
This module fixes the observable semantics of insert/replace/remove before their
implementation: exact lookup behavior, candidate discovery after capability
replacement, and selection non-interference for unrelated mutations.
-/

namespace CMeta
namespace Producer

private def gcc14Backend : PreprocessorBackend :=
  { compilerFamily := .gcc
    compilerMajorVersion := 14
    languageMode := .c11
    directSameProducerAccepted := false
    deferredSameProducerAccepted := true
    certifiedSameProducerDepth := 4 }

private def gcc14DeepBackend : PreprocessorBackend :=
  { compilerFamily := .gcc
    compilerMajorVersion := 14
    languageMode := .c11
    directSameProducerAccepted := false
    deferredSameProducerAccepted := true
    certifiedSameProducerDepth := 8 }

private def gcc15Backend : PreprocessorBackend :=
  { compilerFamily := .gcc
    compilerMajorVersion := 15
    languageMode := .c11
    directSameProducerAccepted := false
    deferredSameProducerAccepted := true
    certifiedSameProducerDepth := 8 }

private def clang18Backend : PreprocessorBackend :=
  { compilerFamily := .clang
    compilerMajorVersion := 18
    languageMode := .c11
    directSameProducerAccepted := false
    deferredSameProducerAccepted := true
    certifiedSameProducerDepth := 4 }

private def clang19Backend : PreprocessorBackend :=
  { compilerFamily := .clang
    compilerMajorVersion := 19
    languageMode := .c11
    directSameProducerAccepted := false
    deferredSameProducerAccepted := true
    certifiedSameProducerDepth := 8 }

private def gcc14 : CertifiedPreprocessorBackend :=
  ⟨gcc14Backend, by native_decide⟩

private def gcc14Deep : CertifiedPreprocessorBackend :=
  ⟨gcc14DeepBackend, by native_decide⟩

private def gcc15 : CertifiedPreprocessorBackend :=
  ⟨gcc15Backend, by native_decide⟩

private def clang18 : CertifiedPreprocessorBackend :=
  ⟨clang18Backend, by native_decide⟩

private def clang19 : CertifiedPreprocessorBackend :=
  ⟨clang19Backend, by native_decide⟩

private def baseRegistry : PreprocessorBackendRegistry :=
  { entries := [gcc14, clang18]
    uniqueKeys := by native_decide }

private def gccQuery : BackendQuery :=
  { family := .gcc
    languageMode := .c11 }

private def depth3IR : ReplayIR :=
  .replay 1 (.replay 2 (.replay 1 .emit))

private def depth5IR : ReplayIR :=
  .replay 1 (.replay 1 (.replay 1 (.replay 1 (.replay 1 .emit))))

private def replayPolicy : BackendSelectionPolicy :=
  BackendSelectionPolicy.preferGreaterCertifiedDepth.thenBy
    BackendSelectionPolicy.preferNewerVersion

/-- Fresh insert adds a new exact key and makes it immediately discoverable. -/
theorem CPreprocessorBackendRegistryMutationConformance.insert_lookup :
    (baseRegistry.insert gcc15).map (fun registry =>
      (registry.lookup gcc15.key).map CertifiedPreprocessorBackend.key) =
      some (some gcc15.key) := by
  native_decide

/-- Insert is partial: an already-present exact backend key is rejected. -/
theorem CPreprocessorBackendRegistryMutationConformance.insert_duplicate_rejected :
    baseRegistry.insert gcc14 = none := by
  native_decide

/-- Replace is partial but preserves identity: an existing key receives the new
    certified capability payload under that same key. -/
theorem CPreprocessorBackendRegistryMutationConformance.replace_lookup :
    (baseRegistry.replace gcc14Deep).map (fun registry =>
      (registry.lookup gcc14.key).map (fun backend =>
        backend.replayCapability.certifiedSameProducerDepth)) =
      some (some 8) := by
  native_decide

/-- Replacing a key that is not present is rejected rather than silently inserting. -/
theorem CPreprocessorBackendRegistryMutationConformance.replace_missing_rejected :
    baseRegistry.replace gcc15 = none := by
  native_decide

/-- Removing an exact key makes that key absent. -/
theorem CPreprocessorBackendRegistryMutationConformance.remove_lookup :
    (baseRegistry.remove gcc14.key).lookup gcc14.key = none := by
  native_decide

/-- Removing a missing key is a no-op at the observable lookup level. -/
theorem CPreprocessorBackendRegistryMutationConformance.remove_missing_noop :
    ((baseRegistry.remove gcc15.key).lookup clang18.key).map
        CertifiedPreprocessorBackend.key =
      (baseRegistry.lookup clang18.key).map CertifiedPreprocessorBackend.key := by
  native_decide

/-- Candidate discovery observes replacement capability, not stale registry data. -/
theorem CPreprocessorBackendRegistryMutationConformance.replace_updates_candidates :
    (baseRegistry.replace gcc14Deep).map (fun registry =>
      (registry.supportingCandidates gccQuery depth5IR).map
        CertifiedPreprocessorBackend.key) =
      some [gcc14Deep.key] := by
  native_decide

/-- A successful insert outside the queried compatibility class leaves candidate
    discovery unchanged by the production non-interference theorem. -/
theorem CPreprocessorBackendRegistryMutationConformance.unrelated_insert_preserves_candidates
    (inserted : PreprocessorBackendRegistry)
    (hinsert : baseRegistry.insert clang19 = some inserted) :
    inserted.supportingCandidates gccQuery depth3IR =
      baseRegistry.supportingCandidates gccQuery depth3IR := by
  exact PreprocessorBackendRegistry.supportingCandidates_insert_irrelevant
    baseRegistry clang19 inserted gccQuery depth3IR hinsert (by native_decide)

/-- Because candidate discovery is unchanged, an unrelated insert cannot perturb
    the result of any selection policy. -/
theorem CPreprocessorBackendRegistryMutationConformance.unrelated_insert_preserves_selection
    (inserted : PreprocessorBackendRegistry)
    (hinsert : baseRegistry.insert clang19 = some inserted) :
    inserted.selectSupporting replayPolicy gccQuery depth3IR =
      baseRegistry.selectSupporting replayPolicy gccQuery depth3IR := by
  exact PreprocessorBackendRegistry.selectSupporting_insert_irrelevant
    baseRegistry clang19 inserted replayPolicy gccQuery depth3IR hinsert (by native_decide)

/-- The production lookup frame law: successful insertion preserves every other key. -/
theorem CPreprocessorBackendRegistryMutationConformance.insert_frame
    (inserted : PreprocessorBackendRegistry)
    (hinsert : baseRegistry.insert gcc15 = some inserted) :
    (inserted.lookup clang18.key).map CertifiedPreprocessorBackend.key =
      (baseRegistry.lookup clang18.key).map CertifiedPreprocessorBackend.key := by
  exact PreprocessorBackendRegistry.lookup_insert_ne
    baseRegistry gcc15 inserted clang18.key hinsert (by native_decide)

/-- The production lookup frame law: replacement preserves every other key. -/
theorem CPreprocessorBackendRegistryMutationConformance.replace_frame
    (replaced : PreprocessorBackendRegistry)
    (hreplace : baseRegistry.replace gcc14Deep = some replaced) :
    (replaced.lookup clang18.key).map CertifiedPreprocessorBackend.key =
      (baseRegistry.lookup clang18.key).map CertifiedPreprocessorBackend.key := by
  exact PreprocessorBackendRegistry.lookup_replace_ne
    baseRegistry gcc14Deep replaced clang18.key hreplace (by native_decide)

/-- The production lookup frame law: removal preserves every non-target key. -/
theorem CPreprocessorBackendRegistryMutationConformance.remove_frame :
    ((baseRegistry.remove gcc14.key).lookup clang18.key).map
        CertifiedPreprocessorBackend.key =
      (baseRegistry.lookup clang18.key).map CertifiedPreprocessorBackend.key := by
  exact PreprocessorBackendRegistry.lookup_remove_ne
    baseRegistry gcc14.key clang18.key (by native_decide)

end Producer
end CMeta
