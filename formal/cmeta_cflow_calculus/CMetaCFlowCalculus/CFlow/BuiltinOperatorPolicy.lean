import CMetaCFlowCalculus.CMeta.BuiltinSignatures
import CMetaCFlowCalculus.CFlow.OperatorPolicyManifest

namespace CMetaCFlowCalculus.CFlow

open CMeta

/-- The current CFlow operator admission facts in stable generated order. -/
def builtinOperatorPolicy : OperatorPolicyManifest where
  filter := [builtinUnaryIntToBool]
  map := [
    builtinUnaryIntToInt,
    builtinUnaryIntToLong,
    builtinUnaryLongToDouble,
    builtinUnaryDoubleToInt,
    builtinUnaryIntToDouble,
    builtinUnaryIntToFloat,
    builtinUnaryFloatToDouble
  ]
  transform := [builtinUnaryIntToLong]
  flatMap := [builtinGeneratorIntToLong]
  reduce := [builtinBinaryLongLongToLong]
  zip := [builtinBinaryLongDoubleToDouble]

theorem builtinOperatorPolicy_wellFormed :
    builtinOperatorPolicy.WellFormed builtinSignatureManifest := by
  change builtinOperatorPolicy.validate builtinSignatureManifest = .ok ()
  rfl

theorem builtinOperatorPolicy_coversRegistry :
    builtinSignatureManifest.unary.all
        (fun relation => builtinOperatorPolicy.allUnary.contains relation) &&
      builtinSignatureManifest.binary.all
        (fun relation => builtinOperatorPolicy.allBinary.contains relation) &&
      builtinSignatureManifest.generators.all
        (fun relation => builtinOperatorPolicy.allGenerators.contains relation) = true := by
  decide

end CMetaCFlowCalculus.CFlow
