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

end CMetaCFlowCalculus.CFlow.Statechart
