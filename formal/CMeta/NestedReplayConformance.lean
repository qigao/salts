import CMeta.NestedReplay
import CMeta.NestedReplayGeneratedC

/-!
# Real C nested Producer replay conformance

The C11 applicability witness executes the actual `Replay` backend spelling.
These checks connect its observed cardinalities to the abstract finite Cartesian
semantics proved in `CMeta.NestedReplay`.
-/

namespace CMeta
namespace Producer

private def p : List Nat := [1, 2]
private def q : List Nat := [3, 4, 5]

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

end Producer
end CMeta
