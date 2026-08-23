import Std

namespace CMetaCFlowCalculus.CMeta

/-- One CMeta type row consumed by descriptor and callable generation. -/
structure CTypeRow where
  token : String
  cType : String
  descriptor : String
  kind : String
  traits : String
  deriving Repr, DecidableEq, BEq

structure UnaryRelation where
  input : String
  output : String
  deriving Repr, DecidableEq, BEq

structure BinaryRelation where
  left : String
  right : String
  output : String
  deriving Repr, DecidableEq, BEq

structure GeneratorRelation where
  input : String
  output : String
  deriving Repr, DecidableEq, BEq

/-- Finite, enumerable facts from which CMeta signature families are emitted. -/
structure SignatureManifest where
  types : List CTypeRow
  unary : List UnaryRelation
  binary : List BinaryRelation
  generators : List GeneratorRelation
  deriving Repr, DecidableEq, BEq

inductive ManifestError where
  | emptyTypes
  | emptyUnary
  | emptyBinary
  | emptyGenerators
  | duplicateType
  | duplicateUnary
  | duplicateBinary
  | duplicateGenerator
  | unknownType
  deriving Repr, DecidableEq, BEq

/-- O(n²) duplicate detection for a small, build-time-only manifest. Keeping the
    scan structural avoids a second ordering or hashing fact source. -/
private def unique [BEq α] : List α → Bool
  | [] => true
  | value :: rest => !rest.contains value && unique rest

private def unaryTypesKnown (tokens : List String) : List UnaryRelation → Bool
  | [] => true
  | relation :: rest =>
      tokens.contains relation.input && tokens.contains relation.output &&
        unaryTypesKnown tokens rest

private def binaryTypesKnown (tokens : List String) : List BinaryRelation → Bool
  | [] => true
  | relation :: rest =>
      tokens.contains relation.left && tokens.contains relation.right &&
        tokens.contains relation.output && binaryTypesKnown tokens rest

private def generatorTypesKnown
    (tokens : List String) : List GeneratorRelation → Bool
  | [] => true
  | relation :: rest =>
      tokens.contains relation.input && tokens.contains relation.output &&
        generatorTypesKnown tokens rest

def SignatureManifest.validate
    (manifest : SignatureManifest) : Except ManifestError Unit :=
  let tokens := manifest.types.map (·.token)
  if manifest.types.isEmpty then .error .emptyTypes
  else if manifest.unary.isEmpty then .error .emptyUnary
  else if manifest.binary.isEmpty then .error .emptyBinary
  else if manifest.generators.isEmpty then .error .emptyGenerators
  else if !unique tokens then .error .duplicateType
  else if !unique manifest.unary then .error .duplicateUnary
  else if !unique manifest.binary then .error .duplicateBinary
  else if !unique manifest.generators then .error .duplicateGenerator
  else if !unaryTypesKnown tokens manifest.unary then .error .unknownType
  else if !binaryTypesKnown tokens manifest.binary then .error .unknownType
  else if !generatorTypesKnown tokens manifest.generators then .error .unknownType
  else .ok ()

/-- A proof-facing predicate backed by the executable validator. -/
def SignatureManifest.WellFormed (manifest : SignatureManifest) : Prop :=
  manifest.validate = .ok ()

end CMetaCFlowCalculus.CMeta
