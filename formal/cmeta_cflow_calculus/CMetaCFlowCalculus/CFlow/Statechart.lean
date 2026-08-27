namespace CMetaCFlowCalculus.CFlow.Statechart

/-- One already-enabled C candidate with its normalized ancestry and exit set. -/
structure Candidate where
  transition : Nat
  source : Nat
  sourceAncestors : List Nat
  leafOrder : Nat
  exitSet : List Nat
  deriving Repr, DecidableEq

private def overlaps (left right : Candidate) : Bool :=
  left.exitSet.any fun state => right.exitSet.contains state

def conflicts (left right : Candidate) : Bool :=
  overlaps left right || overlaps right left

def properDescendantSource (descendant ancestor : Candidate) : Bool :=
  descendant.sourceAncestors.contains ancestor.source

def rejectedBySelected (candidate : Candidate)
    (selected : List Candidate) : Bool :=
  selected.any fun current =>
    conflicts current candidate && !properDescendantSource candidate current

def retainNonconflicting (candidate : Candidate) : List Candidate →
    List Candidate :=
  List.filter fun current => !conflicts current candidate

def insertCandidate (selected : List Candidate)
    (candidate : Candidate) : List Candidate :=
  if rejectedBySelected candidate selected then selected
  else retainNonconflicting candidate selected ++ [candidate]

structure Selection where
  seenTransitions : List Nat
  selected : List Candidate
  deriving Repr, DecidableEq

def initialSelection : Selection :=
  { seenTransitions := [], selected := [] }

def stepCandidate (state : Selection) (candidate : Candidate) : Selection :=
  if state.seenTransitions.contains candidate.transition then state
  else
    { seenTransitions := state.seenTransitions ++ [candidate.transition]
      selected := insertCandidate state.selected candidate }

def evaluateFrom : Selection → List Candidate → Selection
  | state, [] => state
  | state, candidate :: remaining =>
      evaluateFrom (stepCandidate state candidate) remaining

/-- The input is C's active-leaf/document order, leaf-to-ancestor bubbling
    order, and normalized trigger/priority/document row order. The forward
    evaluation consumes that order once and stably ignores repeated IDs. -/
def select (candidates : List Candidate) : List Candidate :=
  (evaluateFrom initialSelection candidates).selected

def ConflictFree (candidates : List Candidate) : Prop :=
  candidates.Pairwise fun left right => conflicts left right = false

/-- C's exit-order comparator: descendants precede ancestors; unrelated
    states use reverse document order. -/
def exitPrecedes (ancestors : Nat → List Nat) (documentOrder : Nat → Nat)
    (left right : Nat) : Bool :=
  if (ancestors left).contains right then true
  else if (ancestors right).contains left then false
  else documentOrder right < documentOrder left

/-- C's entry-order comparator: ancestors precede descendants; unrelated
    states use document order. -/
def entryPrecedes (ancestors : Nat → List Nat) (documentOrder : Nat → Nat)
    (left right : Nat) : Bool :=
  if (ancestors right).contains left then true
  else if (ancestors left).contains right then false
  else documentOrder left < documentOrder right

def ExitOrdered (ancestors : Nat → List Nat) (documentOrder : Nat → Nat)
    (states : List Nat) : Prop :=
  states.Pairwise fun left right =>
    exitPrecedes ancestors documentOrder left right = true

def EntryOrdered (ancestors : Nat → List Nat) (documentOrder : Nat → Nat)
    (states : List Nat) : Prop :=
  states.Pairwise fun left right =>
    entryPrecedes ancestors documentOrder left right = true

/-- The normalized facts consumed by C's dense configuration validator. -/
structure ConfigurationModel where
  stateUniverse : List Nat
  ancestors : Nat → List Nat
  children : Nat → List Nat
  isReal : Nat → Bool
  isCompound : Nat → Bool
  isParallel : Nat → Bool
  isLeaf : Nat → Bool

/-- The pure proof boundary after C has constructed and validated a staged
    configuration. It mirrors no-pseudo, ancestry, compound exclusivity,
    parallel coverage, uniqueness, bounds, and leaf-presence checks. Callback
    execution and allocation are outside this model. -/
def LegalConfiguration (model : ConfigurationModel)
    (active : List Nat) : Prop :=
  active.Nodup ∧
  (∀ state ∈ active, state ∈ model.stateUniverse) ∧
  (∀ state ∈ active, model.isReal state = true) ∧
  (∀ state ∈ active, ∀ ancestor ∈ model.ancestors state,
    ancestor ∈ active) ∧
  (∀ state ∈ active, model.isCompound state = true →
    ((model.children state).filter (active.contains ·)).length = 1) ∧
  (∀ state ∈ active, model.isParallel state = true →
    ∀ child ∈ model.children state,
      model.isReal child = true → child ∈ active) ∧
  ∃ leaf ∈ active, model.isLeaf leaf = true

structure ModeledMicrostep (model : ConfigurationModel) where
  nextActive : List Nat
  nextLegal : LegalConfiguration model nextActive

def applyMicrostep {model : ConfigurationModel}
    (microstep : ModeledMicrostep model) : List Nat :=
  microstep.nextActive

/-- Mirrors C's publication gate: a failed microstep selects the old published
    buffer, while success selects the fully validated staged buffer. -/
def commitConfiguration (succeeded : Bool)
    (published staged : List Nat) : List Nat :=
  if succeeded then staged else published

private def appendUnique (states : List Nat) (state : Nat) : List Nat :=
  if states.contains state then states else states ++ [state]

private def stableUnique (states : List Nat) : List Nat :=
  states.foldl appendUnique []

/-- Shallow history either follows its explicit default or default-enters
    below every remembered immediate child. -/
def restoreShallow (remembered defaultTarget : List Nat)
    (defaultBelow : Nat → List Nat) : List Nat :=
  if remembered.isEmpty then defaultTarget
  else remembered.flatMap defaultBelow

/-- Deep history reconstructs remembered leaves and ancestors, with C's bitset
    uniqueness represented by stable left-to-right deduplication. -/
def restoreDeep (rememberedLeaves : List Nat)
    (ancestorsIncludingSelf : Nat → List Nat) : List Nat :=
  stableUnique (rememberedLeaves.flatMap ancestorsIncludingSelf)

def ShallowRestored (remembered defaultTarget : List Nat)
    (defaultBelow : Nat → List Nat) (result : List Nat) : Prop :=
  result = restoreShallow remembered defaultTarget defaultBelow

def DeepRestored (rememberedLeaves : List Nat)
    (ancestorsIncludingSelf : Nat → List Nat) (result : List Nat) : Prop :=
  result = restoreDeep rememberedLeaves ancestorsIncludingSelf

end CMetaCFlowCalculus.CFlow.Statechart
