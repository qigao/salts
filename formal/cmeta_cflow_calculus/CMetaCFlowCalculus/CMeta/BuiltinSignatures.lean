import CMetaCFlowCalculus.CMeta.SignatureManifest

namespace CMetaCFlowCalculus.CMeta

/-- The built-in CMeta type/signature facts in stable public ABI order. -/
def builtinSignatureManifest : SignatureManifest where
  types := [
    { token := "B", cType := "CMETA_BOOL_TYPE",
      descriptor := "cmeta_type_bool", kind := "CMETA_T_BOOL",
      traits := "cmeta_traits_bool" },
    { token := "I", cType := "int", descriptor := "cmeta_type_int",
      kind := "CMETA_T_INTEGER", traits := "cmeta_traits_int" },
    { token := "L", cType := "long", descriptor := "cmeta_type_long",
      kind := "CMETA_T_INTEGER", traits := "cmeta_traits_long" },
    { token := "F", cType := "float", descriptor := "cmeta_type_float",
      kind := "CMETA_T_FLOAT", traits := "cmeta_traits_float" },
    { token := "D", cType := "double", descriptor := "cmeta_type_double",
      kind := "CMETA_T_FLOAT", traits := "cmeta_traits_double" }
  ]
  unary := [
    { input := "I", output := "I" },
    { input := "I", output := "B" },
    { input := "I", output := "L" },
    { input := "L", output := "D" },
    { input := "D", output := "I" },
    { input := "I", output := "D" },
    { input := "I", output := "F" },
    { input := "F", output := "D" }
  ]
  binary := [
    { left := "L", right := "L", output := "L" },
    { left := "L", right := "D", output := "D" }
  ]
  generators := [
    { input := "I", output := "L" }
  ]

theorem builtinSignatureManifest_wellFormed :
    builtinSignatureManifest.WellFormed := by
  change builtinSignatureManifest.validate = .ok ()
  rfl

theorem builtinBinaryRelations_finite :
    builtinSignatureManifest.binary.length <
      builtinSignatureManifest.types.length ^ 3 := by
  decide

end CMetaCFlowCalculus.CMeta
