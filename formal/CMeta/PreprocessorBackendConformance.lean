import CMeta.PreprocessorBackend
import CMeta.NestedReplayBackendPlan
import CMeta.NestedReplayGccGeneratedC
import CMeta.NestedReplayClangGeneratedC

/-!
# Preprocessor backend identity conformance

The nested-replay certificate must be attached to explicit compiler backends
rather than treated as a universal C11 fact.  GCC and Clang are independently
witnessed and compared through the shared replay lowering abstraction.
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

/-- Exact strict-C11 mode is part of both backend certificates. -/
theorem CPreprocessorBackendConformance.language_modes_are_c11 :
    NestedReplayGccGeneratedC.languageStandard = LanguageMode.c11.standardValue ∧
    NestedReplayClangGeneratedC.languageStandard = LanguageMode.c11.standardValue := by
  native_decide

/-- Replay lowering consumes the capability projected from the identified
    backend rather than a free-standing depth value. -/
theorem CPreprocessorBackendConformance.capability_projection :
    gccBackend.replayCapability.certifiedSameProducerDepth =
      gccBackend.certifiedSameProducerDepth ∧
    clangBackend.replayCapability.certifiedSameProducerDepth =
      clangBackend.certifiedSameProducerDepth := by
  constructor <;> rfl

/-- Both real compiler backends require deferred replay for active same-ID
    re-entry under the witnessed strict-C11 configuration. -/
theorem CPreprocessorBackendConformance.both_require_deferred :
    gccBackend.requiresDeferred = true ∧ clangBackend.requiresDeferred = true := by
  native_decide

/-- The currently witnessed GCC and Clang backends expose the same certified
    replay capability envelope. -/
theorem CPreprocessorBackendConformance.capabilities_agree :
    gccBackend.replayCapability = clangBackend.replayCapability := by
  native_decide

/-- Therefore the same re-entry IR lowers identically through either compiler
    capability, while compiler identity remains explicit outside the lowering. -/
theorem CPreprocessorBackendConformance.reentry_lowering_is_portable :
    lowerReplayBackendPlan gccBackend.replayCapability reentrantIR =
      lowerReplayBackendPlan clangBackend.replayCapability reentrantIR := by
  native_decide

end Producer
end CMeta
