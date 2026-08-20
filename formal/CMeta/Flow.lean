import CMeta.Dispatch

/-!
# Typed CFlow lowering and dispatch safety

This layer connects the trait/signature/callable proofs to the operator shapes
used by CFlow.  The indices of `TypedOp` are stream input/output types; the
callable signature may have a different result shape (for example filter
returns Bool while preserving the stream element type).
-/

namespace CMeta

/-- A well-typed CFlow operator, indexed by stream input and stream output. -/
inductive TypedOp : CType → CType → Type where
  | filter (t : CType) : TypedOp t t
  | map (input output : CType) : TypedOp input output
  | transform (input output : CType) : TypedOp input output
  | flatMap (input output : CType) : TypedOp input output
  | reduce (t : CType) : TypedOp t t
  | zip (left right output : CType) : TypedOp left output

namespace TypedOp

/-- Surface CFlow operator represented by this typed node. -/
def operator {A R : CType} : TypedOp A R → Operator
  | .filter _ => .filter
  | .map _ _ => .map
  | .transform _ _ => .transform
  | .flatMap _ _ => .flatMap
  | .reduce _ => .reduce
  | .zip _ _ _ => .zip

/-- Exact callable ABI required by a typed node. -/
def signature {A R : CType} : TypedOp A R → Signature
  | .filter t => .unary t .bool
  | .map input output => .unary input output
  | .transform input output => .unary input output
  | .flatMap input output => .generator input output
  | .reduce t => .binary t t t
  | .zip left right output => .binary left right output

end TypedOp

/-- Dynamic type transition corresponding to CFlow graph admission. -/
def stepType : Operator → CType → Signature → Option CType
  | .filter, input, .unary a .bool =>
      if a = input then some input else none
  | .map, input, .unary a output =>
      if a = input then some output else none
  | .transform, input, .unary a output =>
      if a = input then some output else none
  | .flatMap, input, .generator a output =>
      if a = input then some output else none
  | .reduce, input, .binary a b output =>
      if a = input ∧ b = input ∧ output = input then some input else none
  | .zip, input, .binary a _ output =>
      if a = input then some output else none
  | _, _, _ => none

namespace TypedOp

/-- Every statically typed operator is admitted by the dynamic type transition. -/
theorem step_exact {A R : CType} (node : TypedOp A R) :
    stepType node.operator A node.signature = some R := by
  cases node <;> simp [operator, signature, stepType]

/-- Progress: a well-typed node never gets stuck at type admission. -/
theorem progress {A R : CType} (node : TypedOp A R) :
    ∃ next, stepType node.operator A node.signature = some next :=
  ⟨R, node.step_exact⟩

/-- Preservation: if the dynamic checker accepts the same node, its output is unique. -/
theorem output_unique {A R R' : CType} (node : TypedOp A R)
    (h : stepType node.operator A node.signature = some R') : R = R' := by
  have hs : (some R : Option CType) = some R' := node.step_exact.symm.trans h
  exact Option.some.inj hs

end TypedOp

/-- A pipeline is typed by construction: every node consumes the previous output. -/
inductive Pipeline : CType → CType → Type where
  | done (t : CType) : Pipeline t t
  | cons {A B R : CType} : TypedOp A B → Pipeline B R → Pipeline A R

namespace Pipeline

/-- Erase a typed pipeline to the operator/signature stream stored by graph IR. -/
def steps {A R : CType} : Pipeline A R → List (Operator × Signature)
  | .done _ => []
  | .cons node rest => (node.operator, node.signature) :: rest.steps

/-- Structural size of a typed pipeline. -/
def length {A R : CType} : Pipeline A R → Nat
  | .done _ => 0
  | .cons _ rest => rest.length + 1

theorem steps_length {A R : CType} (p : Pipeline A R) :
    p.steps.length = p.length := by
  induction p with
  | done t => rfl
  | cons node rest ih => simp [steps, length, ih]

end Pipeline

/-- Dynamic checker for the erased graph-level operator/signature stream. -/
def checkPipeline : CType → List (Operator × Signature) → Option CType
  | current, [] => some current
  | current, (op, sig) :: rest =>
      match stepType op current sig with
      | some next => checkPipeline next rest
      | none => none

/-- Type preservation for the whole pipeline after erasing the dependent indices. -/
theorem Pipeline.check_steps {A R : CType} (p : Pipeline A R) :
    checkPipeline A p.steps = some R := by
  induction p with
  | done t => rfl
  | cons node rest ih =>
      simp [Pipeline.steps, checkPipeline, TypedOp.step_exact, ih]

/-- Current built-in CFlow signature policy, mirrored from operator_policy.h. -/
def cflowBuiltInPolicy : OperatorPolicy
  | .filter => [.unary .int .bool]
  | .map =>
      [.unary .int .int,
       .unary .int .long,
       .unary .long .double,
       .unary .double .int,
       .unary .int .double,
       .unary .int .float,
       .unary .float .double]
  | .transform => [.unary .int .long]
  | .flatMap => [.generator .int .long]
  | .reduce => [.binary .long .long .long]
  | .zip => [.binary .long .double .double]

/-- One successfully resolved typed node. -/
structure ResolvedStep (rules : List DispatchRule) (A R : CType) where
  node : TypedOp A R
  target : Nat
  dispatches : dispatch rules node.operator node.signature = some target

namespace ResolvedStep

theorem dispatch_exact {rules : List DispatchRule} {A R : CType}
    (s : ResolvedStep rules A R) :
    ∃ rule, rule ∈ rules ∧
      rule.op = s.node.operator ∧
      rule.sig = s.node.signature ∧
      rule.target = s.target := by
  exact dispatch_sound rules s.node.operator s.node.signature s.target s.dispatches

end ResolvedStep

/-- A target implementation has one logical signature even if reused by operators. -/
def TargetSignatureUnique (rules : List DispatchRule) : Prop :=
  ∀ r1, r1 ∈ rules → ∀ r2, r2 ∈ rules →
    r1.target = r2.target → r1.sig = r2.sig

/-- Dispatch well-formedness needed for type-safe target reuse. -/
structure WellFormedDispatch (policy : OperatorPolicy)
    (rules : List DispatchRule) : Prop where
  respectsPolicy : RulesRespectPolicy policy rules
  targetSignatureUnique : TargetSignatureUnique rules

namespace ResolvedStep

/-- A resolved typed node can only use a signature admitted by its operator policy. -/
theorem policy_safe {policy : OperatorPolicy} {rules : List DispatchRule}
    {A R : CType} (wf : WellFormedDispatch policy rules)
    (s : ResolvedStep rules A R) :
    s.node.signature ∈ policy s.node.operator := by
  exact dispatch_policy_sound policy rules wf.respectsPolicy
    s.node.operator s.node.signature s.target s.dispatches

/-- Main target-safety theorem: any rule sharing the selected implementation
    target has exactly the typed node's logical signature. -/
theorem target_signature_safe {policy : OperatorPolicy}
    {rules : List DispatchRule} {A R : CType}
    (wf : WellFormedDispatch policy rules) (s : ResolvedStep rules A R)
    (rule : DispatchRule) (hrule : rule ∈ rules)
    (htarget : rule.target = s.target) :
    rule.sig = s.node.signature := by
  obtain ⟨selected, hselected, _, hsig, htargetSelected⟩ := s.dispatch_exact
  have sameTarget : rule.target = selected.target :=
    htarget.trans htargetSelected.symm
  have hs : rule.sig = selected.sig :=
    wf.targetSignatureUnique rule hrule selected hselected sameTarget
  exact hs.trans hsig

/-- Equivalent negative form: an incompatible signature cannot inhabit the
    implementation target selected for a well-typed node. -/
theorem cannot_target_incompatible {policy : OperatorPolicy}
    {rules : List DispatchRule} {A R : CType}
    (wf : WellFormedDispatch policy rules) (s : ResolvedStep rules A R)
    (rule : DispatchRule) (hrule : rule ∈ rules)
    (htarget : rule.target = s.target) (bad : Signature)
    (hbad : bad ≠ s.node.signature) : rule.sig ≠ bad := by
  intro heq
  apply hbad
  calc
    bad = rule.sig := heq.symm
    _ = s.node.signature := s.target_signature_safe wf rule hrule htarget

end ResolvedStep

end CMeta
