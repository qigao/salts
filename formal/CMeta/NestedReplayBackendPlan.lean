import CMeta.NestedReplayLowering
import CMeta.PreprocessorBackend

/-!
# Strict-C11 nested replay backend plan

This layer turns an already-structured `ReplayIR` into explicit preprocessor
expansion choices. It separates ordinary direct replay from the deferred and
obstructed path required when the same producer identity is re-entered while an
outer expansion is still active.
-/

namespace CMeta
namespace Producer

/-- Concrete strict-C11 expansion choice for each replay node. -/
inductive ReplayExpansionPlan where
  | emit
  | directReplay (producer : Nat) (body : ReplayExpansionPlan)
  | deferredObstructReplay (producer : Nat) (body : ReplayExpansionPlan)
  deriving Repr, DecidableEq

namespace ReplayExpansionPlan

/-- Direct replay is legal only for an inactive identity. An active
    same-identity re-entry must use the deferred-obstruct path. -/
def respectsActiveProducers : ReplayExpansionPlan → List Nat → Prop
  | .emit, _ => True
  | .directReplay producer body, active =>
      producer ∉ active ∧ body.respectsActiveProducers (producer :: active)
  | .deferredObstructReplay producer body, active =>
      producer ∈ active ∧ body.respectsActiveProducers (producer :: active)

/-- Compile structural replay IR into direct/deferred backend choices. -/
private def fromIRAux (active : List Nat) : ReplayIR → ReplayExpansionPlan
  | .emit => .emit
  | .replay producer body =>
      if producer ∈ active then
        .deferredObstructReplay producer (fromIRAux (producer :: active) body)
      else
        .directReplay producer (fromIRAux (producer :: active) body)

/-- Top-level expansion starts with no active producer identities. -/
def fromIR (ir : ReplayIR) : ReplayExpansionPlan :=
  fromIRAux [] ir

/-- Compact diagnostic encoding used by the real C conformance witness:
    `1` means direct producer replay and `2` means deferred+obstructed replay.
    Producer values are intentionally omitted so the trace describes expansion
    strategy rather than test data. -/
def strategyTrace : ReplayExpansionPlan → List Nat
  | .emit => []
  | .directReplay _ body => 1 :: body.strategyTrace
  | .deferredObstructReplay _ body => 2 :: body.strategyTrace

/-- Compilation never emits a direct replay for an identity already active on
    the expansion stack. -/
private theorem fromIRAux_respects (active : List Nat) (ir : ReplayIR) :
    (fromIRAux active ir).respectsActiveProducers active := by
  induction ir generalizing active with
  | emit =>
      simp [fromIRAux, respectsActiveProducers]
  | replay producer body ih =>
      by_cases h : producer ∈ active
      · simp [fromIRAux, h, respectsActiveProducers, ih]
      · simp [fromIRAux, h, respectsActiveProducers, ih]

/-- Every generated top-level expansion plan is safe with respect to strict-C11
    macro self-suppression. -/
theorem fromIR_respects (ir : ReplayIR) :
    (fromIR ir).respectsActiveProducers [] := by
  exact fromIRAux_respects [] ir

end ReplayExpansionPlan

/-- Fully accepted strict-C11 backend plan. `requiredRescanDepth` is the logical
    nested re-entry depth that the backend certificate must cover; it is not the
    literal number of preprocessor `EVAL` scans. -/
structure ReplayBackendPlan where
  source : ReplayIR
  requiredRescanDepth : Nat
  expansion : ReplayExpansionPlan
  deriving Repr, DecidableEq

namespace ReplayBackendPlan

/-- The unique backend plan determined by replay structure itself. Backend
    capability decides only whether this plan is accepted; it does not alter the
    plan once admission succeeds. -/
def fromIR (ir : ReplayIR) : ReplayBackendPlan :=
  ⟨ir, ir.sameProducerDepth, ReplayExpansionPlan.fromIR ir⟩

end ReplayBackendPlan

/-- Recover the exact IR-computed requirement from any successful structural
    lowering. -/
theorem lowerReplayIR_requirement
    (backend : ReplayBackendCapability) (ir : ReplayIR) (lowered : LoweredReplayIR)
    (h : lowerReplayIR backend ir = some lowered) :
    lowered.requiredSameProducerDepth = ir.sameProducerDepth := by
  by_cases hs : ir.sameProducerDepth ≤ backend.certifiedSameProducerDepth
  · simp [lowerReplayIR, lowerSameProducerDepth, hs] at h
    cases h
    rfl
  · simp [lowerReplayIR, lowerSameProducerDepth, hs] at h

/-- Produce a backend expansion plan only after structural IR applicability has
    passed the certified-depth gate. -/
def lowerReplayBackendPlan
    (backend : ReplayBackendCapability) (ir : ReplayIR) : Option ReplayBackendPlan :=
  match lowerReplayIR backend ir with
  | none => none
  | some lowered =>
      some ⟨ir, lowered.requiredSameProducerDepth, ReplayExpansionPlan.fromIR ir⟩

/-- Once a backend supports the IR's required replay depth, lowering normalizes
    to the one canonical plan determined entirely by that IR. -/
theorem lowerReplayBackendPlan_eq_canonical_of_supports
    (backend : ReplayBackendCapability) (ir : ReplayIR)
    (h : ir.sameProducerDepth ≤ backend.certifiedSameProducerDepth) :
    lowerReplayBackendPlan backend ir = some (ReplayBackendPlan.fromIR ir) := by
  simp [lowerReplayBackendPlan, lowerReplayIR, lowerSameProducerDepth,
    ReplayBackendPlan.fromIR, h]

/-- Successful plan generation is exactly support for the requested IR plus the
    canonical plan identity. -/
theorem lowerReplayBackendPlan_eq_some_iff
    (backend : ReplayBackendCapability) (ir : ReplayIR) (plan : ReplayBackendPlan) :
    lowerReplayBackendPlan backend ir = some plan ↔
      ir.sameProducerDepth ≤ backend.certifiedSameProducerDepth ∧
      plan = ReplayBackendPlan.fromIR ir := by
  by_cases hs : ir.sameProducerDepth ≤ backend.certifiedSameProducerDepth
  · have hc := lowerReplayBackendPlan_eq_canonical_of_supports backend ir hs
    constructor
    · intro hplan
      rw [hc] at hplan
      exact ⟨hs, (Option.some.inj hplan).symm⟩
    · rintro ⟨_, rfl⟩
      exact hc
  · constructor
    · intro hplan
      simp [lowerReplayBackendPlan, lowerReplayIR, lowerSameProducerDepth, hs] at hplan
    · rintro ⟨h, _⟩
      exact (hs h).elim

/-- Successful backend plan generation preserves the IR-derived logical rescan
    requirement. -/
theorem lowerReplayBackendPlan_requirement
    (backend : ReplayBackendCapability) (ir : ReplayIR) (plan : ReplayBackendPlan)
    (h : lowerReplayBackendPlan backend ir = some plan) :
    plan.requiredRescanDepth = ir.sameProducerDepth := by
  cases hl : lowerReplayIR backend ir with
  | none =>
      simp [lowerReplayBackendPlan, hl] at h
  | some lowered =>
      have hreq := lowerReplayIR_requirement backend ir lowered hl
      simp [lowerReplayBackendPlan, hl] at h
      subst plan
      exact hreq

/-- Successful backend plan generation cannot contain a direct replay of an
    already-active producer identity. -/
theorem lowerReplayBackendPlan_respects_active
    (backend : ReplayBackendCapability) (ir : ReplayIR) (plan : ReplayBackendPlan)
    (h : lowerReplayBackendPlan backend ir = some plan) :
    plan.expansion.respectsActiveProducers [] := by
  cases hl : lowerReplayIR backend ir with
  | none =>
      simp [lowerReplayBackendPlan, hl] at h
  | some lowered =>
      simp [lowerReplayBackendPlan, hl] at h
      subst plan
      exact ReplayExpansionPlan.fromIR_respects ir

/-- Any two certified preprocessors lower the same replay IR identically when
    they expose the same replay capability. Kept as a compatibility corollary;
    the stronger theorem below needs only support for this particular IR. -/
theorem certifiedReplayLowering_eq_of_capability_eq
    (a b : CertifiedPreprocessorBackend) (ir : ReplayIR)
    (hcap : a.replayCapability = b.replayCapability) :
    lowerReplayBackendPlan a.replayCapability ir =
      lowerReplayBackendPlan b.replayCapability ir := by
  rw [hcap]

/-- Portability is local to the requested IR: two certified preprocessors need
    not expose identical capability envelopes. If both support this IR, both
    normalize to the same canonical backend plan. -/
theorem certifiedReplayLowering_eq_of_both_supports
    (a b : CertifiedPreprocessorBackend) (ir : ReplayIR)
    (ha : ir.sameProducerDepth ≤ a.replayCapability.certifiedSameProducerDepth)
    (hb : ir.sameProducerDepth ≤ b.replayCapability.certifiedSameProducerDepth) :
    lowerReplayBackendPlan a.replayCapability ir =
      lowerReplayBackendPlan b.replayCapability ir := by
  rw [lowerReplayBackendPlan_eq_canonical_of_supports a.replayCapability ir ha]
  rw [lowerReplayBackendPlan_eq_canonical_of_supports b.replayCapability ir hb]

namespace PreprocessorBackendRegistry

/-- Registry selection and lowering are deliberately separate: lookup chooses
    the certified backend identity; the selected backend capability then gates
    the canonical replay plan. -/
def resolveReplay
    (registry : PreprocessorBackendRegistry) (key : BackendKey) (ir : ReplayIR) :
    Option ReplayBackendPlan :=
  match registry.lookup key with
  | none => none
  | some backend => lowerReplayBackendPlan backend.replayCapability ir

/-- A registry resolution succeeds exactly when the key selects a certified
    backend that supports the requested IR, and the result is the IR's canonical
    replay plan. -/
theorem resolveReplay_eq_some_iff
    (registry : PreprocessorBackendRegistry) (key : BackendKey)
    (ir : ReplayIR) (plan : ReplayBackendPlan) :
    registry.resolveReplay key ir = some plan ↔
      ∃ backend,
        registry.lookup key = some backend ∧
        backend.supportsReplay ir ∧
        plan = ReplayBackendPlan.fromIR ir := by
  cases hlookup : registry.lookup key with
  | none =>
      simp [resolveReplay, hlookup]
  | some backend =>
      simp [resolveReplay, hlookup, lowerReplayBackendPlan_eq_some_iff,
        CertifiedPreprocessorBackend.supportsReplay]

end PreprocessorBackendRegistry

end Producer
end CMeta
