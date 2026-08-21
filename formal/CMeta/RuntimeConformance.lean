module
import all CMeta.PlanConformance
-- TEMP-MODULE-BRIDGE(M6): legacy StructuredConformance needs CType/Callable/ValueVec semantics
public import CMeta.Execution
import all CMeta.Execution

/-!
# Real C runtime ↔ Lean execution semantics conformance

`CPlanGenerated.runtimeWitnesses` is produced by executing real plans through
`cflow_plan_eval_array`. Each witness records the input logical type and
values, the runtime result CType/count, and the returned values.

`CPlanGenerated.differentialWitnesses` goes one step further: one surface Graph
is executed through the general `cflow_eval_array` runtime and, without changing
the surface program, through `cflow_plan_compile_surface` followed by
`cflow_plan_eval_array`.

The expected result below is not a second table of outputs: it is computed by
`ExecProgram.run` using the same logical callbacks represented in the C
witnesses. The closed conformance theorems therefore check both C backends
against each other and against the already-proved Lean execution semantics.
-/

namespace CMeta

private def keepI : Callable [CType.int] CType.bool :=
  Callable.ofUnary (fun (x : Int) => x != 0)

private def mapIL : Callable [CType.int] CType.long :=
  Callable.ofUnary (fun (x : Int) => x)

private def mapIIPlusOne : Callable [CType.int] CType.int :=
  Callable.ofUnary (fun (x : Int) => x + 1)

private def mapILTwice : Callable [CType.int] CType.long :=
  Callable.ofUnary (fun (x : Int) => x * 2)

private def flatIL : CompletedGenerator CType.int CType.long :=
  ⟨fun (x : Int) => [x, x + 10]⟩

private def addL : Callable [CType.long, CType.long] CType.long :=
  Callable.ofBinary (fun (a b : Int) => a + b)

private def mapILChain : MapChain CType.int CType.long :=
  .cons mapIL (.done CType.long)

private def fusedIILChain : MapChain CType.int CType.long :=
  .cons mapIIPlusOne (.cons mapILTwice (.done CType.long))

private def filterProgram : ExecProgram CType.int CType.int :=
  .cons (.filter CType.int keepI) (.done CType.int)

private def mapILProgram : ExecProgram CType.int CType.long :=
  .cons (.map CType.int CType.long mapILChain) (.done CType.long)

private def fusedIILProgram : ExecProgram CType.int CType.long :=
  .cons (.map CType.int CType.long fusedIILChain) (.done CType.long)

private def flatILProgram : ExecProgram CType.int CType.long :=
  .cons (.flatMap CType.int CType.long flatIL) (.done CType.long)

private def reduceLProgram : ExecProgram CType.long CType.long :=
  .cons (.reduce CType.long addL) (.done CType.long)

private structure RuntimeModelResult where
  inputType : String
  outputType : String
  output : List Int

private def runtimeModel
    (name : String) (input : List Int) : Option RuntimeModelResult :=
  match name with
  | "filter_i" =>
      some ⟨"I", "I", filterProgram.run input⟩
  | "map_i_l" =>
      some ⟨"I", "L", mapILProgram.run input⟩
  | "transform_i_l" =>
      some ⟨"I", "L", mapILProgram.run input⟩
  | "fused_map_i_i_l" =>
      some ⟨"I", "L", fusedIILProgram.run input⟩
  | "flat_map_i_l" =>
      some ⟨"I", "L", flatILProgram.run input⟩
  | "reduce_l" =>
      some ⟨"L", "L", reduceLProgram.run input⟩
  | "reduce_l_empty" =>
      some ⟨"L", "L", reduceLProgram.run input⟩
  | _ => none

private def runtimeWitnessConforms
    (w : CPlanGenerated.RuntimeWitness) : Bool :=
  match runtimeModel w.name w.input with
  | some expected =>
      w.inputType == expected.inputType &&
      w.outputType == expected.outputType &&
      w.count == expected.output.length &&
      w.output == expected.output
  | none => false

private def differentialBackendsAgree
    (w : CPlanGenerated.DifferentialWitness) : Bool :=
  w.referenceOutputType == w.planOutputType &&
  w.referenceCount == w.planCount &&
  w.referenceOutput == w.planOutput

private def differentialWitnessConforms
    (w : CPlanGenerated.DifferentialWitness) : Bool :=
  match runtimeModel w.name w.input with
  | some expected =>
      w.inputType == expected.inputType &&
      w.referenceOutputType == expected.outputType &&
      w.referenceCount == expected.output.length &&
      w.referenceOutput == expected.output &&
      w.planOutputType == expected.outputType &&
      w.planCount == expected.output.length &&
      w.planOutput == expected.output &&
      differentialBackendsAgree w
  | none => false

/-- The executable runtime witness suite covers every direct execution opcode,
    TRANSFORM→MAP, an actual two-callback fused MAP, expansion, and the empty
    REDUCE edge case. -/
theorem CRuntimeConformance.coverage :
    CPlanGenerated.runtimeWitnesses.map (fun w => w.name) =
      ["filter_i", "map_i_l", "transform_i_l", "fused_map_i_i_l",
       "flat_map_i_l", "reduce_l", "reduce_l_empty"] := by
  native_decide

/-- Main executable runtime-conformance theorem. For every generated witness,
    the real `cflow_plan_eval_array` result CType, cardinality and values match
    the result computed by the typed `ExecProgram.run` semantics. -/
theorem CRuntimeConformance.matches_execution_model :
    CPlanGenerated.runtimeWitnesses.all runtimeWitnessConforms = true := by
  native_decide

/-- The backend differential suite uses the same semantic coverage as the
    direct runtime suite. -/
theorem CBackendDifferential.coverage :
    CPlanGenerated.differentialWitnesses.map (fun w => w.name) =
      ["filter_i", "map_i_l", "transform_i_l", "fused_map_i_i_l",
       "flat_map_i_l", "reduce_l", "reduce_l_empty"] := by
  native_decide

/-- Running one surface Graph through the general scheduler/runtime and through
    the normalize/optimize/direct-plan backend yields identical logical CType,
    cardinality and values for every differential witness. -/
theorem CBackendDifferential.surface_and_plan_agree :
    CPlanGenerated.differentialWitnesses.all differentialBackendsAgree = true := by
  native_decide

/-- Three-way conformance: both real C execution backends agree with each other
    and with the result computed by the typed Lean `ExecProgram.run` model. -/
theorem CBackendDifferential.surface_plan_and_model_agree :
    CPlanGenerated.differentialWitnesses.all differentialWitnessConforms = true := by
  native_decide

/-- Public extension gate: the authoritative C type universe remains aligned
    and the real direct-plan runtime matches the typed execution model on the
    generated runtime suite. -/
theorem CImplementationConformance.headers_and_runtime :
    CGenerated.builtinTypeTokens = ["B", "I", "L", "F", "D"] ∧
    CPlanGenerated.runtimeWitnesses.all runtimeWitnessConforms = true := by
  exact ⟨CImplementationConformance.headers_and_plan_compiler.1,
    CRuntimeConformance.matches_execution_model⟩

/-- Strongest executable implementation gate currently modeled: the C header
    universe is aligned, direct-plan execution matches the typed model, and the
    general surface runtime agrees with the direct-plan backend and the same
    formal semantics. -/
theorem CImplementationConformance.headers_runtime_and_backend_differential :
    CGenerated.builtinTypeTokens = ["B", "I", "L", "F", "D"] ∧
    CPlanGenerated.runtimeWitnesses.all runtimeWitnessConforms = true ∧
    CPlanGenerated.differentialWitnesses.all differentialWitnessConforms = true := by
  exact ⟨CImplementationConformance.headers_and_runtime.1,
    CImplementationConformance.headers_and_runtime.2,
    CBackendDifferential.surface_plan_and_model_agree⟩

end CMeta
