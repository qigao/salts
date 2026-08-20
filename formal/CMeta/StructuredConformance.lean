import CMeta.RuntimeConformance
import CMeta.StructuredGeneratedC

/-!
# Structured relation runtime conformance

This module extends the executable formal model beyond the direct-plan subset
with the concrete structured semantics exercised by the C runtime witnesses:

* homogeneous 1:1 `ALL + FOLD` relations;
* synchronous `ANY + SELECT`;
* ordered `SEQUENCE + SELECT`;
* `ALL_DONE + FOLD`, which folds the last value produced by each completed
  branch;
* heterogeneous surface ZIP lowered to `ALL + INVOKE`.

The models preserve declaration order and mirror the value materialization used
by `coord.c` and `relation_exec.c` while keeping direct-plan rejection as an
explicit capability boundary.
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

/-- Synchronous ANY/SELECT witness semantics. The real coordinator starts at
    branch zero; with immediately-producing branches the first declared branch
    wins and the remaining branches are cancelled. -/
structure AnySelectExec (A R : CType) where
  first : Callable1 A R
  rest : List (Callable1 A R)

namespace AnySelectExec

def run {A R : CType} (rel : AnySelectExec A R)
    (xs : ValueVec A) : ValueVec R :=
  xs.map rel.first.run

theorem run_length {A R : CType} (rel : AnySelectExec A R)
    (xs : ValueVec A) :
    (rel.run xs).length = xs.length := by
  simp [run]

end AnySelectExec

/-- Ordered SEQUENCE/SELECT witness semantics. Each source value is evaluated
    by every branch in declaration order and each selected branch value is
    emitted before advancing to the next branch. -/
structure SequenceSelectExec (A R : CType) where
  first : Callable1 A R
  rest : List (Callable1 A R)

namespace SequenceSelectExec

def branches {A R : CType} (rel : SequenceSelectExec A R) :
    List (Callable1 A R) :=
  rel.first :: rel.rest

def runOne {A R : CType} (rel : SequenceSelectExec A R)
    (x : A.denote) : List R.denote :=
  rel.branches.map (fun branch => branch.run x)

def run {A R : CType} (rel : SequenceSelectExec A R)
    (xs : ValueVec A) : ValueVec R :=
  xs.flatMap rel.runOne

end SequenceSelectExec

/-- One successfully completed branch trace with at least one value.  This is
    the operational contract required by ALL_DONE, which errors if a branch
    completes without ever producing a value. -/
structure NonemptyBranchTrace (A R : CType) where
  run : A.denote → R.denote × List R.denote

namespace NonemptyBranchTrace

/-- The value retained by the coordinator after the branch has fully completed. -/
def lastValue {A R : CType} (branch : NonemptyBranchTrace A R)
    (x : A.denote) : R.denote :=
  let trace := branch.run x
  trace.2.foldl (fun _ value => value) trace.1

end NonemptyBranchTrace

/-- ALL_DONE/FOLD semantics: fully drain each branch, retain each branch's last
    produced value, then fold those retained values in branch order. -/
structure AllDoneFoldExec (A R : CType) where
  first : NonemptyBranchTrace A R
  rest : List (NonemptyBranchTrace A R)
  reducer : Callable2 R R R

namespace AllDoneFoldExec

def runOne {A R : CType} (rel : AllDoneFoldExec A R)
    (x : A.denote) : R.denote :=
  (rel.rest.map (fun branch => branch.lastValue x)).foldl
    rel.reducer.run (rel.first.lastValue x)

def run {A R : CType} (rel : AllDoneFoldExec A R)
    (xs : ValueVec A) : ValueVec R :=
  xs.map rel.runOne

theorem run_length {A R : CType} (rel : AllDoneFoldExec A R)
    (xs : ValueVec A) :
    (rel.run xs).length = xs.length := by
  simp [run]

end AllDoneFoldExec

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

private def anySelectExec : AnySelectExec CType.int CType.long :=
  ⟨relationLeft, [relationRight]⟩

private def sequenceSelectExec : SequenceSelectExec CType.int CType.long :=
  ⟨relationLeft, [relationRight]⟩

private def allDoneLeft : NonemptyBranchTrace CType.int CType.long :=
  ⟨fun (x : Int) => (x, [x + 1])⟩

private def allDoneRight : NonemptyBranchTrace CType.int CType.long :=
  ⟨fun (x : Int) => (x * 10, [x * 10 + 100])⟩

private def allDoneFoldExec : AllDoneFoldExec CType.int CType.long :=
  ⟨allDoneLeft, [allDoneRight], relationAdd⟩

private def relationMapPipeline : Pipeline CType.int CType.long :=
  .cons (.map CType.int CType.long) (.done CType.long)

private def relationTyped : TypedRelation CType.int CType.long :=
  .fold
    { first := relationMapPipeline, rest := [relationMapPipeline] }
    relationAdd

private def selectTyped : TypedRelation CType.int CType.long :=
  .select { first := relationMapPipeline, rest := [relationMapPipeline] }

/-- The FOLD branch/reducer shape is admitted by the structured graph typing
    judgment already proved for `TypedRelation.fold`. -/
theorem StructuredRelationConformance.typed_relation_valid :
    checkRelation CType.int relationTyped.erase = some CType.long := by
  exact relationTyped.check_erase

/-- ANY and SEQUENCE change scheduling/result multiplicity, not the homogeneous
    SELECT type equation. -/
theorem StructuredRelationConformance.typed_select_valid :
    checkRelation CType.int selectTyped.erase = some CType.long := by
  exact selectTyped.check_erase

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

private structure CoordinationModelResult where
  inputType : String
  outputType : String
  coordination : String
  completion : String
  result : String
  error : String
  branchCount : Nat
  reducer : String
  output : List Int

private def coordinationModel (name : String) (input : List Int) :
    Option CoordinationModelResult :=
  match name with
  | "relation_any_select_i_l" =>
      some ⟨"I", "L", "ANY", "COORDINATOR", "SELECT", "FAIL_FAST",
        2, "", anySelectExec.run input⟩
  | "relation_sequence_select_i_l" =>
      some ⟨"I", "L", "SEQUENCE", "COORDINATOR", "SELECT", "FAIL_FAST",
        2, "", sequenceSelectExec.run input⟩
  | "relation_all_done_fold_i_l" =>
      some ⟨"I", "L", "ALL", "ALL_DONE", "FOLD", "FAIL_FAST",
        2, "B_L_L_L", allDoneFoldExec.run input⟩
  | _ => none

private def coordinationWitnessConforms
    (w : CStructuredGenerated.CoordinationWitness) : Bool :=
  match coordinationModel w.name w.input with
  | some expected =>
      w.inputType == expected.inputType &&
      w.outputType == expected.outputType &&
      w.coordination == expected.coordination &&
      w.completion == expected.completion &&
      w.result == expected.result &&
      w.error == expected.error &&
      w.branchCount == expected.branchCount &&
      w.reducer == expected.reducer &&
      w.count == expected.output.length &&
      w.output == expected.output &&
      !w.directPlanAccepted
  | none => false

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

/-- The coordination witness suite covers the scheduler policies whose value
    semantics are modeled in this module. -/
theorem StructuredCoordinationConformance.coverage :
    CStructuredGenerated.coordinationWitnesses.map (fun w => w.name) =
      ["relation_any_select_i_l", "relation_sequence_select_i_l",
       "relation_all_done_fold_i_l"] := by
  native_decide

/-- Real coord.c + relation_exec.c observations agree with the explicit ANY,
    SEQUENCE and ALL_DONE value models, including completion/error metadata. -/
theorem StructuredCoordinationConformance.runtime_matches_model :
    CStructuredGenerated.coordinationWitnesses.all coordinationWitnessConforms = true := by
  native_decide

/-- The surface ZIP normalizes to the expected ALL/INVOKE descriptor and its
    real structured runtime values match the formal left/right/combine model. -/
theorem StructuredZipConformance.runtime_matches_lowered_model :
    CStructuredGenerated.zipWitnesses.all zipWitnessConforms = true := by
  native_decide

/-- Direct-plan capability remains intentionally narrower than the structured
    runtime: ordinary relations, coordination variants and ZIP-lowered INVOKE
    relations are all rejected by the real direct-plan compiler. -/
theorem StructuredRelationConformance.direct_plan_rejects_structured :
    CStructuredGenerated.relationWitnesses.all
      (fun w => !w.directPlanAccepted) = true ∧
    CStructuredGenerated.coordinationWitnesses.all
      (fun w => !w.directPlanAccepted) = true ∧
    CStructuredGenerated.zipWitnesses.all
      (fun w => !w.directPlanAccepted) = true := by
  constructor
  · native_decide
  constructor <;> native_decide

/-- Public structured-backend gate combining typed relations/lowering,
    coordination semantics, execution semantics and direct-plan rejection. -/
theorem CImplementationConformance.structured_runtime :
    CStructuredGenerated.relationWitnesses.all relationWitnessConforms = true ∧
    CStructuredGenerated.coordinationWitnesses.all coordinationWitnessConforms = true ∧
    CStructuredGenerated.zipWitnesses.all zipWitnessConforms = true ∧
    (CStructuredGenerated.relationWitnesses.all
      (fun w => !w.directPlanAccepted) = true ∧
     CStructuredGenerated.coordinationWitnesses.all
      (fun w => !w.directPlanAccepted) = true ∧
     CStructuredGenerated.zipWitnesses.all
      (fun w => !w.directPlanAccepted) = true) := by
  exact ⟨StructuredRelationConformance.generated_descriptor_matches,
    StructuredCoordinationConformance.runtime_matches_model,
    StructuredZipConformance.runtime_matches_lowered_model,
    StructuredRelationConformance.direct_plan_rejects_structured⟩

end CMeta
