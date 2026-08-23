import CMetaCFlowCalculus.CFlow.Observation
import CMetaCFlowCalculus.CFlow.Syntax

namespace CMetaCFlowCalculus.CFlow

open CMetaCFlowCalculus.CMeta

/-- A typed Stream surface term paired with its trusted denotation. -/
structure Surface (Γ : Env) (inputTy outputTy : Ty) (mode : FlowMode) where
  term : Flow Γ inputTy outputTy mode
  semantics : Semantics inputTy outputTy

/-- Lowered Graph IR. Typing and structural validity are separate obligations. -/
structure Graph (_Γ : Env) (inputTy outputTy : Ty) (_mode : FlowMode) where
  semantics : Semantics inputTy outputTy
  wellTyped : Prop
  valid : Prop

/-- Graph after canonical normalization, kept nominally distinct from Graph IR. -/
structure NormalizedGraph (_Γ : Env) (inputTy outputTy : Ty)
    (_mode : FlowMode) where
  semantics : Semantics inputTy outputTy
  wellTyped : Prop
  valid : Prop

/-- Graph after sound optimization, kept nominally distinct from normalized IR. -/
structure OptimizedGraph (_Γ : Env) (inputTy outputTy : Ty)
    (_mode : FlowMode) where
  semantics : Semantics inputTy outputTy
  wellTyped : Prop
  valid : Prop

/-- Compiled Plan with explicit Kernel capability requirements. -/
structure Plan (_Γ : Env) (inputTy outputTy : Ty) (_mode : FlowMode) where
  semantics : Semantics inputTy outputTy
  wellFormed : Prop
  requirements : CapabilityRequirements

/-- A concrete execution after the Kernel environment has been selected. -/
structure KernelExecution (_Γ : Env) (inputTy outputTy : Ty)
    (_mode : FlowMode) where
  semantics : ExecutionSemantics inputTy outputTy

/-- Trusted obligation implemented later by Surface-to-Graph conformance. -/
structure LoweringContract (Γ : Env) (inputTy outputTy : Ty)
    (mode : FlowMode) where
  lower : Surface Γ inputTy outputTy mode → Graph Γ inputTy outputTy mode
  preservesTyping : ∀ surface, (lower surface).wellTyped
  preservesValidity : ∀ surface, (lower surface).valid
  preservesObservation : ∀ surface,
    ObsEq surface.semantics (lower surface).semantics

/-- Trusted obligation implemented later by Graph normalization conformance. -/
structure NormalizeContract (Γ : Env) (inputTy outputTy : Ty)
    (mode : FlowMode) where
  normalize : Graph Γ inputTy outputTy mode →
    NormalizedGraph Γ inputTy outputTy mode
  preservesTyping : ∀ graph, graph.wellTyped → (normalize graph).wellTyped
  preservesValidity : ∀ graph, graph.valid → (normalize graph).valid
  preservesObservation : ∀ graph, graph.valid →
    ObsEq graph.semantics (normalize graph).semantics

/-- Optimizer soundness contract; Phase D supplies accepted rewrite theorems. -/
structure OptimizeContract (Γ : Env) (inputTy outputTy : Ty)
    (mode : FlowMode) where
  optimize : NormalizedGraph Γ inputTy outputTy mode →
    OptimizedGraph Γ inputTy outputTy mode
  preservesTyping : ∀ graph, graph.wellTyped → (optimize graph).wellTyped
  preservesValidity : ∀ graph, graph.valid → (optimize graph).valid
  preservesObservation : ∀ graph, graph.valid →
    ObsEq graph.semantics (optimize graph).semantics

/-- Trusted obligation implemented later by Graph-to-Plan conformance. -/
structure CompileContract (Γ : Env) (inputTy outputTy : Ty)
    (mode : FlowMode) where
  compile : OptimizedGraph Γ inputTy outputTy mode →
    Plan Γ inputTy outputTy mode
  preservesWellFormed : ∀ graph, graph.valid → (compile graph).wellFormed
  preservesObservation : ∀ graph, graph.valid →
    ObsEq graph.semantics (compile graph).semantics

/-- Kernel execution is sound only for well-formed, capability-supported Plans. -/
structure KernelContract (Γ : Env) (inputTy outputTy : Ty)
    (mode : FlowMode) where
  execute : KernelCapabilities → Plan Γ inputTy outputTy mode →
    KernelExecution Γ inputTy outputTy mode
  preservesObservation : ∀ K plan,
    plan.wellFormed → Capabilities K plan.requirements →
      ExecutionObsEqAt K plan.semantics (execute K plan).semantics

/-- All contracts required to justify one typed architecture pipeline. -/
structure Architecture (Γ : Env) (inputTy outputTy : Ty)
    (mode : FlowMode) where
  lowering : LoweringContract Γ inputTy outputTy mode
  normalization : NormalizeContract Γ inputTy outputTy mode
  optimization : OptimizeContract Γ inputTy outputTy mode
  compilation : CompileContract Γ inputTy outputTy mode
  kernel : KernelContract Γ inputTy outputTy mode

namespace Architecture

def graph {Γ : Env} {inputTy outputTy : Ty} {mode : FlowMode}
    (architecture : Architecture Γ inputTy outputTy mode)
    (surface : Surface Γ inputTy outputTy mode) :
    Graph Γ inputTy outputTy mode :=
  architecture.lowering.lower surface

def normalizedGraph {Γ : Env} {inputTy outputTy : Ty} {mode : FlowMode}
    (architecture : Architecture Γ inputTy outputTy mode)
    (surface : Surface Γ inputTy outputTy mode) :
    NormalizedGraph Γ inputTy outputTy mode :=
  architecture.normalization.normalize (architecture.graph surface)

def optimizedGraph {Γ : Env} {inputTy outputTy : Ty} {mode : FlowMode}
    (architecture : Architecture Γ inputTy outputTy mode)
    (surface : Surface Γ inputTy outputTy mode) :
    OptimizedGraph Γ inputTy outputTy mode :=
  architecture.optimization.optimize (architecture.normalizedGraph surface)

def plan {Γ : Env} {inputTy outputTy : Ty} {mode : FlowMode}
    (architecture : Architecture Γ inputTy outputTy mode)
    (surface : Surface Γ inputTy outputTy mode) :
    Plan Γ inputTy outputTy mode :=
  architecture.compilation.compile (architecture.optimizedGraph surface)

def execution {Γ : Env} {inputTy outputTy : Ty} {mode : FlowMode}
    (architecture : Architecture Γ inputTy outputTy mode)
    (K : KernelCapabilities) (surface : Surface Γ inputTy outputTy mode) :
    KernelExecution Γ inputTy outputTy mode :=
  architecture.kernel.execute K (architecture.plan surface)

end Architecture

end CMetaCFlowCalculus.CFlow
