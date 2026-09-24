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

/-- Stable public ordering for the built-in cmeta_sig enum. Relation lists
    describe membership; this list independently describes ABI position. -/
inductive SignatureRef where
  | unary (relation : UnaryRelation)
  | binary (relation : BinaryRelation)
  | generator (relation : GeneratorRelation)
  deriving Repr, DecidableEq, BEq

/-- Finite, enumerable facts from which CMeta signature families are emitted. -/
structure SignatureManifest where
  types : List CTypeRow
  unary : List UnaryRelation
  binary : List BinaryRelation
  generators : List GeneratorRelation
  abiOrder : List SignatureRef
  deriving Repr, DecidableEq, BEq

inductive ManifestError where
  | emptyTypes
  | emptyUnary
  | emptyBinary
  | emptyGenerators
  | emptyAbiOrder
  | duplicateType
  | duplicateUnary
  | duplicateBinary
  | duplicateGenerator
  | duplicateAbiSignature
  | unknownType
  | abiSignatureMismatch
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

private def SignatureManifest.signatureSet
    (manifest : SignatureManifest) : List SignatureRef :=
  manifest.unary.map SignatureRef.unary ++
    manifest.binary.map SignatureRef.binary ++
    manifest.generators.map SignatureRef.generator

private def SignatureManifest.abiOrderMatchesRelations
    (manifest : SignatureManifest) : Bool :=
  let expected := manifest.signatureSet
  manifest.abiOrder.length == expected.length &&
    manifest.abiOrder.all (fun signature => expected.contains signature) &&
    expected.all (fun signature => manifest.abiOrder.contains signature)

def SignatureManifest.validate
    (manifest : SignatureManifest) : Except ManifestError Unit :=
  let tokens := manifest.types.map (·.token)
  if manifest.types.isEmpty then .error .emptyTypes
  else if manifest.unary.isEmpty then .error .emptyUnary
  else if manifest.binary.isEmpty then .error .emptyBinary
  else if manifest.generators.isEmpty then .error .emptyGenerators
  else if manifest.abiOrder.isEmpty then .error .emptyAbiOrder
  else if !unique tokens then .error .duplicateType
  else if !unique manifest.unary then .error .duplicateUnary
  else if !unique manifest.binary then .error .duplicateBinary
  else if !unique manifest.generators then .error .duplicateGenerator
  else if !unique manifest.abiOrder then .error .duplicateAbiSignature
  else if !unaryTypesKnown tokens manifest.unary then .error .unknownType
  else if !binaryTypesKnown tokens manifest.binary then .error .unknownType
  else if !generatorTypesKnown tokens manifest.generators then .error .unknownType
  else if !manifest.abiOrderMatchesRelations then .error .abiSignatureMismatch
  else .ok ()

/-- A proof-facing predicate backed by the executable validator. -/
def SignatureManifest.WellFormed (manifest : SignatureManifest) : Prop :=
  manifest.validate = .ok ()

end CMetaCFlowCalculus.CMeta
