import Std

namespace CMetaCFlowCalculus.CMeta

/-- A finite compile-time function is represented by explicit input/output
    rows. Product input types model the public binary and ternary C macros. -/
abbrev FiniteRelation (ι ο : Type) := List (ι × ο)

private def unique [BEq α] [LawfulBEq α] : List α → Bool
  | [] => true
  | value :: rest => !rest.contains value && unique rest

/-- No input key is declared more than once. This representation-level
    property gives every successful token lookup one declared result. -/
def Functional [BEq ι] [LawfulBEq ι]
    (rows : FiniteRelation ι ο) : Bool :=
  unique (rows.map fun row => row.1)

/-- Every key in the declared domain has a relation row. -/
def TotalOn [BEq ι] [LawfulBEq ι]
    (domain : List ι) (rows : FiniteRelation ι ο) : Bool :=
  let inputs := rows.map fun row => row.1
  domain.all fun input => inputs.contains input

/-- Every relation result belongs to the declared output universe. -/
def ClosedOver [BEq ο] [LawfulBEq ο]
    (outputs : List ο) (rows : FiniteRelation ι ο) : Bool :=
  rows.all fun row => outputs.contains row.2

/-- Executable proof-facing contract for one finite type or value function. -/
def WellFormed [BEq ι] [LawfulBEq ι] [BEq ο] [LawfulBEq ο]
    (domain : List ι) (outputs : List ο)
    (rows : FiniteRelation ι ο) : Prop :=
  Functional rows = true ∧
    TotalOn domain rows = true ∧
    ClosedOver outputs rows = true

/-- First-match lookup mirrors the generated C identifier selected by one
    `TypeEval*` or `ValueEval*` invocation. -/
def lookup [DecidableEq ι]
    (input : ι) : FiniteRelation ι ο → Option ο
  | [] => none
  | row :: rest =>
      if row.1 = input then some row.2 else lookup input rest

theorem lookup_some_mem [DecidableEq ι]
    (input : ι) (rows : FiniteRelation ι ο) (output : ο)
    (found : lookup input rows = some output) :
    (input, output) ∈ rows := by
  induction rows with
  | nil => simp [lookup] at found
  | cons row rest inductionHypothesis =>
      by_cases sameInput : row.1 = input
      · simp [lookup, sameInput] at found
        apply List.mem_cons.mpr
        left
        apply Prod.ext
        · exact sameInput.symm
        · exact found.symm
      · simp [lookup, sameInput] at found
        exact List.mem_cons_of_mem row (inductionHypothesis found)

theorem WellFormed.functional
    [BEq ι] [LawfulBEq ι] [BEq ο] [LawfulBEq ο]
    {domain : List ι} {outputs : List ο} {rows : FiniteRelation ι ο}
    (wellFormed : WellFormed domain outputs rows) :
    Functional rows = true :=
  wellFormed.1

theorem WellFormed.totalOn
    [BEq ι] [LawfulBEq ι] [BEq ο] [LawfulBEq ο]
    {domain : List ι} {outputs : List ο} {rows : FiniteRelation ι ο}
    (wellFormed : WellFormed domain outputs rows) :
    TotalOn domain rows = true :=
  wellFormed.2.1

theorem WellFormed.closedOver
    [BEq ι] [LawfulBEq ι] [BEq ο] [LawfulBEq ο]
    {domain : List ι} {outputs : List ο} {rows : FiniteRelation ι ο}
    (wellFormed : WellFormed domain outputs rows) :
    ClosedOver outputs rows = true :=
  wellFormed.2.2

/-- An explicit finite DFA. Transitions and accepting results use the same
    finite-relation representation as CMeta type and value functions. -/
structure FiniteDfa (σ ο : Type) where
  start : Nat
  transitions : FiniteRelation (Nat × σ) Nat
  accepts : FiniteRelation Nat ο

/-- Determinism requires one target for each state/symbol pair and one result
    for each accepting state. -/
def DfaWellFormed [BEq σ] [LawfulBEq σ]
    (dfa : FiniteDfa σ ο) : Prop :=
  Functional dfa.transitions = true ∧
    Functional dfa.accepts = true

/-- One DFA transition is an ordinary finite-relation lookup. -/
def dfaStep [DecidableEq σ]
    (dfa : FiniteDfa σ ο) (state : Nat) (symbol : σ) : Option Nat :=
  lookup (state, symbol) dfa.transitions

/-- Consume a symbol list and retain the reached state for proof reuse. -/
def dfaRunFrom [DecidableEq σ]
    (dfa : FiniteDfa σ ο) : Nat → List σ → Option Nat
  | state, [] => some state
  | state, symbol :: rest =>
      match dfaStep dfa state symbol with
      | none => none
      | some next => dfaRunFrom dfa next rest

/-- A run succeeds only when the reached state has an explicit accept row. -/
def dfaRun [DecidableEq σ] [DecidableEq ο]
    (dfa : FiniteDfa σ ο) (symbols : List σ) : Option ο :=
  match dfaRunFrom dfa dfa.start symbols with
  | none => none
  | some state => lookup state dfa.accepts

theorem dfaStep_some_mem [DecidableEq σ]
    (dfa : FiniteDfa σ ο) (source : Nat) (symbol : σ) (target : Nat)
    (found : dfaStep dfa source symbol = some target) :
    ((source, symbol), target) ∈ dfa.transitions := by
  exact lookup_some_mem (source, symbol) dfa.transitions target found

theorem dfaAccept_some_mem [DecidableEq ο]
    (dfa : FiniteDfa σ ο) (state : Nat) (output : ο)
    (found : lookup state dfa.accepts = some output) :
    (state, output) ∈ dfa.accepts := by
  exact lookup_some_mem state dfa.accepts output found

end CMetaCFlowCalculus.CMeta
