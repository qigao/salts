module
import CMeta.PreprocessorBackendSelection
public meta import CMeta.PreprocessorBackendSelection
import all CMeta.PreprocessorBackendSelection

/-!
# Preprocessor backend selection conformance

Selection policy must be independent of registry list order once candidate
identity is constrained by one backend query. This module uses synthetic
same-family compiler versions only to exercise policy algebra; real compiler
certificates remain covered by the GCC/Clang conformance modules.
-/

namespace CMeta
namespace Producer

private def gcc14Backend : PreprocessorBackend :=
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

private def gcc16Backend : PreprocessorBackend :=
  { compilerFamily := .gcc
    compilerMajorVersion := 16
    languageMode := .c11
    directSameProducerAccepted := false
    deferredSameProducerAccepted := true
    certifiedSameProducerDepth := 4 }

private def gcc14 : CertifiedPreprocessorBackend :=
  ⟨gcc14Backend, by native_decide⟩

private def gcc15 : CertifiedPreprocessorBackend :=
  ⟨gcc15Backend, by native_decide⟩

private def gcc16 : CertifiedPreprocessorBackend :=
  ⟨gcc16Backend, by native_decide⟩

private def registryA : PreprocessorBackendRegistry :=
  { entries := [gcc14, gcc15, gcc16]
    uniqueKeys := by native_decide }

private def registryB : PreprocessorBackendRegistry :=
  { entries := [gcc16, gcc14, gcc15]
    uniqueKeys := by native_decide }

private def gccQuery : BackendQuery :=
  { family := .gcc
    languageMode := .c11 }

private def reentrantIR : ReplayIR :=
  .replay 1 (.replay 2 (.replay 1 .emit))

private def replayPolicy : BackendSelectionPolicy :=
  BackendSelectionPolicy.preferGreaterCertifiedDepth.thenBy
    BackendSelectionPolicy.preferNewerVersion

private def replayPolicyWellFormed : WellFormedSelectionPolicy :=
  BackendSelectionPolicy.replayWellFormed

private theorem registryEntriesPerm : registryA.entries.Perm registryB.entries := by
  change [gcc14, gcc15, gcc16].Perm [gcc16, gcc14, gcc15]
  simpa using
    (List.perm_append_comm
      (l₁ := [gcc14, gcc15])
      (l₂ := [gcc16]))

/-- The well-formed wrapper certifies the concrete replay policy, not a second
    implementation with different ranking behavior. -/
theorem CPreprocessorBackendSelectionConformance.policy_identity :
    replayPolicyWellFormed.policy = replayPolicy := by
  rfl

/-- Greater certified depth wins before compiler version; equal depth then uses
    the newer compiler version. GCC 15/depth 8 therefore beats GCC 16/depth 4. -/
theorem CPreprocessorBackendSelectionConformance.depth_then_version_fixture :
    (registryA.selectSupporting replayPolicy gccQuery reentrantIR).map
        CertifiedPreprocessorBackend.key = some gcc15.key := by
  native_decide

/-- Reordering the same certified registry entries cannot change the selected
    backend identity for one query and replay IR. -/
theorem CPreprocessorBackendSelectionConformance.registry_order_independent :
    (registryA.selectSupporting replayPolicy gccQuery reentrantIR).map
        CertifiedPreprocessorBackend.key =
      (registryB.selectSupporting replayPolicy gccQuery reentrantIR).map
        CertifiedPreprocessorBackend.key := by
  exact PreprocessorBackendRegistry.selectSupporting_key_eq_of_entries_perm
    replayPolicyWellFormed registryA registryB gccQuery reentrantIR registryEntriesPerm

end Producer
end CMeta
