import CMetaCFlowCalculus.CFlow.OperatorPolicyManifest
import CMetaCFlowCalculus.CFlow.BuiltinOperatorPolicy
import CMetaCFlowCalculus.CFlow.OperatorPolicyHeader

open CMetaCFlowCalculus.CMeta
open CMetaCFlowCalculus.CFlow

namespace CMetaCFlowCalculus.Tests.OperatorPolicyManifest

def unregisteredPolicy : OperatorPolicyManifest :=
  { builtinOperatorPolicy with
    map := [{ input := "I", output := "Z" }] }

def uncoveredPolicy : OperatorPolicyManifest :=
  { builtinOperatorPolicy with
    map := builtinOperatorPolicy.map.drop 1 }

def invalidFilterShapePolicy : OperatorPolicyManifest :=
  { builtinOperatorPolicy with
    filter := [builtinUnaryIntToInt]
    map := builtinUnaryIntToBool :: builtinOperatorPolicy.map }

def invalidReduceShapePolicy : OperatorPolicyManifest :=
  { builtinOperatorPolicy with
    reduce := [builtinBinaryLongDoubleToDouble]
    zip := [builtinBinaryLongLongToLong] }

def duplicateMapPolicy : OperatorPolicyManifest :=
  { builtinOperatorPolicy with
    map := builtinOperatorPolicy.map ++ builtinOperatorPolicy.map.take 1 }

def renderedPolicyHasRequiredMacros : Bool :=
  match OperatorPolicyHeader.render builtinSignatureManifest
      builtinOperatorPolicy with
  | .error _ => false
  | .ok header =>
      (header.splitOn "CFLOW_BUILTIN_MAP_SIGNATURE_LIST").length > 1 &&
        (header.splitOn "CFLOW_BUILTIN_REDUCE_SIGNATURE_LIST").length > 1

example : builtinOperatorPolicy.validate builtinSignatureManifest = .ok () := by
  rfl

example : builtinOperatorPolicy.WellFormed builtinSignatureManifest :=
  builtinOperatorPolicy_wellFormed

example : builtinOperatorPolicy.map.length = 7 := by decide

example : builtinOperatorPolicy.allUnary.length = 9 := by decide

example : unregisteredPolicy.validate builtinSignatureManifest =
    .error .unregisteredSignature := by rfl

example : uncoveredPolicy.validate builtinSignatureManifest =
    .error .registryNotCovered := by rfl

example : ({ builtinOperatorPolicy with map := [] }).validate
    builtinSignatureManifest = .error .emptyOperator := by rfl

example : duplicateMapPolicy.validate builtinSignatureManifest =
    .error .duplicateOperatorSignature := by rfl

example : builtinOperatorPolicy.validate
    ({ builtinSignatureManifest with binary := [] }) =
      .error .invalidRegistry := by rfl

example : invalidFilterShapePolicy.validate builtinSignatureManifest =
    .error .invalidOperatorShape := by rfl

example : invalidReduceShapePolicy.validate builtinSignatureManifest =
    .error .invalidOperatorShape := by rfl

example : renderedPolicyHasRequiredMacros = true := by native_decide

end CMetaCFlowCalculus.Tests.OperatorPolicyManifest
