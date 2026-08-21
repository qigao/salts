import CMeta.NestedReplayLowering
import CMeta.NestedReplayGeneratedC

/-!
# Real C nested Producer replay conformance

The C11 applicability witness executes the actual `Replay` backend spelling.
These checks connect its observed cardinalities to the abstract finite Cartesian
semantics proved in `CMeta.NestedReplay` and expose a conservative lowering
capability derived from the verified backend depth.
-/

namespace CMeta
namespace Producer

private def p : List Nat := [1, 2]
private def q : List Nat := [3, 4, 5]

private def c11ReplayBackend : ReplayBackendCapability :=
  ⟨NestedReplayGeneratedC.certifiedSameProducerDepth⟩

/-- Distinct producer identities can nest directly and must realize the full
    Cartesian product cardinality. -/
theorem CNestedReplayConformance.distinct_producers :
    NestedReplayGeneratedC.distinctCount = p.length * q.length := by
  native_decide

/-- Deferred same-producer replay realizes the square cardinality. -/
theorem CNestedReplayConformance.same_producer_depth2 :
    NestedReplayGeneratedC.depth2Count = p.length ^ 2 := by
  native_decide

/-- A second deferred replay layer composes multiplicatively. -/
theorem CNestedReplayConformance.same_producer_depth3 :
    NestedReplayGeneratedC.depth3Count = p.length ^ 3 := by
  native_decide

/-- A third deferred replay layer composes multiplicatively. -/
theorem CNestedReplayConformance.same_producer_depth4 :
    NestedReplayGeneratedC.depth4Count = p.length ^ 4 := by
  native_decide

/-- The observed two-level same-producer count is the concrete instance of the
    abstract `nestedReplay_same_length` theorem used by the semantic model. -/
theorem CNestedReplayConformance.depth2_matches_model :
    NestedReplayGeneratedC.depth2Count =
      (nestedReplay (fun x y : Nat => x + y) p p).length := by
  rw [nestedReplay_same_length]
  native_decide

/-- The real C witness certifies same-producer deferred replay through depth 4.
    This is a verified lower bound, not a claim that deeper expansion is
    impossible. -/
theorem CNestedReplayConformance.certified_depth :
    c11ReplayBackend.certifiedSameProducerDepth = 4 := by
  native_decide

/-- Any semantic nesting depth within the backend certificate has a lowering. -/
theorem CNestedReplayConformance.within_certificate_realizable
    (depth : Nat)
    (h : depth ≤ c11ReplayBackend.certifiedSameProducerDepth) :
    ∃ loweredDepth,
      lowerSameProducerDepth c11ReplayBackend depth = some loweredDepth := by
  exact lowerSameProducerDepth_progress c11ReplayBackend depth h

/-- The deepest currently certified witness remains realizable. -/
theorem CNestedReplayConformance.depth4_realizable :
    lowerSameProducerDepth c11ReplayBackend 4 = some 4 := by
  native_decide

/-- Depth 5 is outside the current certificate, so lowering conservatively
    rejects it. This does not assert that the C preprocessor can never support
    depth 5; only that this proof slice has not certified it yet. -/
theorem CNestedReplayConformance.depth5_outside_current_certificate :
    lowerSameProducerDepth c11ReplayBackend 5 = none := by
  native_decide

end Producer
end CMeta
