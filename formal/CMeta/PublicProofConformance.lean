import CMeta.PublicProof

/-!
# Public proof facade conformance

This module checks that the stable public proof surface is sufficient for the
already-proved end-to-end CMeta/CFlow safety envelope.  It intentionally uses
only `CMeta.PublicProof` as its import boundary.
-/

namespace CMeta
namespace PublicProofConformance

/-- The public structured-graph carrier is only a stable alias. -/
theorem structuredGraphAlias {A R : CType}
    (graph : PublicProof.StructuredGraph A R) :
    checkGraph A graph.stages = some R :=
  PublicProof.structured_graph_type_safe graph

/-- ZIP lowering remains available without importing lowering internals. -/
theorem zipLowering {A O : CType}
    (zip : PublicProof.Zip A O) :
    checkInvokeRelation A zip.lower = some O :=
  PublicProof.zip_lowering_type_safe zip

/-- Optimizer preservation remains available without importing optimizer internals. -/
theorem fusedMapPreservation {A R : CType}
    (fused : PublicProof.FusedMap A R) :
    MapChain.check A fused.chain.signatures = some R :=
  PublicProof.fused_map_type_safe fused

/-- Direct-plan compilation and execution compose through the public facade. -/
theorem directPlanExact {A R : CType}
    (program : PublicProof.DirectProgram A R)
    (values : ValueVec A) :
    PlanWellTyped program.planProgram.compile ∧
      runRuntimePlan program.runtimeCode ⟨A, values⟩ =
        some ⟨R, program.run values⟩ :=
  PublicProof.direct_plan_exact program values

/-- A successful runtime result has exactly the compiled public output type. -/
theorem runtimeOutputTypeSafe {A R : CType}
    (program : PublicProof.DirectProgram A R)
    (values : ValueVec A)
    (out : PublicProof.RuntimeOutput)
    (h : runRuntimePlan program.runtimeCode ⟨A, values⟩ = some out) :
    out.1 = program.planProgram.compile.output :=
  PublicProof.runtime_output_type_safe program values out h

/-- Static plan checking and runtime execution agree on the final CType. -/
theorem staticRuntimeAgreement {A R : CType}
    (program : PublicProof.DirectProgram A R)
    (values : ValueVec A)
    (out : PublicProof.RuntimeOutput)
    (h : runRuntimePlan program.runtimeCode ⟨A, values⟩ = some out) :
    checkPlan program.planProgram.compile.input program.planProgram.compile.code =
      some out.1 :=
  PublicProof.static_checker_matches_runtime program values out h

end PublicProofConformance
end CMeta
