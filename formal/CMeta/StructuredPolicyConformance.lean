import CMeta.StructuredConformance
import CMeta.StructuredPolicyGeneratedC

/-!
# Remaining structured relation policy conformance

This module closes the main relation-policy gaps left after the coordination
suite:

* synchronous two-branch `LATEST + FOLD` combine-latest behavior;
* `ANY + SELECT + IGNORE`, where a failing branch is treated as completed and
  another branch may still win;
* `SEQUENCE + SELECT + FIRST_RESULT + TRY_NEXT`, the fallback policy that skips
  a failing branch and selects the next successful branch.

The generated observations come from the real CFlow scheduler/relation runtime.
The models below are intentionally scoped to the deterministic synchronous
witnesses used by CI; they do not claim arbitrary asynchronous scheduling
fairness or termination.
-/

namespace CMeta

/-- A synchronous branch that produces exactly two values before completing. -/
structure TwoValueTrace (A R : CType) where
  first : A.denote → R.denote
  second : A.denote → R.denote

/-- Two-branch combine-latest/FOLD execution matching the deterministic cursor
    order used by the synchronous LATEST witness. -/
structure LatestFold2Exec (A R : CType) where
  left : TwoValueTrace A R
  right : TwoValueTrace A R
  reducer : Callable2 R R R

namespace LatestFold2Exec

/-- Initial readiness occurs after left[0], right[0]; later updates are emitted
    in cursor order left[1], right[1]. Each update folds the latest values. -/
def runOne {A R : CType} (rel : LatestFold2Exec A R)
    (x : A.denote) : List R.denote :=
  [ rel.reducer.run (rel.left.first x) (rel.right.first x),
    rel.reducer.run (rel.left.second x) (rel.right.first x),
    rel.reducer.run (rel.left.second x) (rel.right.second x) ]

def run {A R : CType} (rel : LatestFold2Exec A R)
    (xs : ValueVec A) : ValueVec R :=
  xs.flatMap rel.runOne

/-- The concrete two-value/two-branch LATEST witness emits three combinations
    per source value. -/
theorem run_length {A R : CType} (rel : LatestFold2Exec A R)
    (xs : ValueVec A) :
    (rel.run xs).length = xs.length * 3 := by
  induction xs with
  | nil => rfl
  | cons x xs ih =>
      simp [run, runOne, ih, Nat.add_mul]

end LatestFold2Exec

/-- Value semantics shared by IGNORE and TRY_NEXT witnesses: the first branch
    fails before producing a value, so the second branch is selected. The
    distinction between the policies is checked separately in generated
    descriptor metadata. -/
structure FailThenSelectExec (A R : CType) where
  fallback : Callable1 A R

namespace FailThenSelectExec

def run {A R : CType} (rel : FailThenSelectExec A R)
    (xs : ValueVec A) : ValueVec R :=
  xs.map rel.fallback.run

theorem run_length {A R : CType} (rel : FailThenSelectExec A R)
    (xs : ValueVec A) :
    (rel.run xs).length = xs.length := by
  simp [run]

end FailThenSelectExec

private def latestLeft : TwoValueTrace CType.int CType.long :=
  ⟨fun (x : Int) => x, fun (x : Int) => x + 1⟩

private def latestRight : TwoValueTrace CType.int CType.long :=
  ⟨fun (x : Int) => x * 10, fun (x : Int) => x * 10 + 100⟩

private def policyAdd : Callable2 CType.long CType.long CType.long :=
  ⟨fun (a b : Int) => a + b⟩

private def latestExec : LatestFold2Exec CType.int CType.long :=
  ⟨latestLeft, latestRight, policyAdd⟩

private def fallbackCallable : Callable1 CType.int CType.long :=
  ⟨fun (x : Int) => x * 10⟩

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

/-- LATEST changes coordination multiplicity, not the homogeneous FOLD type
    equation. -/
theorem StructuredPolicyConformance.latest_type_valid :
    checkRelation CType.int latestTyped.erase = some CType.long := by
  exact latestTyped.check_erase

/-- IGNORE and TRY_NEXT change failure routing, not the homogeneous SELECT type
    equation. -/
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

/-- The policy suite covers the remaining major coordination/error-routing
    combinations modeled here. -/
theorem StructuredPolicyConformance.coverage :
    CStructuredPolicyGenerated.policyWitnesses.map (fun w => w.name) =
      ["relation_latest_fold_i_l", "relation_any_ignore_i_l",
       "relation_fallback_try_next_i_l"] := by
  native_decide

/-- Real coord.c + relation_exec.c observations agree with the explicit LATEST,
    IGNORE and TRY_NEXT models, including descriptor metadata and direct-plan
    rejection. -/
theorem StructuredPolicyConformance.runtime_matches_model :
    CStructuredPolicyGenerated.policyWitnesses.all policyWitnessConforms = true := by
  native_decide

/-- Public gate for the remaining structured relation policies. -/
theorem CImplementationConformance.structured_policy_runtime :
    CStructuredPolicyGenerated.policyWitnesses.all policyWitnessConforms = true := by
  exact StructuredPolicyConformance.runtime_matches_model

end CMeta
