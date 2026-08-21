module
import all CMeta.RuntimeConformance
import all CMeta.StructuredGeneratedC
import all CMeta.Graph
import all CMeta.Lowering

/-!
# Structured relation runtime conformance

This module extends the executable formal model beyond the direct-plan subset
with the concrete structured semantics exercised by the C runtime witnesses.
-/

namespace CMeta

/-- Executable value semantics for a non-empty homogeneous set of 1:1 relation branches. -/
structure FoldRelationExec (A R : CType) where
  first : Callable [A] R
  rest : List (Callable [A] R)
  reducer : Callable [R, R] R

namespace FoldRelationExec

def runOne {A R : CType} (rel : FoldRelationExec A R)
    (x : A.denote) : R.denote :=
  (rel.rest.map (fun branch => branch.invoke1 x)).foldl
    rel.reducer.invoke2 (rel.first.invoke1 x)

def run {A R : CType} (rel : FoldRelationExec A R)
    (xs : ValueVec A) : ValueVec R :=
  xs.map rel.runOne

theorem run_length {A R : CType} (rel : FoldRelationExec A R)
    (xs : ValueVec A) :
    (rel.run xs).length = xs.length := by
  simp [run]

end FoldRelationExec

structure AnySelectExec (A R : CType) where
  first : Callable [A] R
  rest : List (Callable [A] R)

namespace AnySelectExec

def run {A R : CType} (rel : AnySelectExec A R)
    (xs : ValueVec A) : ValueVec R :=
  xs.map rel.first.invoke1

theorem run_length {A R : CType} (rel : AnySelectExec A R)
    (xs : ValueVec A) :
    (rel.run xs).length = xs.length := by
  simp [run]

end AnySelectExec

structure SequenceSelectExec (A R : CType) where
  first : Callable [A] R
  rest : List (Callable [A] R)

namespace SequenceSelectExec

def branches {A R : CType} (rel : SequenceSelectExec A R) :
    List (Callable [A] R) :=
  rel.first :: rel.rest

def runOne {A R : CType} (rel : SequenceSelectExec A R)
    (x : A.denote) : List R.denote :=
  rel.branches.map (fun branch => branch.invoke1 x)

def run {A R : CType} (rel : SequenceSelectExec A R)
    (xs : ValueVec A) : ValueVec R :=
  xs.flatMap rel.runOne

end SequenceSelectExec

structure NonemptyBranchTrace (A R : CType) where
  run : A.denote → R.denote × List R.denote

namespace NonemptyBranchTrace

def lastValue {A R : CType} (branch : NonemptyBranchTrace A R)
    (x : A.denote) : R.denote :=
  let trace := branch.run x
  trace.2.foldl (fun _ value => value) trace.1

end NonemptyBranchTrace

structure AllDoneFoldExec (A R : CType) where
  first : NonemptyBranchTrace A R
  rest : List (NonemptyBranchTrace A R)
  reducer : Callable [R, R] R

namespace AllDoneFoldExec

def runOne {A R : CType} (rel : AllDoneFoldExec A R)
    (x : A.denote) : R.denote :=
  (rel.rest.map (fun branch => branch.lastValue x)).foldl
    rel.reducer.invoke2 (rel.first.lastValue x)

def run {A R : CType} (rel : AllDoneFoldExec A R)
    (xs : ValueVec A) : ValueVec R :=
  xs.map rel.runOne

theorem run_length {A R : CType} (rel : AllDoneFoldExec A R)
    (xs : ValueVec A) :
    (rel.run xs).length = xs.length := by
  simp [run]

end AllDoneFoldExec

structure ZipInvokeExec (A L R O : CType) where
  left : Callable [A] L
  right : Callable [A] R
  combine : Callable [L, R] O

namespace ZipInvokeExec

def runOne {A L R O : CType} (zip : ZipInvokeExec A L R O)
    (x : A.denote) : O.denote :=
  zip.combine.invoke2 (zip.left.invoke1 x) (zip.right.invoke1 x)

def run {A L R O : CType} (zip : ZipInvokeExec A L R O)
    (xs : ValueVec A) : ValueVec O :=
  xs.map zip.runOne

theorem run_length {A L R O : CType} (zip : ZipInvokeExec A L R O)
    (xs : ValueVec A) :
    (zip.run xs).length = xs.length := by
  simp [run]

end ZipInvokeExec

private def relationLeft : Callable [CType.int] CType.long :=
  Callable.ofUnary (fun (x : Int) => x)

private def relationRight : Callable [CType.int] CType.long :=
  Callable.ofUnary (fun (x : Int) => x * 10)

private def relationAdd : Callable [CType.long, CType.long] CType.long :=
  Callable.ofBinary (fun (a b : Int) => a + b)

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

theorem StructuredRelationConformance.typed_relation_valid :
    checkRelation CType.int relationTyped.erase = some CType.long := by
  exact relationTyped.check_erase

theorem StructuredRelationConformance.typed_select_valid :
    checkRelation CType.int selectTyped.erase = some CType.long := by
  exact selectTyped.check_erase

private def zipRight : Callable [CType.int] CType.double :=
  Callable.ofUnary (fun (_ : Int) => (2.0 : Float))

private def zipCombine : Callable [CType.long, CType.double] CType.double :=
  Callable.ofBinary (fun (_ : Int) (right : Float) => right)

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

theorem StructuredRelationConformance.generated_descriptor_matches :
    CStructuredGenerated.relationWitnesses.all relationWitnessConforms = true := by
  native_decide

theorem StructuredRelationConformance.runtime_matches_model :
    CStructuredGenerated.relationWitnesses.all
      (fun w => w.count == w.output.length &&
        w.output == relationExec.run w.input) = true := by
  native_decide

theorem StructuredCoordinationConformance.coverage :
    CStructuredGenerated.coordinationWitnesses.map (fun w => w.name) =
      ["relation_any_select_i_l", "relation_sequence_select_i_l",
       "relation_all_done_fold_i_l"] := by
  native_decide

theorem StructuredCoordinationConformance.runtime_matches_model :
    CStructuredGenerated.coordinationWitnesses.all coordinationWitnessConforms = true := by
  native_decide

theorem StructuredZipConformance.runtime_matches_lowered_model :
    CStructuredGenerated.zipWitnesses.all zipWitnessConforms = true := by
  native_decide

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
