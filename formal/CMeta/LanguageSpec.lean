import CMeta.PreprocessorBackendRegistrySetoid

/-!
# CMeta formal language specification

This module is the stable inference-rule facade over the executable CMeta
semantics. It does not introduce a second implementation of replay, registries,
selection, or lowering. Instead it names the existing judgments and exposes
proved rules in five groups:

* syntax carriers: replay IR, certified backends, queries, registries and plans;
* static judgments: certification, query matching, replay support and candidates;
* dynamic semantics: canonical lowering, selection and exact-key resolution;
* equivalence: registry observational `≈`;
* congruence: the operations that are proved to respect that equivalence.
-/

namespace CMeta
namespace Producer
namespace LanguageSpec

/-! ## Syntax carriers -/

abbrev IR := ReplayIR
abbrev Backend := CertifiedPreprocessorBackend
abbrev Query := BackendQuery
abbrev Registry := PreprocessorBackendRegistry
abbrev Plan := ReplayBackendPlan

/-! ## Static and dynamic judgments -/

/-- A raw backend has enough evidence to be wrapped as a certified backend. -/
abbrev Certifiable (backend : PreprocessorBackend) : Prop :=
  backend.IsReplayCertified

/-- A certified backend belongs to one compatibility query class. -/
abbrev Matches (backend : Backend) (query : Query) : Prop :=
  backend.matchesQuery query

/-- A certified backend covers the replay requirement computed from one IR. -/
abbrev Supports (backend : Backend) (ir : IR) : Prop :=
  backend.supportsReplay ir

/-- One backend is a valid supporting candidate in one concrete registry. -/
abbrev Candidate
    (registry : Registry) (query : Query) (ir : IR) (backend : Backend) : Prop :=
  backend ∈ registry.supportingCandidates query ir

/-- Dynamic lowering judgment for a certified backend. -/
abbrev LowersTo (backend : Backend) (ir : IR) (plan : Plan) : Prop :=
  lowerReplayBackendPlan backend.replayCapability ir = some plan

/-- Exact-key registry resolution judgment. -/
abbrev ResolvesTo
    (registry : Registry) (key : BackendKey) (ir : IR) (plan : Plan) : Prop :=
  registry.resolveReplay key ir = some plan

namespace Rule

/-! ## Static rules -/

/-- `[T-CERT]`: package raw evidence into the certified-backend type. -/
def cert_intro
    (backend : PreprocessorBackend)
    (certificate : Certifiable backend) : Backend :=
  ⟨backend, certificate⟩

/-- `[T-MATCH]`: family and language mode establish compatibility-class match. -/
theorem match_intro
    (backend : Backend) (query : Query)
    (hfamily : backend.backend.compilerFamily = query.family)
    (hmode : backend.backend.languageMode = query.languageMode) :
    Matches backend query :=
  ⟨hfamily, hmode⟩

/-- `[T-MATCH-ELIM]`: matching exposes exactly family and mode equality. -/
theorem match_elim
    (backend : Backend) (query : Query)
    (h : Matches backend query) :
    backend.backend.compilerFamily = query.family ∧
      backend.backend.languageMode = query.languageMode :=
  h

/-- `[T-SUPPORT]`: the IR-computed depth inside the certified envelope establishes support. -/
theorem support_intro
    (backend : Backend) (ir : IR)
    (h : ir.sameProducerDepth ≤
      backend.replayCapability.certifiedSameProducerDepth) :
    Supports backend ir :=
  h

/-- `[T-SUPPORT-ELIM]`: support exposes the exact certified-depth inequality. -/
theorem support_elim
    (backend : Backend) (ir : IR)
    (h : Supports backend ir) :
    ir.sameProducerDepth ≤ backend.replayCapability.certifiedSameProducerDepth :=
  h

/-- `[T-CANDIDATE]`: registry membership, query match and support form a candidate. -/
theorem candidate_intro
    (registry : Registry) (backend : Backend) (query : Query) (ir : IR)
    (hentry : backend ∈ registry.entries)
    (hmatch : Matches backend query)
    (hsupport : Supports backend ir) :
    Candidate registry query ir backend := by
  exact (registry.mem_supportingCandidates_iff query ir backend).2
    ⟨hentry, hmatch, hsupport⟩

/-- `[T-CANDIDATE-ELIM]`: candidate formation is an exact iff, not a lossy wrapper. -/
theorem candidate_elim
    (registry : Registry) (backend : Backend) (query : Query) (ir : IR)
    (h : Candidate registry query ir backend) :
    backend ∈ registry.entries ∧ Matches backend query ∧ Supports backend ir := by
  exact (registry.mem_supportingCandidates_iff query ir backend).1 h

/-! ## Dynamic rules -/

/-- `[T-LOWER]`: every supporting backend lowers to the one canonical plan. -/
theorem lower_intro
    (backend : Backend) (ir : IR)
    (hsupport : Supports backend ir) :
    LowersTo backend ir (ReplayBackendPlan.fromIR ir) := by
  exact lowerReplayBackendPlan_eq_canonical_of_supports
    backend.replayCapability ir hsupport

/-- `[T-LOWER-ELIM]`: successful lowering implies support and canonical-plan identity. -/
theorem lower_elim
    (backend : Backend) (ir : IR) (plan : Plan)
    (hlower : LowersTo backend ir plan) :
    Supports backend ir ∧ plan = ReplayBackendPlan.fromIR ir := by
  exact (lowerReplayBackendPlan_eq_some_iff backend.replayCapability ir plan).1 hlower

/-- `[T-SELECT]`: a successful policy selection is a supporting candidate. -/
theorem selection_elim
    (registry : Registry) (policy : BackendSelectionPolicy)
    (query : Query) (ir : IR) (backend : Backend)
    (hselect : registry.selectSupporting policy query ir = some backend) :
    Candidate registry query ir backend := by
  exact registry.selectSupporting_mem_candidates policy query ir backend hselect

/-- `[T-SELECT-LOWER]`: selection may choose a certificate, but not a different replay plan. -/
theorem selection_lower
    (registry : Registry) (policy : BackendSelectionPolicy)
    (query : Query) (ir : IR) (backend : Backend)
    (hselect : registry.selectSupporting policy query ir = some backend) :
    LowersTo backend ir (ReplayBackendPlan.fromIR ir) := by
  exact registry.selectSupporting_lowering_canonical policy query ir backend hselect

/-- `[T-RESOLVE]`: exact-key lookup plus support resolves to the canonical replay plan. -/
theorem resolve_intro
    (registry : Registry) (key : BackendKey) (ir : IR) (backend : Backend)
    (hlookup : registry.lookup key = some backend)
    (hsupport : Supports backend ir) :
    ResolvesTo registry key ir (ReplayBackendPlan.fromIR ir) := by
  unfold ResolvesTo PreprocessorBackendRegistry.resolveReplay
  rw [hlookup]
  exact lower_intro backend ir hsupport

/-- `[T-RESOLVE-ELIM]`: successful resolution identifies a stored supporting backend and canonical plan. -/
theorem resolve_elim
    (registry : Registry) (key : BackendKey) (ir : IR) (plan : Plan)
    (hresolve : ResolvesTo registry key ir plan) :
    ∃ backend,
      registry.lookup key = some backend ∧
      Supports backend ir ∧
      plan = ReplayBackendPlan.fromIR ir := by
  exact (registry.resolveReplay_eq_some_iff key ir plan).1 hresolve

/-! ## Observational equivalence rules -/

/-- `[E-REFL]`. -/
theorem eq_refl (registry : Registry) : registry ≈ registry :=
  PreprocessorBackendRegistry.equivalent_refl registry

/-- `[E-SYM]`. -/
theorem eq_symm {left right : Registry} (h : left ≈ right) : right ≈ left :=
  PreprocessorBackendRegistry.equivalent_symm h

/-- `[E-TRANS]`. -/
theorem eq_trans
    {first second third : Registry}
    (h12 : first ≈ second) (h23 : second ≈ third) : first ≈ third :=
  PreprocessorBackendRegistry.equivalent_trans h12 h23

/-! ## Congruence rules -/

/-- `[E-CANDIDATES]`: candidate discovery is congruent up to permutation. -/
theorem candidates_congr
    {left right : Registry} (heq : left ≈ right)
    (query : Query) (ir : IR) :
    (left.supportingCandidates query ir).Perm
      (right.supportingCandidates query ir) :=
  PreprocessorBackendRegistry.supportingCandidates_congr heq query ir

/-- `[E-SELECT]`: well-formed selection preserves the selected exact identity. -/
theorem selection_congr
    (wellFormed : WellFormedSelectionPolicy)
    {left right : Registry} (heq : left ≈ right)
    (query : Query) (ir : IR) :
    (left.selectSupporting wellFormed.policy query ir).map
        CertifiedPreprocessorBackend.key =
      (right.selectSupporting wellFormed.policy query ir).map
        CertifiedPreprocessorBackend.key :=
  PreprocessorBackendRegistry.selectSupporting_key_congr
    wellFormed heq query ir

/-- `[E-LOWER]`: selected replay lowering is a congruent observation. -/
theorem lowering_congr
    (wellFormed : WellFormedSelectionPolicy)
    {left right : Registry} (heq : left ≈ right)
    (query : Query) (ir : IR) :
    (left.selectSupporting wellFormed.policy query ir).map (fun backend =>
        lowerReplayBackendPlan backend.replayCapability ir) =
      (right.selectSupporting wellFormed.policy query ir).map (fun backend =>
        lowerReplayBackendPlan backend.replayCapability ir) :=
  PreprocessorBackendRegistry.selectSupporting_lowering_congr
    wellFormed heq query ir

/-- `[E-REMOVE]`: total removal is a proper Setoid operation. -/
theorem remove_congr
    {left right : Registry} (heq : left ≈ right) (key : BackendKey) :
    left.remove key ≈ right.remove key :=
  PreprocessorBackendRegistry.remove_congr heq key

/-- `[E-INSERT]`: partial insertion preserves failure/success shape and Setoid equality. -/
theorem insert_congr
    {left right : Registry} (heq : left ≈ right) (backend : Backend) :
    PreprocessorBackendRegistry.MutationResultEquivalent
      (left.insert backend) (right.insert backend) :=
  PreprocessorBackendRegistry.insert_congr heq backend

/-- `[E-REPLACE]`: replacement is congruent under the lifted partial-mutation relation. -/
theorem replace_congr
    {left right : Registry} (heq : left ≈ right) (backend : Backend) :
    PreprocessorBackendRegistry.MutationResultEquivalent
      (left.replace backend) (right.replace backend) :=
  PreprocessorBackendRegistry.replace_congr heq backend

end Rule
end LanguageSpec
end Producer
end CMeta
