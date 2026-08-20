import CMeta.Plan

/-!
# Direct plan execution type safety

This module models the type-relevant execution behavior in
`cflow/src/plan_exec.c`.

A runtime value vector carries a logical `CType` tag in addition to its values.
That tag is essential: distinct CMeta descriptors such as `int` and `long` may
share the same Lean host representation, but they remain different logical
runtime types.

The generator model below represents a successfully completed finite generator
trace.  This proves result-type safety of FLAT_MAP; it intentionally does not
claim that an arbitrary C generator eventually returns `DONE`.
-/

namespace CMeta

/-- Values in one runtime vector, indexed by their logical CMeta type. -/
abbrev ValueVec (T : CType) := List T.denote

/-- A value vector after runtime type erasure.  The CType tag remains explicit,
    matching `cflow_plan_value_vec.type`. -/
abbrev PackedVec := Sigma (fun T : CType => ValueVec T)

/-- Finite successful semantics of one generator callback.  Cursor/state
    termination is a separate operational obligation. -/
structure CompletedGenerator (A R : CType) where
  generateAll : A.denote → List R.denote

/-- Executable instruction in the exact direct-plan subset. -/
inductive ExecInst : CType → CType → Type where
  | filter (T : CType) (pred : Callable1 T .bool) : ExecInst T T
  | map (A R : CType) (chain : MapChain A R) : ExecInst A R
  | flatMap (A R : CType) (gen : CompletedGenerator A R) : ExecInst A R
  | reduce (T : CType) (reducer : Callable2 T T T) : ExecInst T T

/-- REDUCE in `plan_exec.c` returns zero values for empty input and exactly one
    accumulated value for non-empty input. -/
def reduceValues {T : CType} (reducer : Callable2 T T T) :
    ValueVec T → ValueVec T
  | [] => []
  | x :: xs => [xs.foldl reducer.run x]

namespace ExecInst

/-- Typed denotation of one successfully executed plan instruction. -/
def run : {A R : CType} → ExecInst A R → ValueVec A → ValueVec R
  | _, _, .filter _ pred => fun xs => xs.filter pred.run
  | _, _, .map _ _ chain => fun xs => xs.map chain.run
  | _, _, .flatMap _ _ gen => fun xs => xs.flatMap gen.generateAll
  | _, _, .reduce _ reducer => reduceValues reducer

/-- The executable instruction erases to the same type-level instruction shape
    accepted by the direct plan compiler. -/
def planNode : {A R : CType} → ExecInst A R → PlanNode A R
  | _, _, .filter T _ => .filter T
  | _, _, .map A R chain => .fusedMap A R chain
  | _, _, .flatMap A R _ => .flatMap A R
  | _, _, .reduce T _ => .reduce T

/-- MAP preserves vector cardinality even when implemented by an optimized
    callback chain. -/
theorem map_length {A R : CType} (chain : MapChain A R)
    (xs : ValueVec A) :
    (xs.map chain.run).length = xs.length := by
  simp

/-- REDUCE cardinality matches the C executor: zero stays zero; non-empty input
    becomes exactly one accumulated value. -/
theorem reduce_length_le_one {T : CType} (reducer : Callable2 T T T)
    (xs : ValueVec T) :
    (reduceValues reducer xs).length ≤ 1 := by
  cases xs <;> simp [reduceValues]

/-- The compiled descriptor for an executable instruction is type-valid. -/
theorem planNode_checked {A R : CType} (inst : ExecInst A R) :
    checkPlanInst A inst.planNode.erase = some R :=
  inst.planNode.check_erase

end ExecInst

/-- Runtime-erased executable instruction.  The implementation remains
    dependently tied to the declared input/output descriptors. -/
structure RuntimeInst where
  input : CType
  output : CType
  impl : ValueVec input → ValueVec output

namespace ExecInst

/-- Erase constructor information while retaining the runtime CType contract. -/
def runtime {A R : CType} (inst : ExecInst A R) : RuntimeInst :=
  ⟨A, R, inst.run⟩

end ExecInst

/-- Execute one runtime instruction after checking the vector's logical CType,
    mirroring the `v->type == i->input_type` guards in `plan_exec.c`. -/
def runRuntimeInst (inst : RuntimeInst) (values : PackedVec) : Option PackedVec :=
  if h : values.1 = inst.input then
    let input : ValueVec inst.input := h ▸ values.2
    some ⟨inst.output, inst.impl input⟩
  else
    none

/-- Any successful runtime instruction produces a vector tagged with exactly
    its declared output CType. -/
theorem runRuntimeInst_output (inst : RuntimeInst) (values out : PackedVec)
    (h : runRuntimeInst inst values = some out) :
    out.1 = inst.output := by
  unfold runRuntimeInst at h
  split at h
  · simp_all
  · simp_all

/-- A typed instruction cannot fail its own runtime input-type guard. -/
theorem ExecInst.runtime_exact {A R : CType} (inst : ExecInst A R)
    (values : ValueVec A) :
    runRuntimeInst inst.runtime ⟨A, values⟩ = some ⟨R, inst.run values⟩ := by
  simp [ExecInst.runtime, runRuntimeInst]

/-- Executable direct plan. Every instruction consumes the preceding vector
    type by construction. -/
inductive ExecProgram : CType → CType → Type where
  | done (T : CType) : ExecProgram T T
  | cons {A B R : CType} :
      ExecInst A B → ExecProgram B R → ExecProgram A R

namespace ExecProgram

/-- Typed execution semantics of the whole plan. -/
def run : {A R : CType} → ExecProgram A R → ValueVec A → ValueVec R
  | _, _, .done _ => fun values => values
  | _, _, .cons inst rest => fun values => rest.run (inst.run values)

/-- Type-only plan program emitted by the compiler model. -/
def planProgram : {A R : CType} → ExecProgram A R → PlanProgram A R
  | _, _, .done T => .done T
  | _, _, .cons inst rest => .cons inst.planNode rest.planProgram

/-- Runtime instruction sequence after dependent constructor erasure. -/
def runtimeCode : {A R : CType} → ExecProgram A R → List RuntimeInst
  | _, _, .done _ => []
  | _, _, .cons inst rest => inst.runtime :: rest.runtimeCode

end ExecProgram

/-- Execute the runtime-erased instruction array. -/
def runRuntimePlan : List RuntimeInst → PackedVec → Option PackedVec
  | [], values => some values
  | inst :: rest, values =>
      match runRuntimeInst inst values with
      | some next => runRuntimePlan rest next
      | none => none

/-- Main execution theorem: erasing an executable typed plan to runtime
    instructions and running the same type guards as the C executor cannot
    produce a wrongly tagged result. -/
theorem ExecProgram.runtime_execution_exact {A R : CType}
    (program : ExecProgram A R) (values : ValueVec A) :
    runRuntimePlan program.runtimeCode ⟨A, values⟩ =
      some ⟨R, program.run values⟩ := by
  induction program generalizing values with
  | done T => rfl
  | cons inst rest ih =>
      simp [ExecProgram.runtimeCode, runRuntimePlan,
        ExecInst.runtime_exact, ExecProgram.run, ih]

/-- Therefore every successful execution of a compiled typed program has the
    statically declared final CType. -/
theorem ExecProgram.result_type_safe {A R : CType}
    (program : ExecProgram A R) (values : ValueVec A) (out : PackedVec)
    (h : runRuntimePlan program.runtimeCode ⟨A, values⟩ = some out) :
    out.1 = R := by
  have exactRun := program.runtime_execution_exact values
  rw [exactRun] at h
  exact Sigma.mk.inj_iff.mp (Option.some.inj h).1

/-- The same executable program also passes the topology-free compiler type
    checker proved in `Plan.lean`. -/
theorem ExecProgram.compiled_plan_well_typed {A R : CType}
    (program : ExecProgram A R) :
    PlanWellTyped program.planProgram.compile := by
  exact program.planProgram.compile_well_typed

end CMeta
