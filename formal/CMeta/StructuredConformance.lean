import CMeta.RuntimeConformance
import CMeta.StructuredGeneratedC

/-!
# Structured relation runtime conformance

This module extends the executable formal model beyond the direct-plan subset
with the concrete structured semantics exercised by the C runtime witnesses:

* homogeneous 1:1 `ALL + FOLD` relations;
* heterogeneous surface ZIP lowered to `ALL + INVOKE`.

The models preserve declaration order and mirror the value materialization used
by `relation_exec.c` while keeping direct-plan rejection as an explicit
capability boundary.
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

/-- Executable value semantics of the two heterogeneous branches created by ZIP
    lowering and combined by RELATION(INVOKE). -/
structure ZipInvokeExec (A L R O : CType) where
  left : Callable1 A L
  right : Callable1 A R
  combine : Callable2 L R O

namespace ZipInvokeExec

/-- Execute one source value through both branches and invoke the binary
    combiner with branch outputs in left/right order. -/
def runOne {A L R O : CType} (zip : ZipInvokeExec A L R O)
    (x : A.denote) : O.denote :=
  zip.combine.run (zip.left.run x) (zip.right.run x)

/-- The 1:1 ZIP witness produces one combined result per source value. -/
def run {A L R O : CType} (zip : ZipInvokeExec A L R O)
    (xs : ValueVec A) : ValueVec O :=
  xs.map zip.runOne

/-- ZIP with 1:1 branches preserves source cardinality. -/
theorem run_length {A L R O : CType} (zip : ZipInvokeExec A L R O)
    (xs : ValueVec A) :
    (zip.run xs).length = xs.length := by
  simp [run]

end ZipInvokeExec

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

private def zipRight : Callable1 CType.int CType.double :=
  ⟨fun (_ : Int) => (2.0 : Float)⟩

private def zipCombine : Callable2 CType.long CType.double CType.double :=
  ⟨fun (_ : Int) (right : Float) => right⟩

private def zipExec : ZipInvokeExec CType.int CType.long CType.double CType.double :=
  ⟨relationLeft, zipRight, zipCombine⟩

private def zipLeftPipeline : Pipeline CType.int CType.long :=
  .cons (.map CType.int CType.long) (.done CType.long)

private def zipRightPipeline : Pipeline CType.int CType.double :=
  .cons (.map CType.int CType.double) (.done CType.double)

private def zipSurface : SurfaceZip CType.int CType.double :=
  { leftOutput := CType.long,
    rightOutput := CType.double,
    left := zipLeftPipeline,
    right := zipRightPipeline,
    combine := zipCombine }

/-- The actual ZIP witness has exactly the heterogeneous branch equation already
    proved sound for surface ZIP lowering to RELATION(INVOKE). -/
theorem StructuredZipConformance.lowering_type_valid :
    checkInvokeRelation CType.int zipSurface.lower = some CType.double := by
  exact zipSurface.lowering_preserves_type

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

private def zipWitnessConforms
    (w : CStructuredGenerated.ZipWitness) : Bool :=
  w.name == "zip_all_invoke_i_l_d_d" &&
  w.inputType == "I" &&
  w.outputType == "D" &&
  w.coordination == "ALL" &&
  w.result == "INVOKE" &&
  w.branchCount == 2 &&
  w.combine == "B_L_D_D" &&
  w.count == (zipExec.run w.input).length &&
  w.output == zipExec.run w.input

/-- The normalized C relation descriptor has the exact type/coordination/result
    metadata modeled by the formal ALL/FOLD relation. -/
theorem StructuredRelationConformance.generated_descriptor_matches :
    CStructuredGenerated.relationWitnesses.all relationWitnessConforms = true := by
  native_decide

/-- The real structured FOLD runtime returns exactly the values computed by the
    formal ordered branch/FOLD semantics. -/
theorem StructuredRelationConformance.runtime_matches_model :
    CStructuredGenerated.relationWitnesses.all
      (fun w => w.count == w.output.length &&
        w.output == relationExec.run w.input) = true := by
  native_decide

/-- The surface ZIP normalizes to the expected ALL/INVOKE descriptor and its
    real structured runtime values match the formal left/right/combine model. -/
theorem StructuredZipConformance.runtime_matches_lowered_model :
    CStructuredGenerated.zipWitnesses.all zipWitnessConforms = true := by
  native_decide

/-- Direct-plan capability remains intentionally narrower than the structured
    runtime: both ordinary relations and ZIP-lowered INVOKE relations are
    rejected by the real direct-plan compiler. -/
theorem StructuredRelationConformance.direct_plan_rejects_structured :
    CStructuredGenerated.relationWitnesses.all
      (fun w => !w.directPlanAccepted) = true ∧
    CStructuredGenerated.zipWitnesses.all
      (fun w => !w.directPlanAccepted) = true := by
  constructor <;> native_decide

/-- Public structured-backend gate combining typed lowering, descriptor shape,
    execution semantics and the direct-plan rejection boundary. -/
theorem CImplementationConformance.structured_runtime :
    CStructuredGenerated.relationWitnesses.all relationWitnessConforms = true ∧
    CStructuredGenerated.zipWitnesses.all zipWitnessConforms = true ∧
    (CStructuredGenerated.relationWitnesses.all
      (fun w => !w.directPlanAccepted) = true ∧
     CStructuredGenerated.zipWitnesses.all
      (fun w => !w.directPlanAccepted) = true) := by
  exact ⟨StructuredRelationConformance.generated_descriptor_matches,
    StructuredZipConformance.runtime_matches_lowered_model,
    StructuredRelationConformance.direct_plan_rejects_structured⟩

end CMeta
