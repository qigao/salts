import CMetaCFlowCalculus.CMeta.SignatureManifest

namespace CMetaCFlowCalculus.CFlow

open CMeta

/-- CFlow's built-in operator membership, separated by callable protocol. -/
structure OperatorPolicyManifest where
  filter : List UnaryRelation
  map : List UnaryRelation
  transform : List UnaryRelation
  flatMap : List GeneratorRelation
  reduce : List BinaryRelation
  zip : List BinaryRelation
  deriving Repr, DecidableEq, BEq

inductive OperatorPolicyError where
  | invalidRegistry
  | emptyOperator
  | duplicateOperatorSignature
  | unregisteredSignature
  | invalidOperatorShape
  | registryNotCovered
  deriving Repr, DecidableEq, BEq

def OperatorPolicyManifest.allUnary
    (policy : OperatorPolicyManifest) : List UnaryRelation :=
  policy.filter ++ policy.map ++ policy.transform

def OperatorPolicyManifest.allBinary
    (policy : OperatorPolicyManifest) : List BinaryRelation :=
  policy.reduce ++ policy.zip

def OperatorPolicyManifest.allGenerators
    (policy : OperatorPolicyManifest) : List GeneratorRelation :=
  policy.flatMap

/-- O(n²) duplicate detection over fixed build-time lists whose built-in
    cardinalities are at most seven. Structural order remains deterministic. -/
private def unique [BEq α] : List α → Bool
  | [] => true
  | value :: rest => !rest.contains value && unique rest

private def registered [BEq α] (registry requested : List α) : Bool :=
  requested.all fun signature => registry.contains signature

private def covered [BEq α] (registry requested : List α) : Bool :=
  registry.all fun signature => requested.contains signature

private def filterShapesValid (policy : OperatorPolicyManifest) : Bool :=
  policy.filter.all fun signature => signature.output == "B"

private def reduceShapesValid (policy : OperatorPolicyManifest) : Bool :=
  policy.reduce.all fun signature =>
    signature.left == signature.right && signature.left == signature.output

def OperatorPolicyManifest.validate
    (policy : OperatorPolicyManifest)
    (registry : SignatureManifest) : Except OperatorPolicyError Unit :=
  match registry.validate with
  | .error _ => .error .invalidRegistry
  | .ok () =>
      if policy.filter.isEmpty || policy.map.isEmpty ||
          policy.transform.isEmpty || policy.flatMap.isEmpty ||
          policy.reduce.isEmpty || policy.zip.isEmpty then
        .error .emptyOperator
      else if !unique policy.filter || !unique policy.map ||
          !unique policy.transform || !unique policy.flatMap ||
          !unique policy.reduce || !unique policy.zip then
        .error .duplicateOperatorSignature
      else if !registered registry.unary policy.allUnary ||
          !registered registry.binary policy.allBinary ||
          !registered registry.generators policy.allGenerators then
        .error .unregisteredSignature
      else if !filterShapesValid policy || !reduceShapesValid policy then
        .error .invalidOperatorShape
      else if !covered registry.unary policy.allUnary ||
          !covered registry.binary policy.allBinary ||
          !covered registry.generators policy.allGenerators then
        .error .registryNotCovered
      else
        .ok ()

def OperatorPolicyManifest.WellFormed
    (policy : OperatorPolicyManifest) (registry : SignatureManifest) : Prop :=
  policy.validate registry = .ok ()

end CMetaCFlowCalculus.CFlow
