import CMetaCFlowCalculus.CMeta.SignatureManifest
import CMetaCFlowCalculus.CMeta.BuiltinSignatures
import CMetaCFlowCalculus.CMeta.SignatureHeader

open CMetaCFlowCalculus.CMeta

namespace CMetaCFlowCalculus.Tests.SignatureManifest

def duplicateBinaryManifest : SignatureManifest :=
  { builtinSignatureManifest with
    binary := builtinSignatureManifest.binary ++
      builtinSignatureManifest.binary.take 1 }

def unknownBinaryTypeManifest : SignatureManifest :=
  { builtinSignatureManifest with
    binary := [{ left := "I", right := "Z", output := "I" }] }

def duplicateTypeManifest : SignatureManifest :=
  { builtinSignatureManifest with
    types := builtinSignatureManifest.types ++
      builtinSignatureManifest.types.take 1 }

def duplicateUnaryManifest : SignatureManifest :=
  { builtinSignatureManifest with
    unary := builtinSignatureManifest.unary ++
      builtinSignatureManifest.unary.take 1 }

def duplicateGeneratorManifest : SignatureManifest :=
  { builtinSignatureManifest with
    generators := builtinSignatureManifest.generators ++
      builtinSignatureManifest.generators.take 1 }

def renderedHeaderHasRequiredMacros : Bool :=
  match SignatureHeader.render builtinSignatureManifest with
  | .error _ => false
  | .ok header =>
      (header.splitOn "CMETA_BUILTIN_TYPE_LIST").length > 1 &&
        (header.splitOn "CMETA_BUILTIN_BINARY_RELATION_LIST").length > 1

example : builtinSignatureManifest.validate = .ok () := by rfl

example : builtinSignatureManifest.WellFormed :=
  builtinSignatureManifest_wellFormed

example : builtinSignatureManifest.binary.length = 2 := by decide

example : builtinSignatureManifest.binary.length < 5 ^ 3 :=
  builtinBinaryRelations_finite

example : duplicateBinaryManifest.validate = .error .duplicateBinary := by
  rfl

example : unknownBinaryTypeManifest.validate = .error .unknownType := by
  rfl

example : duplicateTypeManifest.validate = .error .duplicateType := by rfl

example : duplicateUnaryManifest.validate = .error .duplicateUnary := by rfl

example : duplicateGeneratorManifest.validate = .error .duplicateGenerator := by
  rfl

example : ({ builtinSignatureManifest with types := [] }).validate =
    .error .emptyTypes := by rfl

example : ({ builtinSignatureManifest with unary := [] }).validate =
    .error .emptyUnary := by rfl

example : ({ builtinSignatureManifest with binary := [] }).validate =
    .error .emptyBinary := by rfl

example : ({ builtinSignatureManifest with generators := [] }).validate =
    .error .emptyGenerators := by rfl

example : renderedHeaderHasRequiredMacros = true := by native_decide

end CMetaCFlowCalculus.Tests.SignatureManifest
