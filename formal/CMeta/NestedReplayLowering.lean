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
structure ReplayBackendCapability where
  certifiedSameProducerDepth : Nat
  deriving Repr, DecidableEq

namespace ReplayBackendCapability

/-- A semantic nesting depth is inside the backend's current proof envelope. -/
abbrev supportsSameProducerDepth
    (backend : ReplayBackendCapability) (depth : Nat) : Prop :=
  depth ≤ backend.certifiedSameProducerDepth

end ReplayBackendCapability

/-- Lower a same-producer nesting depth only when it is covered by the current
    backend certificate. The returned depth is unchanged; this function models
    applicability/gating rather than macro expansion itself. -/
def lowerSameProducerDepth
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
theorem lowerSameProducerDepth_progress
    (backend : ReplayBackendCapability) (depth : Nat)
    (h : depth ≤ backend.certifiedSameProducerDepth) :
    ∃ loweredDepth, lowerSameProducerDepth backend depth = some loweredDepth := by
  refine ⟨depth, ?_⟩
  simp [lowerSameProducerDepth,
    ReplayBackendCapability.supportsSameProducerDepth, h]

end Producer
end CMeta
