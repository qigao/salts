module
import all CMeta.StructuredConformance
import all CMeta.StructuredPolicyGeneratedC

/-!
# Remaining structured relation policy conformance

This module closes the main relation-policy gaps left after the coordination
suite with deterministic synchronous witnesses.
-/

namespace CMeta

structure TwoValueTrace (A R : CType) where
  first : A.denote → R.denote
  second : A.denote → R.denote

structure LatestFold2Exec (A R : CType) where
  left : TwoValueTrace A R
  right : TwoValueTrace A R
  reducer : Callable [R, R] R

namespace LatestFold2Exec

def runOne {A R : CType} (rel : LatestFold2Exec A R)
    (x : A.denote) : List R.denote :=
  [ rel.reducer.invoke2 (rel.left.first x) (rel.right.first x),
    rel.reducer.invoke2 (rel.left.second x) (rel.right.first x),
    rel.reducer.invoke2 (rel.left.second x) (rel.right.second x) ]

def run {A R : CType} (rel : LatestFold2Exec A R)
    (xs : ValueVec A) : ValueVec R :=
  xs.flatMap rel.runOne

end LatestFold2Exec

structure FailThenSelectExec (A R : CType) where
  fallback : Callable [A] R

namespace FailThenSelectExec

def run {A R : CType} (rel : FailThenSelectExec A R)
    (xs : ValueVec A) : ValueVec R :=
  xs.map rel.fallback.invoke1

theorem run_length {A R : CType} (rel : FailThenSelectExec A R)
    (xs : ValueVec A) :
    (rel.run xs).length = xs.length := by
  simp [run]

end FailThenSelectExec

private def latestLeft : TwoValueTrace CType.int CType.long :=
  ⟨fun (x : Int) => x, fun (x : Int) => x + 1⟩

private def latestRight : TwoValueTrace CType.int CType.long :=
  ⟨fun (x : Int) => x * 10, fun (x : Int) => x * 10 + 100⟩

private def policyAdd : Callable [CType.long, CType.long] CType.long :=
  Callable.ofBinary (fun (a b : Int) => a + b)

private def latestExec : LatestFold2Exec CType.int CType.long :=
  ⟨latestLeft, latestRight, policyAdd⟩

private def fallbackCallable : Callable [CType.int] CType.long :=
  Callable.ofUnary (fun (x : Int) => x * 10)

private def failThenSelectExec : FailThenSelectExec CType.int CType.long :=
  ⟨fallbackCallable⟩

private def flatMapPipeline : Pipeline CType.int CType.long :=
  .cons (.flatMap CType.int CType.long) (.done CType.long)

private def mapPipeline : Pipeline CType.int CType.long :=
  .cons (.map CType.int CType.long) (.done CType.long)

private def latestTyped : TypedRelation CType.int CType.long :=
  .fold
    { first := flatMapPipeline, rest := [flatMapPipeline] }
    policyAdd

private def fallbackTyped : TypedRelation CType.int CType.long :=
  .select
    { first := flatMapPipeline, rest := [mapPipeline] }

theorem StructuredPolicyConformance.latest_type_valid :
    checkRelation CType.int latestTyped.erase = some CType.long := by
  exact latestTyped.check_erase

theorem StructuredPolicyConformance.fallback_type_valid :
    checkRelation CType.int fallbackTyped.erase = some CType.long := by
  exact fallbackTyped.check_erase

private structure PolicyModelResult where
  inputType : String
  outputType : String
  coordination : String
  completion : String
  result : String
  error : String
  branchCount : Nat
  reducer : String
  output : List Int

private def policyModel (name : String) (input : List Int) :
    Option PolicyModelResult :=
  match name with
  | "relation_latest_fold_i_l" =>
      some ⟨"I", "L", "LATEST", "COORDINATOR", "FOLD", "FAIL_FAST",
        2, "B_L_L_L", latestExec.run input⟩
  | "relation_any_ignore_i_l" =>
      some ⟨"I", "L", "ANY", "COORDINATOR", "SELECT", "IGNORE",
        2, "", failThenSelectExec.run input⟩
  | "relation_fallback_try_next_i_l" =>
      some ⟨"I", "L", "SEQUENCE", "FIRST_RESULT", "SELECT", "TRY_NEXT",
        2, "", failThenSelectExec.run input⟩
  | _ => none

private def policyWitnessConforms
    (w : CStructuredPolicyGenerated.PolicyWitness) : Bool :=
  match policyModel w.name w.input with
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

theorem StructuredPolicyConformance.coverage :
    CStructuredPolicyGenerated.policyWitnesses.map (fun w => w.name) =
      ["relation_latest_fold_i_l", "relation_any_ignore_i_l",
       "relation_fallback_try_next_i_l"] := by
  native_decide

theorem StructuredPolicyConformance.runtime_matches_model :
    CStructuredPolicyGenerated.policyWitnesses.all policyWitnessConforms = true := by
  native_decide

theorem CImplementationConformance.structured_policy_runtime :
    CStructuredPolicyGenerated.policyWitnesses.all policyWitnessConforms = true := by
  exact StructuredPolicyConformance.runtime_matches_model

end CMeta
