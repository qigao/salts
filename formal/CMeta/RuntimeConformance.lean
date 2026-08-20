import CMeta.PlanConformance
import CMeta.Execution

/-!
# Real C runtime ↔ Lean execution semantics conformance

`CPlanGenerated.runtimeWitnesses` is produced by executing real plans through
`cflow_plan_eval_array`.  Each witness records the input logical type and
values, the runtime result CType/count, and the returned values.

The expected result below is not a second table of outputs: it is computed by
`ExecProgram.run` using the same logical callbacks represented in the C
witness.  The closed conformance theorem therefore differentially checks the
real C direct-plan runtime against the already-proved Lean execution semantics.
-/

namespace CMeta

private def keepI : Callable1 CType.int CType.bool :=
  ⟨fun x => x != 0⟩

private def mapIL : Callable1 CType.int CType.long :=
  ⟨fun x => x⟩

private def mapIIPlusOne : Callable1 CType.int CType.int :=
  ⟨fun x => x + 1⟩

private def mapILTwice : Callable1 CType.int CType.long :=
  ⟨fun x => x * 2⟩

private def flatIL : CompletedGenerator CType.int CType.long :=
  ⟨fun x => [x, x + 10]⟩

private def addL : Callable2 CType.long CType.long CType.long :=
  ⟨fun a b => a + b⟩

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
      some { inputType := "I", outputType := "I",
        output := filterProgram.run input }
  | "map_i_l" =>
      some { inputType := "I", outputType := "L",
        output := mapILProgram.run input }
  | "transform_i_l" =>
      some { inputType := "I", outputType := "L",
        output := mapILProgram.run input }
  | "fused_map_i_i_l" =>
      some { inputType := "I", outputType := "L",
        output := fusedIILProgram.run input }
  | "flat_map_i_l" =>
      some { inputType := "I", outputType := "L",
        output := flatILProgram.run input }
  | "reduce_l" =>
      some { inputType := "L", outputType := "L",
        output := reduceLProgram.run input }
  | "reduce_l_empty" =>
      some { inputType := "L", outputType := "L",
        output := reduceLProgram.run input }
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

/-- The executable runtime witness suite covers every direct execution opcode,
    TRANSFORM→MAP, an actual two-callback fused MAP, expansion, and the empty
    REDUCE edge case. -/
theorem CRuntimeConformance.coverage :
    CPlanGenerated.runtimeWitnesses.map (fun w => w.name) =
      ["filter_i", "map_i_l", "transform_i_l", "fused_map_i_i_l",
       "flat_map_i_l", "reduce_l", "reduce_l_empty"] := by
  native_decide

/-- Main executable runtime-conformance theorem.  For every generated witness,
    the real `cflow_plan_eval_array` result CType, cardinality and values match
    the result computed by the typed `ExecProgram.run` semantics. -/
theorem CRuntimeConformance.matches_execution_model :
    CPlanGenerated.runtimeWitnesses.all runtimeWitnessConforms = true := by
  native_decide

/-- Public extension gate: the authoritative C type universe remains aligned
    and the real direct-plan runtime matches the typed execution model on the
    generated differential suite.  Plan-compiler conformance remains exposed by
    `CImplementationConformance.headers_and_plan_compiler`. -/
theorem CImplementationConformance.headers_and_runtime :
    CGenerated.builtinTypeTokens = ["B", "I", "L", "F", "D"] ∧
    CPlanGenerated.runtimeWitnesses.all runtimeWitnessConforms = true := by
  exact ⟨CImplementationConformance.headers_and_plan_compiler.1,
    CRuntimeConformance.matches_execution_model⟩

end CMeta
