module
import CMeta.NestedReplayBackendPlan
public meta import CMeta.NestedReplayBackendPlan
import CMeta.NestedReplayGccGeneratedC
public meta import CMeta.NestedReplayGccGeneratedC

/-!
# Strict-C11 nested replay applicability conformance

This proof slice imports compile-time applicability evidence from the witnessed
GCC C11 backend. Direct same-producer nesting is expected to be rejected by the
preprocessor, while the deferred+obstruct path is accepted by the executable
witness. The resulting certificate is compared to the symbolic backend plan.
-/

namespace CMeta
namespace Producer

private def reentrantIR : ReplayIR :=
  .replay 1 (.replay 2 (.replay 1 .emit))

private def realBackendRequiresDeferred : Bool :=
  (NestedReplayGccGeneratedC.directSameProducerAccepted == false) &&
  (NestedReplayGccGeneratedC.deferredSameProducerAccepted == true)

private def symbolicPlanRequiresDeferred : Bool :=
  (ReplayExpansionPlan.fromIR reentrantIR).strategyTrace.contains 2

/-- The real strict-C11 negative compilation probe rejects direct same-producer
    nesting on the witnessed GCC backend. -/
theorem CNestedReplayApplicability.direct_same_producer_rejected :
    NestedReplayGccGeneratedC.directSameProducerAccepted = false := by
  native_decide

/-- The deferred+obstruct replay witness itself successfully compiles and runs
    under the exact witnessed GCC strict-C11 configuration. -/
theorem CNestedReplayApplicability.deferred_same_producer_accepted :
    NestedReplayGccGeneratedC.deferredSameProducerAccepted = true := by
  native_decide

/-- The real GCC backend therefore requires the deferred path for this re-entry
    shape. -/
theorem CNestedReplayApplicability.real_backend_requires_deferred :
    realBackendRequiresDeferred = true := by
  native_decide

/-- The compile-time applicability certificate agrees with the symbolic
    expansion plan: the real backend requires deferred replay exactly where the
    plan contains a deferred+obstruct node for the witnessed re-entry shape. -/
theorem CNestedReplayApplicability.requirement_matches_symbolic_plan :
    realBackendRequiresDeferred = symbolicPlanRequiresDeferred := by
  native_decide

end Producer
end CMeta
