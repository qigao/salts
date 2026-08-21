import CMeta.PreprocessorBackend
import CMeta.NestedReplayBackendPlan
import CMeta.NestedReplayGccGeneratedC
import CMeta.NestedReplayClangGeneratedC

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
  ⟨[gccCertified, clangCertified]⟩

private def reentrantIR : ReplayIR :=
  .replay 1 (.replay 2 (.replay 1 .emit))

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

/-- Portability is now a generic theorem over any two certified backends with
    equal replay capabilities, rather than a GCC/Clang-specific pair theorem. -/
theorem CPreprocessorBackendConformance.reentry_lowering_is_portable :
    lowerReplayBackendPlan gccCertified.replayCapability reentrantIR =
      lowerReplayBackendPlan clangCertified.replayCapability reentrantIR := by
  exact certifiedReplayLowering_eq_of_capability_eq
    gccCertified clangCertified reentrantIR (by native_decide)

end Producer
end CMeta
