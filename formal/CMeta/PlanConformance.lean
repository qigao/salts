import CMeta.Conformance
import CMeta.PlanGeneratedC

/-!
# Real C plan compiler ↔ Lean plan model conformance

`CMeta.CPlanGenerated` is emitted by a C executable that constructs surface
Graphs through the public CFlow API, invokes the real normalize/optimize/plan
compiler path, then inspects the resulting `cflow_plan_inst[]` descriptors.
CI regenerates the snapshot before the Lean build and rejects any diff.

This file decodes those observed C descriptors into the existing Lean
`ErasedPlan` model and asks the already-proved `checkPlan` validator to accept
every witness.  The bridge therefore checks compiler output rather than
re-stating `plan_compile.c` in Lean.
-/

namespace CMeta

private def decodeType : String → Option CType
  | "B" => some .bool
  | "I" => some .int
  | "L" => some .long
  | "F" => some .float
  | "D" => some .double
  | _ => none

private def decodeOpcode : String → Option PlanOpcode
  | "FILTER" => some .filter
  | "MAP" => some .map
  | "FLAT_MAP" => some .flatMap
  | "REDUCE" => some .reduce
  | _ => none

private def decodeSignature : String → Option Signature
  | "U_I_B" => some (.unary .int .bool)
  | "U_I_I" => some (.unary .int .int)
  | "U_I_L" => some (.unary .int .long)
  | "U_L_D" => some (.unary .long .double)
  | "U_D_I" => some (.unary .double .int)
  | "U_I_D" => some (.unary .int .double)
  | "U_I_F" => some (.unary .int .float)
  | "U_F_D" => some (.unary .float .double)
  | "G_I_L" => some (.generator .int .long)
  | "B_L_L_L" => some (.binary .long .long .long)
  | "B_L_D_D" => some (.binary .long .double .double)
  | _ => none

private def decodeSignatures : List String → Option (List Signature)
  | [] => some []
  | id :: rest => do
      let sig ← decodeSignature id
      let tail ← decodeSignatures rest
      pure (sig :: tail)

private def decodeInst
    (row : CPlanGenerated.PlanInstRow) : Option ErasedPlanInst := do
  let opcode ← decodeOpcode row.opcode
  let input ← decodeType row.input
  let output ← decodeType row.output
  let callbacks ← decodeSignatures row.callbacks
  pure { opcode, input, output, callbacks }

private def decodeCode : List CPlanGenerated.PlanInstRow →
    Option (List ErasedPlanInst)
  | [] => some []
  | row :: rest => do
      let inst ← decodeInst row
      let tail ← decodeCode rest
      pure (inst :: tail)

private def decodeWitness
    (w : CPlanGenerated.PlanWitness) : Option ErasedPlan := do
  let input ← decodeType w.input
  let output ← decodeType w.output
  let code ← decodeCode w.code
  pure { input, output, code }

private def witnessConforms (w : CPlanGenerated.PlanWitness) : Bool :=
  match decodeWitness w with
  | some plan => decide (PlanWellTyped plan)
  | none => false

/-- The C witness suite deliberately exercises every direct-plan constructor
    shape, including TRANSFORM canonicalization and a two-callback fused MAP. -/
theorem CPlanCompilerConformance.coverage :
    CPlanGenerated.witnesses.map (fun w => w.name) =
      ["filter_i", "map_i_l", "transform_i_l", "fused_map_i_l_d",
       "flat_map_i_l", "reduce_l"] := by
  decide

/-- Every descriptor sequence produced by the real C plan compiler witness
    suite decodes to a `PlanWellTyped` Lean plan.  In particular, this checks
    opcode choice, public endpoints, every instruction endpoint, callback
    signature shape, and fused MAP callback order. -/
theorem CPlanCompilerConformance.generated_plans_well_typed :
    CPlanGenerated.witnesses.all witnessConforms = true := by
  decide

/-- Compact conformance gate combining header-generation and real plan-compiler
    witnesses. -/
theorem CImplementationConformance.headers_and_plan_compiler :
    (CGenerated.builtinTypeTokens =
      [.bool, .int, .long, .float, .double].map
        (fun t => match t with
          | .bool => "B" | .int => "I" | .long => "L"
          | .float => "F" | .double => "D")) ∧
    CPlanGenerated.witnesses.all witnessConforms = true := by
  constructor
  · exact CHeaderConformance.type_universe
  · exact CPlanCompilerConformance.generated_plans_well_typed

end CMeta
