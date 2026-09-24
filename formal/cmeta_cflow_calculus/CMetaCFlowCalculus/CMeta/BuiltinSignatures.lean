import CMetaCFlowCalculus.CMeta.SignatureManifest

namespace CMetaCFlowCalculus.CMeta

def builtinUnaryIntToInt : UnaryRelation := { input := "I", output := "I" }
def builtinUnaryIntToBool : UnaryRelation := { input := "I", output := "B" }
def builtinUnaryIntToLong : UnaryRelation := { input := "I", output := "L" }
def builtinUnaryLongToDouble : UnaryRelation := { input := "L", output := "D" }
def builtinUnaryDoubleToInt : UnaryRelation := { input := "D", output := "I" }
def builtinUnaryIntToDouble : UnaryRelation := { input := "I", output := "D" }
def builtinUnaryIntToFloat : UnaryRelation := { input := "I", output := "F" }
def builtinUnaryFloatToDouble : UnaryRelation := { input := "F", output := "D" }
def builtinUnaryDoubleToBool : UnaryRelation := { input := "D", output := "B" }
def builtinUnaryDoubleToDouble : UnaryRelation := { input := "D", output := "D" }

def builtinBinaryLongLongToLong : BinaryRelation :=
  { left := "L", right := "L", output := "L" }

def builtinBinaryLongDoubleToDouble : BinaryRelation :=
  { left := "L", right := "D", output := "D" }

def builtinBinaryDoubleDoubleToDouble : BinaryRelation :=
  { left := "D", right := "D", output := "D" }

def builtinGeneratorIntToLong : GeneratorRelation :=
  { input := "I", output := "L" }

/-- The built-in CMeta type/signature facts. Membership and ABI order are
    deliberately separate so new relations can be appended without renumbering
    existing cmeta_sig values. -/
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
    builtinUnaryIntToInt,
    builtinUnaryIntToBool,
    builtinUnaryIntToLong,
    builtinUnaryLongToDouble,
    builtinUnaryDoubleToInt,
    builtinUnaryIntToDouble,
    builtinUnaryIntToFloat,
    builtinUnaryFloatToDouble,
    builtinUnaryDoubleToBool,
    builtinUnaryDoubleToDouble
  ]
  binary := [
    builtinBinaryLongLongToLong,
    builtinBinaryLongDoubleToDouble,
    builtinBinaryDoubleDoubleToDouble
  ]
  generators := [
    builtinGeneratorIntToLong
  ]
  abiOrder := [
    .unary builtinUnaryIntToInt,
    .unary builtinUnaryIntToBool,
    .unary builtinUnaryIntToLong,
    .unary builtinUnaryLongToDouble,
    .unary builtinUnaryDoubleToInt,
    .unary builtinUnaryIntToDouble,
    .unary builtinUnaryIntToFloat,
    .unary builtinUnaryFloatToDouble,
    .binary builtinBinaryLongLongToLong,
    .binary builtinBinaryLongDoubleToDouble,
    .generator builtinGeneratorIntToLong,
    .unary builtinUnaryDoubleToBool,
    .unary builtinUnaryDoubleToDouble,
    .binary builtinBinaryDoubleDoubleToDouble
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
