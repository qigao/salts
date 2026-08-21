module
import CMeta.NestedReplay

/-!
# Nested Producer replay backend lowering capability

The semantic `nestedReplay` model is backend-independent. This module models the
separate implementation boundary imposed by a strict-C11 macro backend: a
backend advertises a *certified* same-producer nesting depth, and lowering is
permitted only inside that verified envelope.

`certifiedSameProducerDepth` is deliberately a lower-bound certificate. It is
not an assertion that deeper expansion is impossible; a stronger witness may
raise the certificate later without changing the semantic model.
-/

namespace CMeta
namespace Producer

/-- Conservative capability exposed by a nested-replay lowering backend. -/
public structure ReplayBackendCapability where
  certifiedSameProducerDepth : Nat
  deriving Repr, DecidableEq

namespace ReplayBackendCapability

/-- A semantic nesting depth is inside the backend's current proof envelope. -/
public abbrev supportsSameProducerDepth
    (backend : ReplayBackendCapability) (depth : Nat) : Prop :=
  depth ≤ backend.certifiedSameProducerDepth

end ReplayBackendCapability

/-- Structural surface/IR for producer replay expansion. Producer identity is
    explicit because strict-C11 macro self-suppression depends on whether a
    producer is re-entered while an earlier expansion of the same identity is
    still active. The inductive shape is well-formed by construction. -/
public inductive ReplayIR where
  | emit
  | replay (producer : Nat) (body : ReplayIR)
  deriving Repr, DecidableEq

namespace ReplayIR

/-- Number of active expansions of one producer identity. -/
private def activeMultiplicity (producer : Nat) : List Nat → Nat
  | [] => 0
  | current :: rest =>
      if producer = current
      then activeMultiplicity producer rest + 1
      else activeMultiplicity producer rest

/-- Maximum simultaneous multiplicity of any producer identity on one nested
    replay path. This counts `P → Q → P` as depth two for P because the outer P
    is still active while Q expands. -/
private def sameProducerDepthAux (active : List Nat) : ReplayIR → Nat
  | .emit => 0
  | .replay producer body =>
      let here := activeMultiplicity producer active + 1
      Nat.max here (sameProducerDepthAux (producer :: active) body)

/-- Backend applicability requirement computed directly from structural IR. -/
public def sameProducerDepth (ir : ReplayIR) : Nat :=
  sameProducerDepthAux [] ir

end ReplayIR

/-- Lower a same-producer nesting depth only when it is covered by the current
    backend certificate. The returned depth is unchanged; this function models
    applicability/gating rather than macro expansion itself. -/
public def lowerSameProducerDepth
    (backend : ReplayBackendCapability) (depth : Nat) : Option Nat :=
  if backend.supportsSameProducerDepth depth then some depth else none

/-- The lowering gate succeeds exactly for depths inside the certificate. -/
theorem lowerSameProducerDepth_iff
    (backend : ReplayBackendCapability) (depth : Nat) :
    lowerSameProducerDepth backend depth = some depth ↔
      backend.supportsSameProducerDepth depth := by
  simp [lowerSameProducerDepth, ReplayBackendCapability.supportsSameProducerDepth]

/-- Progress for nested replay lowering: a depth covered by the certificate
    cannot become stuck at the backend applicability boundary. -/
-- TEMP-MODULE-BRIDGE(M7g): legacy NestedReplayConformance.within_certificate_realizable
public theorem lowerSameProducerDepth_progress
    (backend : ReplayBackendCapability) (depth : Nat)
    (h : depth ≤ backend.certifiedSameProducerDepth) :
    ∃ loweredDepth, lowerSameProducerDepth backend depth = some loweredDepth := by
  refine ⟨depth, ?_⟩
  simp [lowerSameProducerDepth,
    ReplayBackendCapability.supportsSameProducerDepth, h]

/-- Backend-facing replay plan. The compiler records the structural source and
    the automatically computed same-producer depth that justified admission. -/
public structure LoweredReplayIR where
  source : ReplayIR
  requiredSameProducerDepth : Nat
  deriving Repr, DecidableEq

/-- Compiler lowering over structural replay IR. The caller supplies no manual
    depth: the requirement is computed from producer identities in the IR and
    then checked through the same certified depth gate used above. -/
public def lowerReplayIR
    (backend : ReplayBackendCapability) (ir : ReplayIR) : Option LoweredReplayIR :=
  match lowerSameProducerDepth backend ir.sameProducerDepth with
  | some requiredDepth => some ⟨ir, requiredDepth⟩
  | none => none

/-- Structural replay lowering succeeds exactly when its automatically computed
    same-producer depth lies inside the backend certificate. -/
theorem lowerReplayIR_isSome_iff
    (backend : ReplayBackendCapability) (ir : ReplayIR) :
    (lowerReplayIR backend ir).isSome = true ↔
      ir.sameProducerDepth ≤ backend.certifiedSameProducerDepth := by
  by_cases h : ir.sameProducerDepth ≤ backend.certifiedSameProducerDepth
  · simp [lowerReplayIR, lowerSameProducerDepth, h]
  · simp [lowerReplayIR, lowerSameProducerDepth, h]

/-- Compiler progress over well-formed-by-construction replay IR: if the IR's
    computed requirement is covered by the certificate, lowering cannot get
    stuck at the backend applicability boundary. -/
-- TEMP-MODULE-BRIDGE(M7g): legacy NestedReplayConformance.ir_within_certificate_realizable
public theorem lowerReplayIR_progress
    (backend : ReplayBackendCapability) (ir : ReplayIR)
    (h : ir.sameProducerDepth ≤ backend.certifiedSameProducerDepth) :
    ∃ loweredIR, lowerReplayIR backend ir = some loweredIR := by
  refine ⟨⟨ir, ir.sameProducerDepth⟩, ?_⟩
  simp [lowerReplayIR, lowerSameProducerDepth, h]

end Producer
end CMeta
