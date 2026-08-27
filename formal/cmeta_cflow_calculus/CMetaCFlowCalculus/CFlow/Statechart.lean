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

def selectFrom : List Candidate → List Candidate → List Candidate
  | selected, [] => selected
  | selected, candidate :: remaining =>
      selectFrom (insertCandidate selected candidate) remaining

/-- Active-leaf candidate order is consumed once, from earliest to latest. -/
def select (candidates : List Candidate) : List Candidate :=
  selectFrom [] candidates

def ConflictFree (candidates : List Candidate) : Prop :=
  candidates.Pairwise fun left right => conflicts left right = false

end CMetaCFlowCalculus.CFlow.Statechart
