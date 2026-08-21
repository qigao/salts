import CMeta.EndToEnd

/-!
# Stable public proof surface

This module is a thin facade over the already kernel-checked end-to-end CMeta
semantics.  It introduces no second execution, graph, optimizer, lowering, or
plan model.  Carrier names are transparent aliases and every theorem delegates
to an existing `EndToEnd` result.
-/

namespace CMeta
namespace PublicProof

/-! ## Stable carriers -/

abbrev StructuredGraph := TypedGraph
abbrev Zip := SurfaceZip
abbrev FusedMap := CMeta.FusedMap
abbrev DirectProgram := ExecProgram
abbrev RuntimeOutput := PackedVec

/-! ## Stable end-to-end rules -/

/-- Structured graph erasure and validation recover the exact static endpoint. -/
theorem structured_graph_type_safe {A R : CType}
    (graph : StructuredGraph A R) :
    checkGraph A graph.stages = some R :=
  EndToEnd.structured_graph_type_safe graph

/-- Surface ZIP lowering preserves its exact result CType. -/
theorem zip_lowering_type_safe {A O : CType}
    (zip : Zip A O) :
    checkInvokeRelation A zip.lower = some O :=
  EndToEnd.zip_lowering_type_safe zip

/-- Fused-map optimization preserves the callback-chain endpoint. -/
theorem fused_map_type_safe {A R : CType}
    (fused : FusedMap A R) :
    MapChain.check A fused.chain.signatures = some R :=
  EndToEnd.fused_map_type_safe fused

/-- Direct compilation and runtime execution agree exactly. -/
theorem direct_plan_exact {A R : CType}
    (program : DirectProgram A R) (values : ValueVec A) :
    PlanWellTyped program.planProgram.compile ∧
      runRuntimePlan program.runtimeCode ⟨A, values⟩ =
        some ⟨R, program.run values⟩ :=
  EndToEnd.direct_plan_exact program values

/-- Every successful runtime output has the compiled plan's public output CType. -/
theorem runtime_output_type_safe {A R : CType}
    (program : DirectProgram A R) (values : ValueVec A) (out : RuntimeOutput)
    (h : runRuntimePlan program.runtimeCode ⟨A, values⟩ = some out) :
    out.1 = program.planProgram.compile.output :=
  EndToEnd.runtime_result_matches_compiled_output program values out h

/-- Static plan validation and successful runtime execution recover one CType. -/
theorem static_checker_matches_runtime {A R : CType}
    (program : DirectProgram A R) (values : ValueVec A) (out : RuntimeOutput)
    (h : runRuntimePlan program.runtimeCode ⟨A, values⟩ = some out) :
    checkPlan program.planProgram.compile.input program.planProgram.compile.code =
      some out.1 :=
  EndToEnd.static_checker_matches_runtime program values out h

end PublicProof
end CMeta
