module
import all CMeta.LanguageSpec

/-!
# CMeta language rule conformance

This module treats the proved CMeta semantics as a small formal language.  It
requires a stable rule facade over the existing implementation theorems, then
checks that syntax carriers, static judgments, dynamic lowering, observational
equivalence and mutation congruence compose without exposing representation
details.
-/

namespace CMeta
namespace Producer

/-- Stable syntax aliases expose the core language carriers without changing representation. -/
theorem CLanguageSpecConformance.syntaxCarriers
    (ir : LanguageSpec.IR)
    (backend : LanguageSpec.Backend)
    (query : LanguageSpec.Query)
    (registry : LanguageSpec.Registry)
    (plan : LanguageSpec.Plan) :
    ir = ir ∧ backend = backend ∧ query = query ∧ registry = registry ∧ plan = plan := by
  exact ⟨rfl, rfl, rfl, rfl, rfl⟩

/-- Raw certification evidence can be introduced as a certified backend value. -/
theorem CLanguageSpecConformance.certificationIntro
    (backend : PreprocessorBackend)
    (hcert : LanguageSpec.Certifiable backend) :
    (LanguageSpec.Rule.cert_intro backend hcert).backend = backend := by
  rfl

/-- Family/mode equality and certified depth are exactly the primitive static judgments. -/
theorem CLanguageSpecConformance.matchAndSupport
    (backend : CertifiedPreprocessorBackend)
    (query : BackendQuery)
    (ir : ReplayIR)
    (hfamily : backend.backend.compilerFamily = query.family)
    (hmode : backend.backend.languageMode = query.languageMode)
    (hdepth : ir.sameProducerDepth ≤
      backend.replayCapability.certifiedSameProducerDepth) :
    LanguageSpec.Matches backend query ∧ LanguageSpec.Supports backend ir := by
  exact ⟨LanguageSpec.Rule.match_intro backend query hfamily hmode,
    LanguageSpec.Rule.support_intro backend ir hdepth⟩

/-- Static candidate formation followed by backend-independent canonical lowering. -/
theorem CLanguageSpecConformance.staticAndDynamic
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (query : BackendQuery)
    (ir : ReplayIR)
    (hentry : backend ∈ registry.entries)
    (hmatch : LanguageSpec.Matches backend query)
    (hsupport : LanguageSpec.Supports backend ir) :
    LanguageSpec.Candidate registry query ir backend ∧
      LanguageSpec.LowersTo backend ir (ReplayBackendPlan.fromIR ir) := by
  constructor
  · exact LanguageSpec.Rule.candidate_intro
      registry backend query ir hentry hmatch hsupport
  · exact LanguageSpec.Rule.lower_intro backend ir hsupport

/-- Candidate elimination exposes exactly registry membership, query match and support. -/
theorem CLanguageSpecConformance.candidateElim
    (registry : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (query : BackendQuery)
    (ir : ReplayIR)
    (hcandidate : LanguageSpec.Candidate registry query ir backend) :
    backend ∈ registry.entries ∧
      LanguageSpec.Matches backend query ∧
      LanguageSpec.Supports backend ir := by
  exact LanguageSpec.Rule.candidate_elim
    registry backend query ir hcandidate

/-- Successful lowering eliminates to support plus canonical-plan identity. -/
theorem CLanguageSpecConformance.loweringElim
    (backend : CertifiedPreprocessorBackend)
    (ir : ReplayIR)
    (plan : ReplayBackendPlan)
    (hlower : LanguageSpec.LowersTo backend ir plan) :
    LanguageSpec.Supports backend ir ∧
      plan = ReplayBackendPlan.fromIR ir := by
  exact LanguageSpec.Rule.lower_elim backend ir plan hlower

/-- Successful selection is a supporting candidate and therefore lowers canonically. -/
theorem CLanguageSpecConformance.selectionRules
    (registry : PreprocessorBackendRegistry)
    (policy : BackendSelectionPolicy)
    (query : BackendQuery)
    (ir : ReplayIR)
    (backend : CertifiedPreprocessorBackend)
    (hselect : registry.selectSupporting policy query ir = some backend) :
    LanguageSpec.Candidate registry query ir backend ∧
      LanguageSpec.LowersTo backend ir (ReplayBackendPlan.fromIR ir) := by
  exact ⟨LanguageSpec.Rule.selection_elim
      registry policy query ir backend hselect,
    LanguageSpec.Rule.selection_lower
      registry policy query ir backend hselect⟩

/-- Exact-key lookup plus support resolves to the canonical plan; resolution also eliminates exactly. -/
theorem CLanguageSpecConformance.resolveRules
    (registry : PreprocessorBackendRegistry)
    (key : BackendKey)
    (ir : ReplayIR)
    (backend : CertifiedPreprocessorBackend)
    (hlookup : registry.lookup key = some backend)
    (hsupport : LanguageSpec.Supports backend ir) :
    LanguageSpec.ResolvesTo registry key ir (ReplayBackendPlan.fromIR ir) ∧
      (∀ plan, LanguageSpec.ResolvesTo registry key ir plan →
        ∃ selected,
          registry.lookup key = some selected ∧
          LanguageSpec.Supports selected ir ∧
          plan = ReplayBackendPlan.fromIR ir) := by
  constructor
  · exact LanguageSpec.Rule.resolve_intro
      registry key ir backend hlookup hsupport
  · intro plan hresolve
    exact LanguageSpec.Rule.resolve_elim registry key ir plan hresolve

/-- Registry Setoid equivalence transports the complete read-side semantic surface. -/
theorem CLanguageSpecConformance.equivalenceAndCongruence
    (wellFormed : WellFormedSelectionPolicy)
    (left right : PreprocessorBackendRegistry)
    (query : BackendQuery)
    (ir : ReplayIR)
    (heq : left ≈ right) :
    (left.supportingCandidates query ir).Perm
        (right.supportingCandidates query ir) ∧
      (left.selectSupporting wellFormed.policy query ir).map
          CertifiedPreprocessorBackend.key =
        (right.selectSupporting wellFormed.policy query ir).map
          CertifiedPreprocessorBackend.key ∧
      (left.selectSupporting wellFormed.policy query ir).map (fun selected =>
          lowerReplayBackendPlan selected.replayCapability ir) =
        (right.selectSupporting wellFormed.policy query ir).map (fun selected =>
          lowerReplayBackendPlan selected.replayCapability ir) := by
  constructor
  · exact LanguageSpec.Rule.candidates_congr heq query ir
  constructor
  · exact LanguageSpec.Rule.selection_congr wellFormed heq query ir
  · exact LanguageSpec.Rule.lowering_congr wellFormed heq query ir

/-- The equivalence rules form the expected reflexive/symmetric/transitive calculus. -/
theorem CLanguageSpecConformance.equivalenceRules
    (first second third : PreprocessorBackendRegistry)
    (h12 : first ≈ second)
    (h23 : second ≈ third) :
    first ≈ first ∧ second ≈ first ∧ first ≈ third := by
  exact ⟨LanguageSpec.Rule.eq_refl first,
    LanguageSpec.Rule.eq_symm h12,
    LanguageSpec.Rule.eq_trans h12 h23⟩

/-- Total and partial mutations use their audited codomain relations. -/
theorem CLanguageSpecConformance.mutationRules
    (left right : PreprocessorBackendRegistry)
    (backend : CertifiedPreprocessorBackend)
    (key : BackendKey)
    (heq : left ≈ right) :
    left.remove key ≈ right.remove key ∧
      PreprocessorBackendRegistry.MutationResultEquivalent
        (left.insert backend) (right.insert backend) ∧
      PreprocessorBackendRegistry.MutationResultEquivalent
        (left.replace backend) (right.replace backend) := by
  exact ⟨LanguageSpec.Rule.remove_congr heq key,
    LanguageSpec.Rule.insert_congr heq backend,
    LanguageSpec.Rule.replace_congr heq backend⟩

end Producer
end CMeta
