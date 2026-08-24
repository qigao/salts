import CMetaCFlowCalculus.Proofs.MachineHierarchy

namespace CMetaCFlowCalculus.Tests.MachineHierarchy

open CMetaCFlowCalculus.CFlow.MachineHierarchy

def leafCandidate : Candidate :=
  { sourceLeaf := 3, event := 10, guard := 20, action := 30,
    targetLeaf := 4, declarationDepth := 2, declarationPriority := 90 }

def parentCandidate : Candidate :=
  { sourceLeaf := 3, event := 10, guard := 21, action := 31,
    targetLeaf := 5, declarationDepth := 1, declarationPriority := 1 }

example : firstEnabled (fun guard => guard = 20)
    [leafCandidate, parentCandidate] = some leafCandidate := by native_decide

example : selectFlat (fun guard => guard = 21)
    (enumerate 0 [leafCandidate, parentCandidate]) = some parentCandidate := by
  native_decide

example : exitPath [3, 2, 1] 1 = [3, 2] := by native_decide
example : entryPath [5, 1] 1 = [5] := by native_decide

end CMetaCFlowCalculus.Tests.MachineHierarchy
