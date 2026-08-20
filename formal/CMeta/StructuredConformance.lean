import CMeta.RuntimeConformance
import CMeta.StructuredGeneratedC

/-!
# Structured relation runtime conformance

This module extends the executable formal model beyond the direct-plan subset
with the concrete 1:1 `ALL + FOLD` relation semantics exercised by the C
runtime witness.  Each source value is sent to every branch, branch results are
materialized in branch order, and the homogeneous reducer folds them from left
to right, matching `relation_exec.c::fold_values`.
-/

namespace CMeta

/-- Executable value semantics for a non-empty homogeneous set of 1:1 relation
    branches. -/
structure FoldRelationExec (A R : CType) where
  first : Callable1 A R
  rest : List (Callable1 A R)
  reducer : Callable2 R R R

namespace FoldRelationExec

/-- Execute one relation input: first branch seeds the accumulator and the
    remaining branch values are folded in declaration order. -/
def runOne {A R : CType} (rel : FoldRelationExec A R)
    (x : A.denote) : R.denote :=
  (rel.rest.map (fun branch => branch.run x)).foldl
    rel.reducer.run (rel.first.run x)

/-- A 1:1 relation produces one result for every source value. -/
def run {A R : CType} (rel : FoldRelationExec A R)
    (xs : ValueVec A) : ValueVec R :=
  xs.map rel.runOne

/-- The concrete 1:1 ALL/FOLD execution preserves collection cardinality. -/
theorem run_length {A R : CType} (rel : FoldRelationExec A R)
    (xs : ValueVec A) :
    (rel.run xs).length = xs.length := by
  simp [run]

end FoldRelationExec

private def relationLeft : Callable1 CType.int CType.long :=
  ⟨fun (x : Int) => x⟩

private def relationRight : Callable1 CType.int CType.long :=
  ⟨fun (x : Int) => x * 10⟩

private def relationAdd : Callable2 CType.long CType.long CType.long :=
  ⟨fun (a b : Int) => a + b⟩

private def relationExec : FoldRelationExec CType.int CType.long :=
  ⟨relationLeft, [relationRight], relationAdd⟩

private def relationMapPipeline : Pipeline CType.int CType.long :=
  .cons (.map CType.int CType.long) (.done CType.long)

private def relationTyped : TypedRelation CType.int CType.long :=
  .fold
    { first := relationMapPipeline, rest := [relationMapPipeline] }
    relationAdd

/-- The same branch/reducer shape is admitted by the structured graph typing
    judgment already proved for `TypedRelation.fold`. -/
theorem StructuredRelationConformance.typed_relation_valid :
    checkRelation CType.int relationTyped.erase = some CType.long := by
  exact relationTyped.check_erase

private def relationWitnessConforms
    (w : CStructuredGenerated.RelationWitness) : Bool :=
  w.name == "relation_all_fold_i_l" &&
  w.inputType == "I" &&
  w.outputType == "L" &&
  w.coordination == "ALL" &&
  w.result == "FOLD" &&
  w.branchCount == 2 &&
  w.reducer == "B_L_L_L" &&
  w.count == (relationExec.run w.input).length &&
  w.output == relationExec.run w.input

/-- The normalized C relation descriptor has the exact type/coordination/result
    metadata modeled by the formal ALL/FOLD relation. -/
theorem StructuredRelationConformance.generated_descriptor_matches :
    CStructuredGenerated.relationWitnesses.all relationWitnessConforms = true := by
  native_decide

/-- Direct-plan capability remains intentionally narrower than the structured
    runtime: the real plan compiler rejects every generated relation witness. -/
theorem StructuredRelationConformance.direct_plan_rejects_relation :
    CStructuredGenerated.relationWitnesses.all
      (fun w => !w.directPlanAccepted) = true := by
  native_decide

/-- The real structured runtime returns exactly the values computed by the
    formal ordered branch/FOLD semantics. -/
theorem StructuredRelationConformance.runtime_matches_model :
    CStructuredGenerated.relationWitnesses.all
      (fun w => w.count == w.output.length &&
        w.output == relationExec.run w.input) = true := by
  native_decide

/-- Public structured-backend gate combining descriptor shape, execution
    semantics and the direct-plan rejection boundary. -/
theorem CImplementationConformance.structured_relation_runtime :
    CStructuredGenerated.relationWitnesses.all relationWitnessConforms = true ∧
    CStructuredGenerated.relationWitnesses.all
      (fun w => !w.directPlanAccepted) = true := by
  exact ⟨StructuredRelationConformance.generated_descriptor_matches,
    StructuredRelationConformance.direct_plan_rejects_relation⟩

end CMeta
