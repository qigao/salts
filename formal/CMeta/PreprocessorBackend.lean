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

/-- Stable registry identity for one observed compiler/language configuration. -/
structure BackendKey where
  family : CompilerFamily
  majorVersion : Nat
  languageMode : LanguageMode
  deriving Repr, DecidableEq

/-- Compatibility query intentionally omits compiler version. Exact versioned
    identity is handled by `BackendKey`; this query describes the family/mode
    class from which capability-compatible candidates may be discovered. -/
structure BackendQuery where
  family : CompilerFamily
  languageMode : LanguageMode
  deriving Repr, DecidableEq

structure PreprocessorBackend where
  compilerFamily : CompilerFamily
  compilerMajorVersion : Nat
  languageMode : LanguageMode
  directSameProducerAccepted : Bool
  deferredSameProducerAccepted : Bool
  certifiedSameProducerDepth : Nat
  deriving Repr, DecidableEq

namespace PreprocessorBackend

/-- Compiler identity used by the finite backend registry. -/
def key (backend : PreprocessorBackend) : BackendKey :=
  ⟨backend.compilerFamily, backend.compilerMajorVersion, backend.languageMode⟩

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

/-- Identity projection retained by the registry. -/
def key (backend : CertifiedPreprocessorBackend) : BackendKey :=
  backend.backend.key

/-- Capability projection used by replay lowering. -/
def replayCapability
    (backend : CertifiedPreprocessorBackend) : ReplayBackendCapability :=
  backend.backend.replayCapability

/-- Compatibility-class match used only for candidate discovery. Version is
    deliberately absent here and remains available through `backend.key`. -/
abbrev matchesQuery
    (backend : CertifiedPreprocessorBackend) (query : BackendQuery) : Prop :=
  backend.backend.compilerFamily = query.family ∧
  backend.backend.languageMode = query.languageMode

/-- Whether this certified backend covers the replay requirement of one IR. -/
abbrev supportsReplay
    (backend : CertifiedPreprocessorBackend) (ir : ReplayIR) : Prop :=
  ir.sameProducerDepth ≤ backend.replayCapability.certifiedSameProducerDepth

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

/-- Registry representation is a list, but its semantic contract is a finite map:
    every compiler key occurs at most once and every stored entry is certified by
    construction. -/
structure PreprocessorBackendRegistry where
  entries : List CertifiedPreprocessorBackend
  uniqueKeys : (entries.map CertifiedPreprocessorBackend.key).Nodup

namespace PreprocessorBackendRegistry

private def lookupEntries :
    List CertifiedPreprocessorBackend → BackendKey →
      Option CertifiedPreprocessorBackend
  | [], _ => none
  | backend :: rest, key =>
      if backend.key = key then some backend else lookupEntries rest key

/-- Resolve one certified backend by exact compiler identity. -/
def lookup
    (registry : PreprocessorBackendRegistry) (key : BackendKey) :
    Option CertifiedPreprocessorBackend :=
  lookupEntries registry.entries key

/-- Discover every certified backend in the requested family/language class that
    covers the current replay IR. This function intentionally does not rank or
    choose among candidates; selection policy is a separate layer. -/
def supportingCandidates
    (registry : PreprocessorBackendRegistry) (query : BackendQuery) (ir : ReplayIR) :
    List CertifiedPreprocessorBackend :=
  registry.entries.filter fun backend =>
    decide (backend.matchesQuery query ∧ backend.supportsReplay ir)

/-- Candidate discovery is exactly registry membership plus compatibility-class
    match plus support for the requested IR. No ordering or preference policy is
    encoded in this theorem. -/
theorem mem_supportingCandidates_iff
    (registry : PreprocessorBackendRegistry) (query : BackendQuery)
    (ir : ReplayIR) (backend : CertifiedPreprocessorBackend) :
    backend ∈ registry.supportingCandidates query ir ↔
      backend ∈ registry.entries ∧
      backend.matchesQuery query ∧
      backend.supportsReplay ir := by
  simp [supportingCandidates]

end PreprocessorBackendRegistry

end Producer
end CMeta
