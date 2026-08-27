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

/- The build-time tree/document invariant. `ancestorEarlier` corresponds to
   parent-before-descendant preorder; `subtreeContiguous` excludes reopening a
   subtree after an unrelated state. The remaining fields state the tree and
   unique-document facts consumed by the ordering proof. -/
structure HierarchyPreorder (stateUniverse : List Nat)
    (ancestors : Nat → List Nat) (documentOrder : Nat → Nat) : Prop where
  ancestorIrreflexive : ∀ state ∈ stateUniverse, state ∉ ancestors state
  ancestorTransitive : ∀ descendant middle ancestor,
    descendant ∈ stateUniverse → middle ∈ stateUniverse →
    ancestor ∈ stateUniverse →
    middle ∈ ancestors descendant → ancestor ∈ ancestors middle →
    ancestor ∈ ancestors descendant
  documentUnique : ∀ left ∈ stateUniverse, ∀ right ∈ stateUniverse,
    documentOrder left = documentOrder right → left = right
  ancestorEarlier : ∀ descendant ∈ stateUniverse,
    ∀ ancestor ∈ stateUniverse,
    ancestor ∈ ancestors descendant →
    documentOrder ancestor < documentOrder descendant
  subtreeContiguous : ∀ ancestor ∈ stateUniverse,
    ∀ descendant ∈ stateUniverse, ∀ between ∈ stateUniverse,
    ancestor ∈ ancestors descendant →
    documentOrder ancestor < documentOrder between →
    documentOrder between < documentOrder descendant →
    ancestor ∈ ancestors between

def exitSortLe (stateUniverse : List Nat) (ancestors : Nat → List Nat)
    (documentOrder : Nat → Nat) (left right : Nat) : Bool :=
  if stateUniverse.contains left && stateUniverse.contains right then
    decide (left = right) || exitPrecedes ancestors documentOrder left right
  else decide (documentOrder right ≤ documentOrder left)

def entrySortLe (stateUniverse : List Nat) (ancestors : Nat → List Nat)
    (documentOrder : Nat → Nat) (left right : Nat) : Bool :=
  if stateUniverse.contains left && stateUniverse.contains right then
    decide (left = right) || entryPrecedes ancestors documentOrder left right
  else decide (documentOrder left ≤ documentOrder right)

def exitOrder (stateUniverse : List Nat) (ancestors : Nat → List Nat)
    (documentOrder : Nat → Nat) (states : List Nat) : List Nat :=
  states.mergeSort (exitSortLe stateUniverse ancestors documentOrder)

def entryOrder (stateUniverse : List Nat) (ancestors : Nat → List Nat)
    (documentOrder : Nat → Nat) (states : List Nat) : List Nat :=
  states.mergeSort (entrySortLe stateUniverse ancestors documentOrder)

def ExitOrdered (ancestors : Nat → List Nat) (documentOrder : Nat → Nat)
    (states : List Nat) : Prop :=
  states.Pairwise fun left right =>
    decide (left = right) ||
      exitPrecedes ancestors documentOrder left right = true

def EntryOrdered (ancestors : Nat → List Nat) (documentOrder : Nat → Nat)
    (states : List Nat) : Prop :=
  states.Pairwise fun left right =>
    decide (left = right) ||
      entryPrecedes ancestors documentOrder left right = true

def DocumentOrdered (documentOrder : Nat → Nat) (states : List Nat) : Prop :=
  states.Pairwise fun left right => documentOrder left ≤ documentOrder right

/-- The normalized facts consumed by C's dense configuration validator. -/
structure ConfigurationModel where
  stateUniverse : List Nat
  ancestors : Nat → List Nat
  children : Nat → List Nat
  isReal : Nat → Bool
  isCompound : Nat → Bool
  isParallel : Nat → Bool
  isLeaf : Nat → Bool
  documentOrder : Nat → Nat

/-- The pure proof boundary after C has constructed and validated a staged
    configuration. It mirrors no-pseudo, ancestry, compound exclusivity,
    parallel coverage, uniqueness, bounds, and leaf-presence checks. Callback
    execution and allocation are outside this model. -/
def LegalConfiguration (model : ConfigurationModel)
    (active : List Nat) : Prop :=
  active.Nodup ∧
  active.all model.stateUniverse.contains = true ∧
  active.all model.isReal = true ∧
  active.all (fun state =>
    (model.ancestors state).all active.contains) = true ∧
  active.all (fun state =>
    !model.isCompound state ||
      ((model.children state).filter active.contains).length == 1) = true ∧
  active.all (fun state =>
    !model.isParallel state ||
      (model.children state).all fun child =>
        !model.isReal child || active.contains child) = true ∧
  active.any model.isLeaf = true ∧
  DocumentOrdered model.documentOrder active

/-- The four state sources built by C after exit-domain selection. Targetless
    transitions contribute no target state. Defaults and history are already
    iteratively resolved to real states at this boundary. -/
structure MicrostepPlan where
  exitSet : List Nat
  directTargets : List Nat
  defaultTargets : List Nat
  historyTargets : List Nat
  deriving Repr, DecidableEq

/-- Mirrors C's staged configuration constructor: retain states outside the
    selected exit union, append resolved targets, deduplicate, then normalize
    into document order. -/
def constructNext (model : ConfigurationModel) (published : List Nat)
    (plan : MicrostepPlan) : List Nat :=
  entryOrder model.stateUniverse model.ancestors model.documentOrder
    ((published.filter fun state => !plan.exitSet.contains state) ++
      plan.directTargets ++ plan.defaultTargets ++ plan.historyTargets).eraseDups

/-- Executable counterpart of C's finite staged-configuration validation.
    This is deliberately separate from `MicrostepPlan`, so legality is checked
    after construction rather than supplied as a circular plan field. -/
def validateConfiguration (model : ConfigurationModel)
    (active : List Nat) : Bool :=
  decide active.Nodup &&
  (active.all model.stateUniverse.contains &&
  (active.all model.isReal &&
  (active.all (fun state => (model.ancestors state).all active.contains) &&
  (active.all (fun state =>
    !model.isCompound state ||
      ((model.children state).filter active.contains).length == 1) &&
  (active.all (fun state =>
    !model.isParallel state ||
      (model.children state).all fun child =>
        !model.isReal child || active.contains child) &&
  (active.any model.isLeaf &&
  decide (active.Pairwise fun left right =>
    model.documentOrder left ≤ model.documentOrder right)))))))

/-- Mirrors C's publication gate: a failed microstep selects the old published
    buffer, while success selects the fully validated staged buffer. -/
def commitConfiguration (succeeded : Bool)
    (published staged : List Nat) : List Nat :=
  if succeeded then staged else published

/-- Shallow history either follows its explicit default or default-enters
    below every remembered immediate child. -/
def restoreShallow (remembered defaultTarget : List Nat)
    (defaultBelow : Nat → List Nat) : List Nat :=
  match remembered with
  | [] => defaultTarget
  | _ => remembered.flatMap fun child => child :: defaultBelow child

/-- Deep history reconstructs remembered leaves and ancestors, with C's bitset
    uniqueness represented by stable left-to-right deduplication. -/
def restoreDeep (rememberedLeaves : List Nat)
    (ancestorsIncludingSelf : Nat → List Nat) : List Nat :=
  (rememberedLeaves.flatMap ancestorsIncludingSelf).eraseDups

def ShallowRestored (remembered defaultTarget : List Nat)
    (defaultBelow : Nat → List Nat) (result : List Nat) : Prop :=
  match remembered with
  | [] => ∀ state ∈ defaultTarget, state ∈ result
  | _ =>
      (∀ child ∈ remembered, child ∈ result) ∧
      (∀ child ∈ remembered, ∀ state ∈ defaultBelow child,
        state ∈ result)

def DeepRestored (rememberedLeaves : List Nat)
    (ancestorsIncludingSelf : Nat → List Nat) (result : List Nat) : Prop :=
  ∀ leaf ∈ rememberedLeaves, ∀ state ∈ ancestorsIncludingSelf leaf,
    state ∈ result

end CMetaCFlowCalculus.CFlow.Statechart
