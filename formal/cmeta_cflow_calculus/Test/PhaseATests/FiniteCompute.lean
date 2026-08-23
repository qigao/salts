import CMetaCFlowCalculus.CMeta.FiniteCompute

open CMetaCFlowCalculus.CMeta

namespace CMetaCFlowCalculus.Tests.FiniteCompute

inductive ComputeType where
  | small
  | wide
  | boolean
  deriving Repr, DecidableEq, BEq, ReflBEq, LawfulBEq

inductive Operation where
  | add
  | compare
  deriving Repr, DecidableEq, BEq, ReflBEq, LawfulBEq

def numericTypes : List ComputeType := [.small, .wide]

def commonInputs : List (ComputeType × ComputeType) := [
  (.small, .small),
  (.small, .wide),
  (.wide, .small),
  (.wide, .wide)
]

def commonRows : FiniteRelation (ComputeType × ComputeType) ComputeType := [
  ((.small, .small), .small),
  ((.small, .wide), .wide),
  ((.wide, .small), .wide),
  ((.wide, .wide), .wide)
]

def operationRows :
    FiniteRelation (Operation × ComputeType × ComputeType) ComputeType := [
  ((.add, .small, .small), .small),
  ((.add, .small, .wide), .wide),
  ((.compare, .wide, .wide), .boolean)
]

def ambiguousRows : FiniteRelation ComputeType ComputeType := [
  (.small, .small),
  (.small, .wide)
]

example : Functional commonRows = true := by decide
example : TotalOn commonInputs commonRows = true := by decide
example : ClosedOver numericTypes commonRows = true := by decide
example : Functional operationRows = true := by decide
example : Functional ambiguousRows = false := by decide

example : lookup (.small, .wide) commonRows = some .wide := by decide
example : lookup (.wide, .small) commonRows = some .wide := by decide
example : lookup (.boolean, .small) commonRows = none := by decide

example :
    WellFormed commonInputs numericTypes commonRows := by
  constructor
  · decide
  constructor <;> decide

end CMetaCFlowCalculus.Tests.FiniteCompute
