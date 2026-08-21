import CMeta.NestedReplayLowering

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

/-- Fully admitted strict-C11 backend plan. `requiredRescanDepth` is the logical
    nested re-entry depth that the backend certificate must cover; it is not the
    literal number of preprocessor `EVAL` scans. -/
structure ReplayBackendPlan where
  source : ReplayIR
  requiredRescanDepth : Nat
  expansion : ReplayExpansionPlan
  deriving Repr, DecidableEq

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

end Producer
end CMeta
