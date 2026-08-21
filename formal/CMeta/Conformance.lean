module
import all CMeta.EndToEnd
import all CMeta.GeneratedC

/-!
# C header ↔ Lean model conformance

`CMeta.GeneratedC` is generated from the authoritative C headers by
`formal/cmeta_conformance_gen.c`. CI regenerates that file and rejects any diff
before the Lean kernel check runs.  The theorems below therefore connect the
actual C header declarations to the formal model rather than comparing two
independently maintained Lean tables.
-/

namespace CMeta

private def cTypeToken : CType → String
  | .bool => "B"
  | .int => "I"
  | .long => "L"
  | .float => "F"
  | .double => "D"

private def signatureId : Signature → String
  | .unary input output =>
      "U_" ++ cTypeToken input ++ "_" ++ cTypeToken output
  | .binary left right output =>
      "B_" ++ cTypeToken left ++ "_" ++ cTypeToken right ++ "_" ++
        cTypeToken output
  | .generator input output =>
      "G_" ++ cTypeToken input ++ "_" ++ cTypeToken output

private def builtinCTypes : List CType :=
  [.bool, .int, .long, .float, .double]

/-- `CMETA_BUILTIN_TYPE_LIST` and the Lean CType universe have the same built-in
    order and tokens. -/
theorem CHeaderConformance.type_universe :
    CGenerated.builtinTypeTokens = builtinCTypes.map cTypeToken := by
  decide

private def generatedPolicyIds : Operator → List String
  | .filter => CGenerated.filterPolicyIds
  | .map => CGenerated.mapPolicyIds
  | .transform => CGenerated.transformPolicyIds
  | .flatMap => CGenerated.flatMapPolicyIds
  | .reduce => CGenerated.reducePolicyIds
  | .zip => CGenerated.zipPolicyIds

/-- Every built-in signature enabled by `operator_policy.h` is exactly the
    signature enabled by the Lean `cflowBuiltInPolicy`, in the same order. -/
theorem CHeaderConformance.operator_policy (op : Operator) :
    generatedPolicyIds op = (cflowBuiltInPolicy op).map signatureId := by
  cases op <;> decide

private def formalOperatorSchemas : List CGenerated.OperatorSchemaRow :=
  [
    { enumName := "FILTER", method := "filter", methodArgc := 1,
      fnArg := 0, subgraphArg := -1, fnArity := 1,
      p0 := "INPUT", p1 := "NONE", p2 := "NONE", ret := "BOOL",
      output := "SAME", cardinality := "FILTER", subgraphRule := "NONE",
      semantic := "filter", effects := "CMETA_EFFECT_PURE" },
    { enumName := "MAP", method := "map", methodArgc := 1,
      fnArg := 0, subgraphArg := -1, fnArity := 1,
      p0 := "INPUT", p1 := "NONE", p2 := "NONE", ret := "VALUE",
      output := "RETURN", cardinality := "ONE", subgraphRule := "NONE",
      semantic := "map", effects := "CMETA_EFFECT_PURE" },
    { enumName := "TRANSFORM", method := "transform", methodArgc := 1,
      fnArg := 0, subgraphArg := -1, fnArity := 1,
      p0 := "INPUT", p1 := "NONE", p2 := "NONE", ret := "VALUE",
      output := "RETURN", cardinality := "ONE", subgraphRule := "NONE",
      semantic := "map", effects := "CMETA_EFFECT_PURE" },
    { enumName := "FLAT_MAP", method := "flatMap", methodArgc := 1,
      fnArg := 0, subgraphArg := -1, fnArity := 3,
      p0 := "INPUT", p1 := "OUT_PTR", p2 := "CURSOR", ret := "GENERATOR",
      output := "POINTEE1", cardinality := "EXPAND", subgraphRule := "NONE",
      semantic := "flat_map", effects := "CMETA_EFFECT_PURE" },
    { enumName := "REDUCE", method := "reduce", methodArgc := 1,
      fnArg := 0, subgraphArg := -1, fnArity := 2,
      p0 := "INPUT", p1 := "INPUT", p2 := "NONE", ret := "INPUT",
      output := "SAME", cardinality := "REDUCE", subgraphRule := "NONE",
      semantic := "reduce", effects := "CMETA_EFFECT_STATEFUL" },
    { enumName := "ZIP", method := "zip", methodArgc := 2,
      fnArg := 1, subgraphArg := 0, fnArity := 2,
      p0 := "INPUT", p1 := "SUBGRAPH", p2 := "NONE", ret := "VALUE",
      output := "RETURN", cardinality := "ONE",
      subgraphRule := "SUBGRAPH_1TO1", semantic := "high_level",
      effects := "CMETA_EFFECT_PURE" }
  ]

/-- The authoritative `CFlowOperators` declaration has exactly the schema shape
    assumed by the formal operator/lowering/cardinality model. -/
theorem CHeaderConformance.operator_schema :
    CGenerated.operatorSchemas = formalOperatorSchemas := by
  decide

/-- A compact conjunction useful as the single header-conformance gate. -/
theorem CHeaderConformance.headers_match_formal_model :
    CGenerated.builtinTypeTokens = builtinCTypes.map cTypeToken ∧
    (∀ op, generatedPolicyIds op = (cflowBuiltInPolicy op).map signatureId) ∧
    CGenerated.operatorSchemas = formalOperatorSchemas := by
  exact ⟨CHeaderConformance.type_universe,
    CHeaderConformance.operator_policy,
    CHeaderConformance.operator_schema⟩

end CMeta
