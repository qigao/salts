module
public import CMeta.PreprocessorBackend
public import CMeta.NestedReplayBackendPlan
public import CMeta.PreprocessorBackendSelection
public import CMeta.PreprocessorBackendRegistrySetoid
import all CMeta.PreprocessorBackend
import all CMeta.NestedReplayBackendPlan
import all CMeta.PreprocessorBackendSelection
import all CMeta.PreprocessorBackendRegistryEquivalence
import all CMeta.PreprocessorBackendRegistrySetoid

/-!
# CMeta formal language specification

This module is the stable inference-rule facade over the executable CMeta
semantics. It names the syntax carriers, static judgments, canonical dynamic
semantics, observational equivalence, and the already-audited congruence
surface. No second implementation of registry or replay semantics is introduced.
-/

namespace CMeta
namespace Producer
namespace LanguageSpec

/-! ## Syntax carriers -/

public abbrev IR := ReplayIR
public abbrev Backend := CertifiedPreprocessorBackend
public abbrev Query := BackendQuery
public abbrev Registry := PreprocessorBackendRegistry
public abbrev Plan := ReplayBackendPlan

/-! ## Static and dynamic judgments -/

/-- Raw backend evidence sufficient to construct a certified backend. -/
public abbrev Certifiable (backend : PreprocessorBackend) : Prop :=
  backend.IsReplayCertified

/-- Compatibility-class judgment. -/
public abbrev Matches (backend : Backend) (query : Query) : Prop :=
  backend.matchesQuery query

/-- Replay-capability judgment. -/
public abbrev Supports (backend : Backend) (ir : IR) : Prop :=
  backend.supportsReplay ir

/-- Supporting-candidate judgment. -/
public abbrev Candidate
    (registry : Registry)
    (query : Query)
    (ir : IR)
    (backend : Backend) : Prop :=
  backend ∈ registry.supportingCandidates query ir

/-- Successful backend lowering judgment. -/
public abbrev LowersTo (backend : Backend) (ir : IR) (plan : Plan) : Prop :=
  lowerReplayBackendPlan backend.replayCapability ir = some plan

/-- Successful exact-key registry resolution judgment. -/
public abbrev ResolvesTo
    (registry : Registry) (key : BackendKey) (ir : IR) (plan : Plan) : Prop :=
  registry.resolveReplay key ir = some plan

namespace Rule

/-! ## Static rules -/

/-- `[T-CERT]`: package replay evidence into the certified-backend type. -/
public def cert_intro
    (backend : PreprocessorBackend)
    (certificate : Certifiable backend) : Backend :=
  ⟨backend, certificate⟩

/-- `[T-MATCH]`: compiler family and language mode establish query match. -/
public theorem match_intro
    (backend : Backend)
    (query : Query)
    (hfamily : backend.backend.compilerFamily = query.family)
    (hmode : backend.backend.languageMode = query.languageMode) :
    Matches backend query :=
  ⟨hfamily, hmode⟩

/-- `[T-SUPPORT]`: an IR inside the certified depth envelope is supported. -/
public theorem support_intro
    (backend : Backend)
    (ir : IR)
    (hdepth : ir.sameProducerDepth ≤
      backend.replayCapability.certifiedSameProducerDepth) :
    Supports backend ir :=
  hdepth

/-- `[T-CANDIDATE]`: membership, query match and replay support form a candidate. -/
public theorem candidate_intro
    (registry : Registry)
    (backend : Backend)
    (query : Query)
    (ir : IR)
    (hentry : backend ∈ registry.entries)
    (hmatch : Matches backend query)
    (hsupport : Supports backend ir) :
    Candidate registry query ir backend := by
  exact (registry.mem_supportingCandidates_iff query ir backend).2
    ⟨hentry, hmatch, hsupport⟩

/-- `[T-CANDIDATE-ELIM]`: candidate formation is an exact iff. -/
public theorem candidate_elim
    (registry : Registry)
    (backend : Backend)
    (query : Query)
    (ir : IR)
    (h : Candidate registry query ir backend) :
    backend ∈ registry.entries ∧
      Matches backend query ∧
      Supports backend ir := by
  exact (registry.mem_supportingCandidates_iff query ir backend).1 h

/-! ## Dynamic rules -/

/-- `[T-LOWER]`: every supporting backend lowers to the one canonical plan. -/
public theorem lower_intro
    (backend : Backend)
    (ir : IR)
    (hsupport : Supports backend ir) :
    LowersTo backend ir (ReplayBackendPlan.fromIR ir) := by
  exact lowerReplayBackendPlan_eq_canonical_of_supports
    backend.replayCapability ir hsupport

/-- `[T-LOWER-ELIM]`: successful lowering exposes support and canonical identity. -/
public theorem lower_elim
    (backend : Backend)
    (ir : IR)
    (plan : Plan)
    (hlower : LowersTo backend ir plan) :
    Supports backend ir ∧ plan = ReplayBackendPlan.fromIR ir := by
  exact (lowerReplayBackendPlan_eq_some_iff
    backend.replayCapability ir plan).1 hlower

/-- `[T-SELECT]`: successful policy selection is a supporting candidate. -/
public theorem selection_elim
    (registry : Registry)
    (policy : BackendSelectionPolicy)
    (query : Query)
    (ir : IR)
    (backend : Backend)
    (hselect : registry.selectSupporting policy query ir = some backend) :
    Candidate registry query ir backend := by
  exact registry.selectSupporting_mem_candidates
    policy query ir backend hselect

/-- `[T-SELECT-LOWER]`: selection chooses a certificate, never another plan. -/
public theorem selection_lower
    (registry : Registry)
    (policy : BackendSelectionPolicy)
    (query : Query)
    (ir : IR)
    (backend : Backend)
    (hselect : registry.selectSupporting policy query ir = some backend) :
    LowersTo backend ir (ReplayBackendPlan.fromIR ir) := by
  exact registry.selectSupporting_lowering_canonical
    policy query ir backend hselect

/-- `[T-RESOLVE]`: exact lookup plus support resolves to the canonical plan. -/
public theorem resolve_intro
    (registry : Registry)
    (key : BackendKey)
    (ir : IR)
    (backend : Backend)
    (hlookup : registry.lookup key = some backend)
    (hsupport : Supports backend ir) :
    ResolvesTo registry key ir (ReplayBackendPlan.fromIR ir) := by
  unfold ResolvesTo PreprocessorBackendRegistry.resolveReplay
  rw [hlookup]
  exact lower_intro backend ir hsupport

/-- `[T-RESOLVE-ELIM]`: resolution identifies a stored supporter and canonical plan. -/
public theorem resolve_elim
    (registry : Registry)
    (key : BackendKey)
    (ir : IR)
    (plan : Plan)
    (hresolve : ResolvesTo registry key ir plan) :
    ∃ backend,
      registry.lookup key = some backend ∧
      Supports backend ir ∧
      plan = ReplayBackendPlan.fromIR ir := by
  exact (registry.resolveReplay_eq_some_iff key ir plan).1 hresolve

/-! ## Observational equivalence rules -/

/-- `[E-REFL]`. -/
public theorem eq_refl (registry : Registry) : registry ≈ registry :=
  PreprocessorBackendRegistry.equivalent_refl registry

/-- `[E-SYM]`. -/
public theorem eq_symm
    {left right : Registry}
    (h : left ≈ right) : right ≈ left :=
  PreprocessorBackendRegistry.equivalent_symm h

/-- `[E-TRANS]`. -/
public theorem eq_trans
    {first second third : Registry}
    (h12 : first ≈ second)
    (h23 : second ≈ third) : first ≈ third :=
  PreprocessorBackendRegistry.equivalent_trans h12 h23

/-! ## Congruence rules -/

/-- `[E-CANDIDATES]`: discovery is congruent up to permutation. -/
public theorem candidates_congr
    {left right : Registry}
    (heq : left ≈ right)
    (query : Query)
    (ir : IR) :
    (left.supportingCandidates query ir).Perm
      (right.supportingCandidates query ir) :=
  PreprocessorBackendRegistry.supportingCandidates_congr heq query ir

/-- `[E-SELECT]`: well-formed selection preserves selected exact identity. -/
public theorem selection_congr
    (wellFormed : WellFormedSelectionPolicy)
    {left right : Registry}
    (heq : left ≈ right)
    (query : Query)
    (ir : IR) :
    (left.selectSupporting wellFormed.policy query ir).map
        CertifiedPreprocessorBackend.key =
      (right.selectSupporting wellFormed.policy query ir).map
        CertifiedPreprocessorBackend.key :=
  PreprocessorBackendRegistry.selectSupporting_key_congr
    wellFormed heq query ir

/-- `[E-LOWER]`: selected replay lowering is a congruent observation. -/
public theorem lowering_congr
    (wellFormed : WellFormedSelectionPolicy)
    {left right : Registry}
    (heq : left ≈ right)
    (query : Query)
    (ir : IR) :
    (left.selectSupporting wellFormed.policy query ir).map (fun backend =>
        lowerReplayBackendPlan backend.replayCapability ir) =
      (right.selectSupporting wellFormed.policy query ir).map (fun backend =>
        lowerReplayBackendPlan backend.replayCapability ir) :=
  PreprocessorBackendRegistry.selectSupporting_lowering_congr
    wellFormed heq query ir

/-- `[E-REMOVE]`: total removal is a proper registry-Setoid operation. -/
public theorem remove_congr
    {left right : Registry}
    (heq : left ≈ right)
    (key : BackendKey) :
    left.remove key ≈ right.remove key :=
  PreprocessorBackendRegistry.remove_congr heq key

/-- `[E-INSERT]`: insertion preserves partial-result semantics. -/
public theorem insert_congr
    {left right : Registry}
    (heq : left ≈ right)
    (backend : Backend) :
    PreprocessorBackendRegistry.MutationResultEquivalent
      (left.insert backend) (right.insert backend) :=
  PreprocessorBackendRegistry.insert_congr heq backend

/-- `[E-REPLACE]`: replacement preserves partial-result semantics. -/
public theorem replace_congr
    {left right : Registry}
    (heq : left ≈ right)
    (backend : Backend) :
    PreprocessorBackendRegistry.MutationResultEquivalent
      (left.replace backend) (right.replace backend) :=
  PreprocessorBackendRegistry.replace_congr heq backend

end Rule
end LanguageSpec
end Producer
end CMeta
