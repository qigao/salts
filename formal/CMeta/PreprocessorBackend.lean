import CMeta.NestedReplayLowering

/-!
# Preprocessor backend identity

Replay applicability belongs to a concrete preprocessor backend, not to C11 in
the abstract.  This model carries the compiler family and major version together
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

/-- Capability consumed by replay lowering.  Compiler identity remains attached
    to the surrounding backend, while the lowering API receives only the
    capability projection it needs. -/
def replayCapability (backend : PreprocessorBackend) : ReplayBackendCapability :=
  ⟨backend.certifiedSameProducerDepth⟩

/-- Whether the observed backend evidence requires the deferred same-producer
    path: direct re-entry is rejected while the deferred path is accepted. -/
def requiresDeferred (backend : PreprocessorBackend) : Bool :=
  (!backend.directSameProducerAccepted) && backend.deferredSameProducerAccepted

end PreprocessorBackend

end Producer
end CMeta
