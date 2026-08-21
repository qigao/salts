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

/-!
## Verified finite-map mutation

Mutation is defined at the registry boundary rather than by exposing raw list
updates. `insert` and `replace` are partial because their key preconditions can
fail. `remove` is total and treats a missing key as a no-op. Every successful
mutation constructs a new `PreprocessorBackendRegistry`, so `uniqueKeys` and
backend certification remain type-level invariants rather than postconditions.
-/

/-- Remove the first entry with the requested key. Registry key uniqueness means
    this is also the only possible matching entry. -/
private def removeEntries :
    List CertifiedPreprocessorBackend → BackendKey →
      List CertifiedPreprocessorBackend
  | [], _ => []
  | backend :: rest, key =>
      if backend.key = key then rest else backend :: removeEntries rest key

/-- Removing an entry cannot introduce a key that was not present before. -/
private theorem mem_map_removeEntries
    (entries : List CertifiedPreprocessorBackend)
    (target candidate : BackendKey)
    (hmem : candidate ∈
      (removeEntries entries target).map CertifiedPreprocessorBackend.key) :
    candidate ∈ entries.map CertifiedPreprocessorBackend.key := by
  induction entries with
  | nil =>
      simp [removeEntries] at hmem
  | cons backend rest ih =>
      by_cases hkey : backend.key = target
      · simp only [removeEntries, hkey, if_pos]
        exact Or.inr hmem
      · simp only [removeEntries, hkey, if_neg, List.map_cons, List.mem_cons] at hmem ⊢
        exact hmem.elim Or.inl (fun hrest => Or.inr (ih hrest))

/-- The list-level removal implementation preserves key uniqueness. -/
private theorem removeEntries_nodup_keys
    (entries : List CertifiedPreprocessorBackend)
    (target : BackendKey)
    (hunique : (entries.map CertifiedPreprocessorBackend.key).Nodup) :
    ((removeEntries entries target).map CertifiedPreprocessorBackend.key).Nodup := by
  induction entries with
  | nil =>
      simp [removeEntries]
  | cons backend rest ih =>
      have hhead := (List.nodup_cons.mp hunique).1
      have htail := (List.nodup_cons.mp hunique).2
      by_cases hkey : backend.key = target
      · simp only [removeEntries, hkey, if_pos]
        exact htail
      · simp only [removeEntries, hkey, if_neg, List.map_cons]
        exact List.nodup_cons.mpr
          ⟨(fun hmem => hhead (mem_map_removeEntries rest target backend.key hmem)),
            ih htail⟩

/-- Under the registry uniqueness invariant, the removed key cannot remain in
    the resulting key list. -/
private theorem target_not_mem_removeEntries
    (entries : List CertifiedPreprocessorBackend)
    (target : BackendKey)
    (hunique : (entries.map CertifiedPreprocessorBackend.key).Nodup) :
    target ∉ (removeEntries entries target).map CertifiedPreprocessorBackend.key := by
  induction entries with
  | nil =>
      simp [removeEntries]
  | cons backend rest ih =>
      have hhead := (List.nodup_cons.mp hunique).1
      have htail := (List.nodup_cons.mp hunique).2
      by_cases hkey : backend.key = target
      · simp only [removeEntries, hkey, if_pos]
        simpa [hkey] using hhead
      · simp only [removeEntries, hkey, if_neg, List.map_cons, List.mem_cons, not_or]
        exact ⟨Ne.symm hkey, ih htail⟩

private theorem lookupEntries_none_of_not_mem
    (entries : List CertifiedPreprocessorBackend)
    (key : BackendKey)
    (hnotmem : key ∉ entries.map CertifiedPreprocessorBackend.key) :
    lookupEntries entries key = none := by
  induction entries with
  | nil =>
      rfl
  | cons backend rest ih =>
      have hparts : key ≠ backend.key ∧
          key ∉ rest.map CertifiedPreprocessorBackend.key := by
        simpa using hnotmem
      have hbackend : backend.key ≠ key := Ne.symm hparts.1
      simp [lookupEntries, hbackend, ih hparts.2]

/-- Removing another key leaves exact lookup unchanged. This helper is stronger
    than the public key-projection frame law below. -/
private theorem lookupEntries_removeEntries_ne
    (entries : List CertifiedPreprocessorBackend)
    (target key : BackendKey)
    (hne : target ≠ key) :
    lookupEntries (removeEntries entries target) key =
      lookupEntries entries key := by
  induction entries with
  | nil =>
      rfl
  | cons backend rest ih =>
      by_cases hremove : backend.key = target
      · have hlookup : backend.key ≠ key := by
          intro hsame
          exact hne (hremove.symm.trans hsame)
        simp [removeEntries, lookupEntries, hremove, hlookup]
      · by_cases hlookup : backend.key = key
        · simp [removeEntries, lookupEntries, hremove, hlookup]
        · simp [removeEntries, lookupEntries, hremove, hlookup, ih]

/-- Insert a fresh exact backend identity. Duplicate keys are rejected rather
    than relying on list position to decide which certificate wins. -/
def insert
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend) :
    Option PreprocessorBackendRegistry :=
  if hduplicate : backend.key ∈
      registry.entries.map CertifiedPreprocessorBackend.key then
    none
  else
    some
      { entries := backend :: registry.entries
        uniqueKeys := by
          simp [hduplicate, registry.uniqueKeys] }

/-- Remove one exact backend identity. Missing keys are a no-op. -/
def remove
    (registry : PreprocessorBackendRegistry)
    (key : BackendKey) : PreprocessorBackendRegistry :=
  { entries := removeEntries registry.entries key
    uniqueKeys := removeEntries_nodup_keys registry.entries key registry.uniqueKeys }

/-- Replace one existing exact backend identity with a new certified payload.
    Missing keys are rejected; replacement never acts as an implicit insert. -/
def replace
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend) :
    Option PreprocessorBackendRegistry :=
  match registry.lookup backend.key with
  | none => none
  | some _ => (registry.remove backend.key).insert backend

/-- Insert fails exactly when the exact backend key is already present. -/
theorem insert_eq_none_iff
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend) :
    registry.insert backend = none ↔
      backend.key ∈ registry.entries.map CertifiedPreprocessorBackend.key := by
  simp [insert]

/-- Successful insertion resolves the inserted exact identity. -/
theorem lookup_insert_self
    (registry inserted : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (hinsert : registry.insert backend = some inserted) :
    (inserted.lookup backend.key).map CertifiedPreprocessorBackend.key =
      some backend.key := by
  unfold insert at hinsert
  split at hinsert
  · contradiction
  · simp only [Option.some.injEq] at hinsert
    subst inserted
    simp [lookup, lookupEntries]

/-- Successful insertion is a finite-map frame update: every other exact key
    keeps the same lookup result. -/
theorem lookup_insert_ne
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (inserted : PreprocessorBackendRegistry)
    (key : BackendKey)
    (hinsert : registry.insert backend = some inserted)
    (hne : backend.key ≠ key) :
    (inserted.lookup key).map CertifiedPreprocessorBackend.key =
      (registry.lookup key).map CertifiedPreprocessorBackend.key := by
  unfold insert at hinsert
  split at hinsert
  · contradiction
  · simp only [Option.some.injEq] at hinsert
    subst inserted
    simp [lookup, lookupEntries, hne]

/-- Removal makes the target exact key absent. -/
theorem lookup_remove_self
    (registry : PreprocessorBackendRegistry)
    (key : BackendKey) :
    registry.remove key |>.lookup key = none := by
  change lookupEntries (removeEntries registry.entries key) key = none
  exact lookupEntries_none_of_not_mem
    (removeEntries registry.entries key) key
    (target_not_mem_removeEntries registry.entries key registry.uniqueKeys)

/-- Removal is a finite-map frame update for every non-target key. -/
theorem lookup_remove_ne
    (registry : PreprocessorBackendRegistry)
    (target key : BackendKey)
    (hne : target ≠ key) :
    ((registry.remove target).lookup key).map CertifiedPreprocessorBackend.key =
      (registry.lookup key).map CertifiedPreprocessorBackend.key := by
  change (lookupEntries (removeEntries registry.entries target) key).map
      CertifiedPreprocessorBackend.key =
    (lookupEntries registry.entries key).map CertifiedPreprocessorBackend.key
  exact congrArg (Option.map CertifiedPreprocessorBackend.key)
    (lookupEntries_removeEntries_ne registry.entries target key hne)

/-- Successful replacement resolves the new certified payload under the same
    exact backend identity. -/
theorem lookup_replace_self
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (replaced : PreprocessorBackendRegistry)
    (hreplace : registry.replace backend = some replaced) :
    (replaced.lookup backend.key).map CertifiedPreprocessorBackend.key =
      some backend.key := by
  cases hlookup : registry.lookup backend.key with
  | none =>
      simp [replace, hlookup] at hreplace
  | some existing =>
      have hinsert :
          (registry.remove backend.key).insert backend = some replaced := by
        simpa [replace, hlookup] using hreplace
      exact lookup_insert_self (registry.remove backend.key) replaced backend hinsert

/-- Replacement preserves every lookup outside the replaced exact key. -/
theorem lookup_replace_ne
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (replaced : PreprocessorBackendRegistry)
    (key : BackendKey)
    (hreplace : registry.replace backend = some replaced)
    (hne : backend.key ≠ key) :
    (replaced.lookup key).map CertifiedPreprocessorBackend.key =
      (registry.lookup key).map CertifiedPreprocessorBackend.key := by
  cases hlookup : registry.lookup backend.key with
  | none =>
      simp [replace, hlookup] at hreplace
  | some existing =>
      have hinsert :
          (registry.remove backend.key).insert backend = some replaced := by
        simpa [replace, hlookup] using hreplace
      calc
        (replaced.lookup key).map CertifiedPreprocessorBackend.key =
            ((registry.remove backend.key).lookup key).map
              CertifiedPreprocessorBackend.key :=
          lookup_insert_ne (registry.remove backend.key) backend replaced key hinsert hne
        _ = (registry.lookup key).map CertifiedPreprocessorBackend.key :=
          lookup_remove_ne registry backend.key key hne

end PreprocessorBackendRegistry

end Producer
end CMeta
