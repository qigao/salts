import CMetaCFlowCalculus.CFlow.Observation
import CMetaCFlowCalculus.CFlow.Architecture
import CMetaCFlowCalculus.Proofs.Architecture

open CMetaCFlowCalculus.CMeta
open CMetaCFlowCalculus.CFlow

namespace CMetaCFlowCalculus.Tests.PhaseC

def scalarTy : Ty := .named "Scalar"

def kernelCapabilities : KernelCapabilities := fun capability =>
  capability = .syncExecution ∨ capability = .batchExecution

def planRequirements : CapabilityRequirements := fun capability =>
  capability = .syncExecution ∨ capability = .batchExecution

example : Capabilities kernelCapabilities planRequirements := by
  intro capability required
  exact required

def scalarValue : Value scalarTy where
  token := 41

def sampleTrace : Trace scalarTy where
  events := [.value scalarValue, .effect .pure, .done]
  ownershipSafe := true

def sampleSemantics : Semantics scalarTy scalarTy := fun _ _ => sampleTrace

example (K : KernelCapabilities) :
    ObsEqAt K sampleSemantics sampleSemantics := by
  intro input
  rfl

def permissiveEnv : Env where
  hasCapability := fun _ _ => True
  declaresUnary := fun _ _ _ _ _ => True
  declaresBinary := fun _ _ _ _ _ _ => True
  declaresSource := fun _ _ => True
  declaresCollector := fun _ _ _ => True

def scalarSource : SourceDecl permissiveEnv scalarTy where
  name := "scalars"
  declared := trivial

def scalarCollector : CollectorDecl permissiveEnv scalarTy scalarTy where
  name := "scalar"
  declared := trivial

def scalarFlow : Flow permissiveEnv scalarTy scalarTy .terminal :=
  .collect scalarCollector (.source scalarSource)

def surface : Surface permissiveEnv scalarTy scalarTy .terminal where
  term := scalarFlow
  semantics := sampleSemantics

def lowerSurface
    (source : Surface permissiveEnv scalarTy scalarTy .terminal) :
    Graph permissiveEnv scalarTy scalarTy .terminal where
  semantics := source.semantics
  wellTyped := True
  valid := True

def lowering : LoweringContract permissiveEnv scalarTy scalarTy .terminal where
  lower := lowerSurface
  preservesTyping := by intro; trivial
  preservesValidity := by intro; trivial
  preservesObservation := by intro _ _ _; rfl

def normalizeGraph
    (graph : Graph permissiveEnv scalarTy scalarTy .terminal) :
    NormalizedGraph permissiveEnv scalarTy scalarTy .terminal where
  semantics := graph.semantics
  wellTyped := graph.wellTyped
  valid := graph.valid

def normalization :
    NormalizeContract permissiveEnv scalarTy scalarTy .terminal where
  normalize := normalizeGraph
  preservesTyping := by intro _ typed; exact typed
  preservesValidity := by intro _ valid; exact valid
  preservesObservation := by intro _ _ K input; rfl

def optimizeGraph
    (graph : NormalizedGraph permissiveEnv scalarTy scalarTy .terminal) :
    OptimizedGraph permissiveEnv scalarTy scalarTy .terminal where
  semantics := graph.semantics
  wellTyped := graph.wellTyped
  valid := graph.valid

def optimization :
    OptimizeContract permissiveEnv scalarTy scalarTy .terminal where
  optimize := optimizeGraph
  preservesTyping := by intro _ typed; exact typed
  preservesValidity := by intro _ valid; exact valid
  preservesObservation := by intro _ _ K input; rfl

def compileGraph
    (graph : OptimizedGraph permissiveEnv scalarTy scalarTy .terminal) :
    Plan permissiveEnv scalarTy scalarTy .terminal where
  semantics := graph.semantics
  wellFormed := graph.valid
  requirements := planRequirements

def compilation : CompileContract permissiveEnv scalarTy scalarTy .terminal where
  compile := compileGraph
  preservesWellFormed := by intro _ valid; exact valid
  preservesObservation := by intro _ _ K input; rfl

def executePlan (K : KernelCapabilities)
    (plan : Plan permissiveEnv scalarTy scalarTy .terminal) :
    KernelExecution permissiveEnv scalarTy scalarTy .terminal where
  semantics := plan.semantics K

def kernel : KernelContract permissiveEnv scalarTy scalarTy .terminal where
  execute := executePlan
  preservesObservation := by
    intro _ _ _ _ input
    rfl

def architecture : Architecture permissiveEnv scalarTy scalarTy .terminal where
  lowering := lowering
  normalization := normalization
  optimization := optimization
  compilation := compilation
  kernel := kernel

example : (architecture.graph surface).wellTyped :=
  (surface_soundness architecture surface).1

example : (architecture.graph surface).valid :=
  (surface_soundness architecture surface).2.1

example : (architecture.normalizedGraph surface).valid :=
  (normalize_soundness architecture surface).2.1

example : (architecture.optimizedGraph surface).valid :=
  (optimize_soundness architecture surface).2.1

example : (architecture.plan surface).wellFormed :=
  (plan_compilation_soundness architecture surface).1

example : ExecutionObsEqAt kernelCapabilities surface.semantics
    (architecture.execution kernelCapabilities surface).semantics :=
  architecture_observation_preservation architecture kernelCapabilities surface
    (by exact fun _ required => required)

example (input : Input scalarTy) :
    ((architecture.execution kernelCapabilities surface).semantics
      input).ownershipSafe = true := by
  have preserved :=
    architecture_observation_preservation architecture kernelCapabilities surface
      (by exact fun _ required => required) input
  have safe := congrArg Trace.ownershipSafe preserved
  exact safe.symm

def syncOnlyCapabilities : KernelCapabilities := fun capability =>
  capability = .syncExecution

example : ¬Capabilities syncOnlyCapabilities planRequirements := by
  intro supported
  have batch := supported .batchExecution (Or.inr rfl)
  simp [syncOnlyCapabilities] at batch

example : ¬Capabilities syncOnlyCapabilities
    (architecture.plan surface).requirements := by
  simpa [Architecture.plan, architecture, compilation, compileGraph] using
    (show ¬Capabilities syncOnlyCapabilities planRequirements from by
      intro supported
      have batch := supported .batchExecution (Or.inr rfl)
      simp [syncOnlyCapabilities] at batch)

end CMetaCFlowCalculus.Tests.PhaseC
