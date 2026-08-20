import CMeta.Lowering

/-!
# Direct execution-plan type preservation

This module models the type-relevant part of `cflow_plan_compile`.
The direct plan accepts only a linear normalized subset and erases graph
topology into an instruction array.  Each instruction keeps only its opcode,
input/output type descriptors and callback signature(s).

`MAP` and `TRANSFORM` both compile to the MAP opcode.  An optimized fused map
keeps the ordered callback `fn_chain`, matching `cflow_plan_inst.fn_chain`.
-/

namespace CMeta

inductive PlanOpcode where
  | filter
  | map
  | flatMap
  | reduce
  deriving Repr, DecidableEq

/-- Type-relevant view of one `cflow_plan_inst`. -/
structure ErasedPlanInst where
  opcode : PlanOpcode
  input : CType
  output : CType
  callbacks : List Signature
  deriving Repr, DecidableEq

/-- A node in the subset accepted by the direct plan compiler. -/
inductive PlanNode : CType → CType → Type where
  | filter (t : CType) : PlanNode t t
  | map (input output : CType) : PlanNode input output
  | transform (input output : CType) : PlanNode input output
  | fusedMap (input output : CType) (chain : MapChain input output) :
      PlanNode input output
  | flatMap (input output : CType) : PlanNode input output
  | reduce (t : CType) : PlanNode t t

namespace PlanNode

/-- Compilation to the instruction fields copied by `plan_compile.c`. -/
def erase : {A R : CType} → PlanNode A R → ErasedPlanInst
  | _, _, .filter t =>
      ⟨.filter, t, t, [.unary t .bool]⟩
  | _, _, .map input output =>
      ⟨.map, input, output, [.unary input output]⟩
  | _, _, .transform input output =>
      ⟨.map, input, output, [.unary input output]⟩
  | _, _, .fusedMap input output chain =>
      ⟨.map, input, output, chain.signatures⟩
  | _, _, .flatMap input output =>
      ⟨.flatMap, input, output, [.generator input output]⟩
  | _, _, .reduce t =>
      ⟨.reduce, t, t, [.binary t t t]⟩

end PlanNode

/-- Validate one instruction using only fields retained in the direct plan. -/
def checkPlanInst (current : CType) (inst : ErasedPlanInst) : Option CType :=
  if inst.input = current then
    match inst.opcode with
    | .filter =>
        if inst.callbacks = [.unary current .bool] ∧ inst.output = current
        then some current
        else none
    | .map =>
        match MapChain.check current inst.callbacks with
        | some output =>
            if output = inst.output then some inst.output else none
        | none => none
    | .flatMap =>
        if inst.callbacks = [.generator current inst.output]
        then some inst.output
        else none
    | .reduce =>
        if inst.callbacks = [.binary current current current] ∧
            inst.output = current
        then some current
        else none
  else none

/-- Every statically compilable node survives instruction erasure. -/
theorem PlanNode.check_erase {A R : CType} (node : PlanNode A R) :
    checkPlanInst A node.erase = some R := by
  cases node with
  | filter t => simp [PlanNode.erase, checkPlanInst]
  | map input output =>
      simp [PlanNode.erase, checkPlanInst, MapChain.check]
  | transform input output =>
      simp [PlanNode.erase, checkPlanInst, MapChain.check]
  | fusedMap input output chain =>
      simp [PlanNode.erase, checkPlanInst, MapChain.check_signatures]
  | flatMap input output => simp [PlanNode.erase, checkPlanInst]
  | reduce t => simp [PlanNode.erase, checkPlanInst]

/-- `TRANSFORM` is canonicalized to the same direct MAP opcode. -/
theorem transform_compiles_as_map (A R : CType) :
    (PlanNode.transform A R).erase.opcode = .map := rfl

/-- Linear normalized program accepted by `cflow_plan_graph_supported`. -/
inductive PlanProgram : CType → CType → Type where
  | done (t : CType) : PlanProgram t t
  | cons {A B R : CType} :
      PlanNode A B → PlanProgram B R → PlanProgram A R

namespace PlanProgram

/-- Drop graph topology and retain only the direct instruction array. -/
def code : {A R : CType} → PlanProgram A R → List ErasedPlanInst
  | _, _, .done _ => []
  | _, _, .cons node rest => node.erase :: rest.code

end PlanProgram

/-- Sequential type validation of a compiled instruction array. -/
def checkPlan : CType → List ErasedPlanInst → Option CType
  | current, [] => some current
  | current, inst :: rest =>
      match checkPlanInst current inst with
      | some next => checkPlan next rest
      | none => none

/-- Losing Graph/Node/Edge/Subgraph topology does not lose the type chain. -/
theorem PlanProgram.check_code {A R : CType} (program : PlanProgram A R) :
    checkPlan A program.code = some R := by
  induction program with
  | done t => rfl
  | cons node rest ih =>
      simp [PlanProgram.code, checkPlan, PlanNode.check_erase, ih]

/-- Public type fields plus direct code, mirroring `cflow_plan`. -/
structure ErasedPlan where
  input : CType
  output : CType
  code : List ErasedPlanInst
  deriving Repr, DecidableEq

namespace PlanProgram

/-- Compile a statically supported program to the topology-free plan shape. -/
def compile {A R : CType} (program : PlanProgram A R) : ErasedPlan :=
  ⟨A, R, program.code⟩

end PlanProgram

/-- Runtime-checkable well-typedness of an erased direct plan. -/
def PlanWellTyped (plan : ErasedPlan) : Prop :=
  checkPlan plan.input plan.code = some plan.output

/-- Main plan-compilation theorem: public endpoints and every intermediate
    instruction remain type-consistent after topology erasure. -/
theorem PlanProgram.compile_well_typed {A R : CType}
    (program : PlanProgram A R) :
    PlanWellTyped program.compile := by
  exact program.check_code

/-- The plan compiler preserves the Graph root input/output types exactly. -/
theorem PlanProgram.compile_endpoints {A R : CType}
    (program : PlanProgram A R) :
    program.compile.input = A ∧ program.compile.output = R := by
  exact ⟨rfl, rfl⟩

/-- A compiled program has one uniquely recoverable final CType. -/
theorem PlanProgram.output_unique {A R R' : CType}
    (program : PlanProgram A R)
    (h : checkPlan A program.compile.code = some R') : R = R' := by
  have hs : (some R : Option CType) = some R' :=
    program.check_code.symm.trans h
  exact Option.some.inj hs

end CMeta
