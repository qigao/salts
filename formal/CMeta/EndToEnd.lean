module
import all CMeta.Cardinality
import all CMeta.Graph
import all CMeta.Lowering
import all CMeta.Optimize
import all CMeta.Plan
import all CMeta.Execution

/-!
# End-to-end safety envelope

This module composes the previously kernel-checked layers into two explicit
end-to-end boundaries that match the current CFlow implementation.

1. Structured Graph boundary: typed operators/relations (including ZIP lowered
   to RELATION(INVOKE)) erase to dynamically checkable graph descriptors while
   preserving the final CType.
2. Direct execution boundary: the subset accepted by `cflow_plan_compile`
   compiles to a topology-free plan and executes through runtime type guards;
   every successful result has exactly the compiled plan output CType.

The current direct plan intentionally rejects structured RELATION nodes, so the
second theorem does not pretend that every structured graph is executable by
that backend yet.
-/

namespace CMeta

/-- Structured graph validation preserves the statically derived endpoint. -/
theorem EndToEnd.structured_graph_type_safe {A R : CType}
    (graph : TypedGraph A R) :
    checkGraph A graph.stages = some R :=
  graph.check_stages

/-- ZIP normalization preserves its statically derived result CType before the
    structured relation reaches a runtime backend. -/
theorem EndToEnd.zip_lowering_type_safe {A O : CType}
    (zip : SurfaceZip A O) :
    checkInvokeRelation A zip.lower = some O :=
  zip.lowering_preserves_type

/-- Fused MAP optimization preserves the callback-chain endpoint after all
    intermediate callable indices are erased. -/
theorem EndToEnd.fused_map_type_safe {A R : CType}
    (fused : FusedMap A R) :
    MapChain.check A fused.chain.signatures = some R :=
  fused.type_preserved

/-- Compilation and execution agree simultaneously for every executable direct
    program: the compiled descriptor is well typed and runtime erasure executes
    to the exact typed denotation. -/
theorem EndToEnd.direct_plan_exact {A R : CType}
    (program : ExecProgram A R) (values : ValueVec A) :
    PlanWellTyped program.planProgram.compile ∧
    runRuntimePlan program.runtimeCode ⟨A, values⟩ =
      some ⟨R, program.run values⟩ := by
  exact ⟨program.compiled_plan_well_typed,
    program.runtime_execution_exact values⟩

/-- Main direct-backend type-safety theorem: every successful runtime result
    tag equals the public `cflow_plan.output_type` represented by the compiler
    model. -/
theorem EndToEnd.runtime_result_matches_compiled_output {A R : CType}
    (program : ExecProgram A R) (values : ValueVec A) (out : PackedVec)
    (h : runRuntimePlan program.runtimeCode ⟨A, values⟩ = some out) :
    out.1 = program.planProgram.compile.output := by
  have hout : out.1 = R := program.result_type_safe values out h
  simpa [PlanProgram.compile] using hout

/-- Static topology-free plan validation and successful runtime execution agree
    on the same final logical CType. -/
theorem EndToEnd.static_checker_matches_runtime {A R : CType}
    (program : ExecProgram A R) (values : ValueVec A) (out : PackedVec)
    (h : runRuntimePlan program.runtimeCode ⟨A, values⟩ = some out) :
    checkPlan program.planProgram.compile.input program.planProgram.compile.code =
      some out.1 := by
  have hwf := program.compiled_plan_well_typed
  unfold PlanWellTyped at hwf
  have hout := EndToEnd.runtime_result_matches_compiled_output program values out h
  exact hwf.trans (congrArg some hout.symm)

/-- Endpoint formulation closest to the public plan API: source CType is
    preserved by compilation and a successful result CType equals the compiled
    destination CType. -/
theorem EndToEnd.direct_plan_endpoints {A R : CType}
    (program : ExecProgram A R) (values : ValueVec A) (out : PackedVec)
    (h : runRuntimePlan program.runtimeCode ⟨A, values⟩ = some out) :
    program.planProgram.compile.input = A ∧
    out.1 = program.planProgram.compile.output := by
  constructor
  · rfl
  · exact EndToEnd.runtime_result_matches_compiled_output program values out h

end CMeta
