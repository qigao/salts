import CMeta.NestedReplayLowering

/-!
# Preprocessor backend identity

Replay applicability belongs to a concrete preprocessor backend, not to C11 in
the abstract. This model carries the compiler family and major version together
with the exact language mode and the replay capability certificate observed for
that backend.
-/

namespace CMeta
namespace Producer

inductive CompilerFamily where
  | gcc
  | clang
  deriving Repr, DecidableEq

namespace CompilerFamily

def tag : CompilerFamily → Nat
  | .gcc => 1
  | .clang => 2

end CompilerFamily

inductive LanguageMode where
  | c11
  deriving Repr, DecidableEq

namespace LanguageMode

def standardValue : LanguageMode → Nat
  | .c11 => 201112

end LanguageMode

structure PreprocessorBackend where
  compilerFamily : CompilerFamily
  compilerMajorVersion : Nat
  languageMode : LanguageMode
  directSameProducerAccepted : Bool
  deferredSameProducerAccepted : Bool
  certifiedSameProducerDepth : Nat
  deriving Repr, DecidableEq

namespace PreprocessorBackend

/-- Capability consumed by replay lowering. Compiler identity remains attached
    to the surrounding backend, while the lowering API receives only the
    capability projection it needs. -/
def replayCapability (backend : PreprocessorBackend) : ReplayBackendCapability :=
  ⟨backend.certifiedSameProducerDepth⟩

/-- Whether the observed backend evidence requires the deferred same-producer
    path: direct re-entry is rejected while the deferred path is accepted. -/
def requiresDeferred (backend : PreprocessorBackend) : Bool :=
  (!backend.directSameProducerAccepted) && backend.deferredSameProducerAccepted

/-- Evidence required before a backend can enter the replay registry. Direct
    re-entry does not have to be rejected: a future backend may accept both
    direct and deferred forms. The lowering contract only requires that the
    deferred path is observed to work and that a non-zero replay depth has been
    certified for a concrete compiler version. -/
abbrev IsReplayCertified (backend : PreprocessorBackend) : Prop :=
  0 < backend.compilerMajorVersion ∧
  backend.deferredSameProducerAccepted = true ∧
  0 < backend.certifiedSameProducerDepth

end PreprocessorBackend

/-- A backend whose replay evidence is strong enough to be consumed by the
    lowering registry. -/
structure CertifiedPreprocessorBackend where
  backend : PreprocessorBackend
  certificate : backend.IsReplayCertified

namespace CertifiedPreprocessorBackend

/-- Capability projection used by replay lowering. -/
def replayCapability
    (backend : CertifiedPreprocessorBackend) : ReplayBackendCapability :=
  backend.backend.replayCapability

/-- Certification always carries a concrete compiler version. -/
theorem compilerVersionPositive (backend : CertifiedPreprocessorBackend) :
    0 < backend.backend.compilerMajorVersion :=
  backend.certificate.1

/-- Certification guarantees that the deferred same-producer path was accepted. -/
theorem deferredSameProducerAccepted (backend : CertifiedPreprocessorBackend) :
    backend.backend.deferredSameProducerAccepted = true :=
  backend.certificate.2.1

/-- Certification guarantees a non-zero same-producer replay envelope. -/
theorem certifiedDepthPositive (backend : CertifiedPreprocessorBackend) :
    0 < backend.backend.certifiedSameProducerDepth :=
  backend.certificate.2.2

end CertifiedPreprocessorBackend

/-- Registry entries are certified by construction, so adding another compiler
    does not widen the trusted boundary of replay lowering. -/
structure PreprocessorBackendRegistry where
  entries : List CertifiedPreprocessorBackend

end Producer
end CMeta
