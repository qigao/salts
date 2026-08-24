import CMetaCFlowCalculus.Proofs.Temporal

namespace CMetaCFlowCalculus.Tests.Temporal

open CMetaCFlowCalculus.CFlow.Temporal

def initial : State Nat :=
  { pending := some 1, scratch := none, deadline := some 10,
    terminal := .open }

example : (debounceReplace initial 2).pending = some 2 := rfl
example : retainedCount (debounceReplace initial 2) = 1 := by native_decide
example : timeoutTerminal .timer = .error := rfl
example : delayDeadline 7 5 = 12 := rfl

end CMetaCFlowCalculus.Tests.Temporal
