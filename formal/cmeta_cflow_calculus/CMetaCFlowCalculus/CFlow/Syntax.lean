import CMetaCFlowCalculus.CMeta.Environment

namespace CMetaCFlowCalculus.CFlow

open CMetaCFlowCalculus.CMeta

/-- Whether a term still produces stream elements or has reached a terminal. -/
inductive FlowMode where
  | stream
  | terminal
  deriving Repr, DecidableEq

/--
The v1 Flow grammar indexed by `Γ`, input type, output type, and mode.
Only stream terms are accepted by intermediate operators, so a terminal term
cannot be extended by construction.
-/
inductive Flow (Γ : Env) : Ty → Ty → FlowMode → Type where
  | source {element : Ty}
      (source : SourceDecl Γ element) :
      Flow Γ element element .stream
  | map {input middle output : Ty}
      (callable : UnaryCallable Γ middle output)
      (upstream : Flow Γ input middle .stream) :
      Flow Γ input output .stream
  | filter {input element : Ty}
      (predicate : UnaryCallable Γ element .bool)
      (upstream : Flow Γ input element .stream) :
      Flow Γ input element .stream
  | flatMap {input middle output : Ty}
      (callable : UnaryCallable Γ middle (.range output))
      (upstream : Flow Γ input middle .stream) :
      Flow Γ input output .stream
  | limit {input output : Ty}
      (count : Nat)
      (upstream : Flow Γ input output .stream) :
      Flow Γ input output .stream
  | skip {input output : Ty}
      (count : Nat)
      (upstream : Flow Γ input output .stream) :
      Flow Γ input output .stream
  | reduce {input element : Ty}
      (reducer : BinaryCallable Γ element element element)
      (upstream : Flow Γ input element .stream) :
      Flow Γ input element .stream
  | collect {input element result : Ty}
      (collector : CollectorDecl Γ element result)
      (upstream : Flow Γ input element .stream) :
      Flow Γ input result .terminal

/-- Constructive evidence that every terminal v1 term is a `Collect`. -/
structure TerminalWitness (Γ : Env) (input result : Ty) where
  element : Ty
  collector : CollectorDecl Γ element result
  upstream : Flow Γ input element .stream

def terminalWitness {Γ : Env} {input result : Ty} :
    Flow Γ input result .terminal → TerminalWitness Γ input result
  | .collect collector upstream =>
      { element := _, collector := collector, upstream := upstream }

end CMetaCFlowCalculus.CFlow
