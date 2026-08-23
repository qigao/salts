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

inductive InferSymbol where
  | operation (value : Operation)
  | type (value : ComputeType)
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

def inferenceDfa : FiniteDfa InferSymbol ComputeType where
  start := 0
  transitions := [
    ((0, .operation .add), 1),
    ((1, .type .small), 2),
    ((2, .type .wide), 3),
    ((0, .operation .compare), 4),
    ((4, .type .wide), 5),
    ((5, .type .wide), 6)
  ]
  accepts := [(3, .wide), (6, .boolean)]

example : DfaWellFormed inferenceDfa := by
  constructor <;> decide

example :
    dfaRun inferenceDfa [
      .operation .add, .type .small, .type .wide
    ] = some .wide := by
  decide

example :
    dfaRun inferenceDfa [
      .operation .add, .type .wide, .type .small
    ] = none := by
  decide

example :
    dfaStep inferenceDfa 0 (.operation .compare) = some 4 := by
  decide

example :
    WellFormed commonInputs numericTypes commonRows := by
  constructor
  · decide
  constructor <;> decide

end CMetaCFlowCalculus.Tests.FiniteCompute
