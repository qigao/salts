import CMeta.PreprocessorBackend
import CMeta.NestedReplayBackendPlan
import CMeta.NestedReplayGeneratedC

/-!
# Preprocessor backend identity conformance

The nested-replay certificate must be attached to an explicit preprocessor
backend identity rather than treated as a universal C11 fact.
-/

namespace CMeta
namespace Producer

private def observedBackend : PreprocessorBackend :=
  { compilerFamily := .gcc
    compilerMajorVersion := NestedReplayGeneratedC.compilerMajorVersion
    languageMode := .c11
    directSameProducerAccepted := NestedReplayGeneratedC.directSameProducerAccepted
    deferredSameProducerAccepted := NestedReplayGeneratedC.deferredSameProducerAccepted
    certifiedSameProducerDepth := NestedReplayGeneratedC.certifiedSameProducerDepth }

private def reentrantIR : ReplayIR :=
  .replay 1 (.replay 2 (.replay 1 .emit))

/-- The generated witness identifies the compiler family explicitly. -/
theorem CPreprocessorBackendConformance.generated_family_is_gcc :
    NestedReplayGeneratedC.compilerFamilyTag = CompilerFamily.gcc.tag := by
  native_decide

/-- The generated witness records a concrete compiler major version. -/
theorem CPreprocessorBackendConformance.generated_version_is_concrete :
    0 < NestedReplayGeneratedC.compilerMajorVersion := by
  native_decide

/-- Exact strict-C11 mode is part of the backend certificate. -/
theorem CPreprocessorBackendConformance.generated_mode_is_c11 :
    NestedReplayGeneratedC.languageStandard = LanguageMode.c11.standardValue := by
  native_decide

/-- Replay lowering consumes the capability projected from the identified
    backend rather than a free-standing depth value. -/
theorem CPreprocessorBackendConformance.capability_projection :
    observedBackend.replayCapability.certifiedSameProducerDepth =
      observedBackend.certifiedSameProducerDepth := by
  rfl

/-- The identified real backend requires deferred replay for active same-ID
    re-entry. -/
theorem CPreprocessorBackendConformance.real_backend_requires_deferred :
    observedBackend.requiresDeferred = true := by
  native_decide

/-- A re-entry shape inside the identified backend certificate lowers through
    the capability projected from that backend. -/
theorem CPreprocessorBackendConformance.lowering_uses_backend_capability :
    (lowerReplayBackendPlan observedBackend.replayCapability reentrantIR).isSome = true := by
  native_decide

end Producer
end CMeta
