module
public import CMeta.Flow
import all CMeta.Flow
public import CMeta.Callable
import all CMeta.Callable

/-!
# Typed graph and relation safety

This layer lifts the linear `Pipeline` proof to the structured graph shape used
by CFlow. Relation branches are snapshot subgraphs that all receive the same
input type. SELECT requires homogeneous branch output; FOLD additionally
requires a homogeneous reducer `T(T,T) -> T`.
-/

namespace CMeta

/-- Result semantics relevant to relation typing. Coordination/completion/error
    policies affect scheduling, not the type equation proved here. -/
public inductive RelationResult where
  | select
  | fold
  deriving Repr, DecidableEq

/-- A non-empty homogeneous branch set. Every branch starts at `A` and ends at
    `R`, so branch input/output agreement is enforced by construction. -/
public structure TypedBranches (A R : CType) where
  first : Pipeline A R
  rest : List (Pipeline A R)

namespace TypedBranches

/-- Erase branch indices to graph-level operator/signature streams. -/
public def erase {A R : CType} (b : TypedBranches A R) :
    List (List (Operator × Signature)) :=
  b.first.steps :: b.rest.map Pipeline.steps

end TypedBranches

/-- Check the remaining branches against an output type established by the
    first branch. -/
def checkBranchTail (input expected : CType) :
    List (List (Operator × Signature)) → Option CType
  | [] => some expected
  | branch :: rest =>
      match checkPipeline input branch with
      | some output =>
          if output = expected then checkBranchTail input expected rest else none
      | none => none

/-- A dynamic relation branch checker: the branch list must be non-empty and
    every erased branch must validate from the same input to the same output. -/
public def checkBranches (input : CType) :
    List (List (Operator × Signature)) → Option CType
  | [] => none
  | first :: rest =>
      match checkPipeline input first with
      | some output => checkBranchTail input output rest
      | none => none

private theorem checkBranchTail_typed {A R : CType}
    (branches : List (Pipeline A R)) :
    checkBranchTail A R (branches.map Pipeline.steps) = some R := by
  induction branches with
  | nil => rfl
  | cons branch rest ih =>
      simp [checkBranchTail, Pipeline.check_steps, ih]

/-- Erasing homogeneous typed branches and checking them dynamically recovers
    their shared output type. -/
theorem TypedBranches.check_erase {A R : CType} (b : TypedBranches A R) :
    checkBranches A b.erase = some R := by
  simp only [TypedBranches.erase, checkBranches]
  rw [Pipeline.check_steps b.first]
  exact checkBranchTail_typed b.rest

/-- Dynamic relation descriptor, corresponding to the type-relevant part of
    `cflow_relation_schema` plus its optional reducer signature. -/
public structure ErasedRelation where
  branches : List (List (Operator × Signature))
  result : RelationResult
  reducer : Option Signature
  deriving Repr, DecidableEq

/-- Statically typed relation. SELECT has homogeneous branches; FOLD has the
    same branch condition plus a homogeneous `R × R -> R` reducer. -/
public inductive TypedRelation (A R : CType) where
  | select (branches : TypedBranches A R) : TypedRelation A R
  | fold (branches : TypedBranches A R)
      (reducer : Callable [R, R] R) : TypedRelation A R

namespace TypedRelation

/-- Erase the dependent relation indices to the runtime descriptor shape. -/
public def erase : {A R : CType} → TypedRelation A R → ErasedRelation
  | _, _, .select branches =>
      ⟨branches.erase, .select, none⟩
  | _, _, .fold branches reducer =>
      ⟨branches.erase, .fold, some reducer.binaryBackendSignature⟩

end TypedRelation

/-- Dynamic relation admission. SELECT returns the homogeneous branch output;
    FOLD additionally validates the exact homogeneous reducer signature. -/
public def checkRelation (input : CType) (rel : ErasedRelation) : Option CType :=
  match checkBranches input rel.branches with
  | none => none
  | some output =>
      match rel.result with
      | .select => some output
      | .fold =>
          if rel.reducer = some (.binary output output output)
          then some output
          else none

/-- Relation preservation after erasure. -/
theorem TypedRelation.check_erase {A R : CType} (rel : TypedRelation A R) :
    checkRelation A rel.erase = some R := by
  cases rel with
  | select branches =>
      simp [TypedRelation.erase, checkRelation, TypedBranches.check_erase]
  | fold branches reducer =>
      simp [TypedRelation.erase, checkRelation, TypedBranches.check_erase,
        Callable.binaryBackendSignature]

/-- Relation progress: a well-typed relation cannot get stuck in type
    admission after erasure. -/
theorem TypedRelation.progress {A R : CType} (rel : TypedRelation A R) :
    ∃ output, checkRelation A rel.erase = some output :=
  ⟨R, rel.check_erase⟩

/-- Relation output is unique for the erased descriptor produced from one
    typed relation. -/
theorem TypedRelation.output_unique {A R R' : CType}
    (rel : TypedRelation A R)
    (h : checkRelation A rel.erase = some R') : R = R' := by
  have hs : (some R : Option CType) = some R' := rel.check_erase.symm.trans h
  exact Option.some.inj hs

/-- Type-relevant stages of the erased structured graph. -/
public inductive ErasedStage where
  | op (operator : Operator) (signature : Signature)
  | relation (descriptor : ErasedRelation)
  deriving Repr, DecidableEq

/-- Structured graph typing judgment `A => R`. Operators and relations can be
    composed arbitrarily; each constructor forces the next stage to consume the
    previous stage's output. -/
public inductive TypedGraph : CType → CType → Type where
  | done (t : CType) : TypedGraph t t
  | op {A B R : CType} :
      TypedOp A B → TypedGraph B R → TypedGraph A R
  | relation {A B R : CType} :
      TypedRelation A B → TypedGraph B R → TypedGraph A R

namespace TypedGraph

/-- Erase graph type indices to runtime-checkable stages. -/
public def stages : {A R : CType} → TypedGraph A R → List ErasedStage
  | _, _, .done _ => []
  | _, _, .op node rest =>
      .op node.operator node.signature :: rest.stages
  | _, _, .relation rel rest =>
      .relation rel.erase :: rest.stages

end TypedGraph

/-- Dynamic checker for the erased structured graph. -/
public def checkGraph : CType → List ErasedStage → Option CType
  | current, [] => some current
  | current, .op operator signature :: rest =>
      match stepType operator current signature with
      | some next => checkGraph next rest
      | none => none
  | current, .relation descriptor :: rest =>
      match checkRelation current descriptor with
      | some next => checkGraph next rest
      | none => none

/-- Main graph preservation theorem for this structured model: after all
    dependent type indices are erased, ordinary dynamic validation recovers the
    exact statically known result type. -/
theorem TypedGraph.check_stages {A R : CType} (graph : TypedGraph A R) :
    checkGraph A graph.stages = some R := by
  induction graph with
  | done t => rfl
  | op node rest ih =>
      simp [TypedGraph.stages, checkGraph, TypedOp.step_exact, ih]
  | relation rel rest ih =>
      simp [TypedGraph.stages, checkGraph, TypedRelation.check_erase, ih]

/-- Graph progress follows immediately from whole-graph preservation. -/
theorem TypedGraph.progress {A R : CType} (graph : TypedGraph A R) :
    ∃ output, checkGraph A graph.stages = some output :=
  ⟨R, graph.check_stages⟩

/-- Whole-graph output uniqueness after erasure. -/
theorem TypedGraph.output_unique {A R R' : CType} (graph : TypedGraph A R)
    (h : checkGraph A graph.stages = some R') : R = R' := by
  have hs : (some R : Option CType) = some R' := graph.check_stages.symm.trans h
  exact Option.some.inj hs

end CMeta
