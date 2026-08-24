import CMetaCFlowCalculus.Proofs.IOBoundedMpsc

open CMetaCFlowCalculus.IO.BoundedMpsc

namespace CMetaCFlowCalculus.Tests.IOBoundedMpsc

def empty : State Nat :=
  { capacity := 2, queue := [], terminal := .open }

def one : State Nat :=
  { empty with queue := [11] }

def full : State Nat :=
  { empty with queue := [10, 11] }

theorem emptyValid : empty.Valid := by
  simp [State.Valid, empty]

example : (tryPublish empty 11).1 = .accepted := by native_decide
example : (tryPublish full 12).1 = .full := by native_decide
example : (tryConsume one).1 = .item 11 := by native_decide
example : (tryPublish (close empty).2 11).1 = .closed := by native_decide

example : (tryPublish empty 11).2.Valid :=
  tryPublish_preserves_valid empty 11 emptyValid

example : (tryConsume one).2.Valid :=
  tryConsume_preserves_valid one (by simp [State.Valid, one, empty])

example : (tryPublish empty 11).2.queue = [11] := by native_decide
example : (tryConsume one).2.queue = [] := by native_decide
example : (close one).2.queue = one.queue := close_preserves_queue one

end CMetaCFlowCalculus.Tests.IOBoundedMpsc
