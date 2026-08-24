import CMetaCFlowCalculus.CFlow.Machine

namespace CMetaCFlowCalculus.CFlow.MachineHierarchy

open CMetaCFlowCalculus.CFlow.Machine

/-- One transition after ancestry expansion but before dense flat priorities. -/
structure Candidate where
  sourceLeaf : Nat
  event : Nat
  guard : Nat
  action : Nat
  targetLeaf : Nat
  declarationDepth : Nat
  declarationPriority : Nat
  deriving Repr, DecidableEq

/-- Candidates are supplied in leaf-first, then declaration-priority order. -/
def firstEnabled (guards : Nat → Bool) : List Candidate → Option Candidate
  | [] => none
  | candidate :: remaining =>
      if guards candidate.guard then some candidate
      else firstEnabled guards remaining

structure FlatRow where
  candidate : Candidate
  priority : Nat
  deriving Repr, DecidableEq

/-- Dense priorities preserve the already-normalized bubbling order. -/
def enumerate : Nat → List Candidate → List FlatRow
  | _, [] => []
  | priority, candidate :: remaining =>
      { candidate := candidate, priority := priority } ::
        enumerate (priority + 1) remaining

def selectFlat (guards : Nat → Bool) : List FlatRow → Option Candidate
  | [] => none
  | row :: remaining =>
      if guards row.candidate.guard then some row.candidate
      else selectFlat guards remaining

/-- A state-to-root path includes the state and root. -/
def exitPath (sourceToRoot : List Nat) (lca : Nat) : List Nat :=
  sourceToRoot.takeWhile (· != lca)

/-- Reversing target-to-root yields LCA-to-leaf entry order. -/
def entryPath (targetToRoot : List Nat) (lca : Nat) : List Nat :=
  (targetToRoot.takeWhile (· != lca)).reverse

/-- Composite target descent stores the resolved leaf kind in flat IR. -/
def flattenedTargetKind
    (resolvedLeaf : _root_.CMetaCFlowCalculus.CFlow.Machine.StateDecl) :
    _root_.CMetaCFlowCalculus.CFlow.MachineStateKind :=
  resolvedLeaf.kind

end CMetaCFlowCalculus.CFlow.MachineHierarchy
