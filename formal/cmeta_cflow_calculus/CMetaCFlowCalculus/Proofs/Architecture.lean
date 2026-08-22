import CMetaCFlowCalculus.CFlow.Architecture

namespace CMetaCFlowCalculus.CFlow

open CMetaCFlowCalculus.CMeta

/-- Typed Surface lowering establishes a valid Graph and preserves observations. -/
theorem surface_soundness {Γ : Env} {inputTy outputTy : Ty} {mode : FlowMode}
    (architecture : Architecture Γ inputTy outputTy mode)
    (surface : Surface Γ inputTy outputTy mode) :
    (architecture.graph surface).wellTyped ∧
      (architecture.graph surface).valid ∧
      ObsEq surface.semantics (architecture.graph surface).semantics := by
  exact ⟨architecture.lowering.preservesTyping surface,
    architecture.lowering.preservesValidity surface,
    architecture.lowering.preservesObservation surface⟩

/-- Normalization preserves Graph typing, validity, and observations. -/
theorem normalize_soundness {Γ : Env} {inputTy outputTy : Ty} {mode : FlowMode}
    (architecture : Architecture Γ inputTy outputTy mode)
    (surface : Surface Γ inputTy outputTy mode) :
    (architecture.normalizedGraph surface).wellTyped ∧
      (architecture.normalizedGraph surface).valid ∧
      ObsEq (architecture.graph surface).semantics
        (architecture.normalizedGraph surface).semantics := by
  have graphSound := surface_soundness architecture surface
  exact ⟨architecture.normalization.preservesTyping _ graphSound.1,
    architecture.normalization.preservesValidity _ graphSound.2.1,
    architecture.normalization.preservesObservation _ graphSound.2.1⟩

/-- Optimization preserves normalized Graph typing, validity, and observations. -/
theorem optimize_soundness {Γ : Env} {inputTy outputTy : Ty} {mode : FlowMode}
    (architecture : Architecture Γ inputTy outputTy mode)
    (surface : Surface Γ inputTy outputTy mode) :
    (architecture.optimizedGraph surface).wellTyped ∧
      (architecture.optimizedGraph surface).valid ∧
      ObsEq (architecture.normalizedGraph surface).semantics
        (architecture.optimizedGraph surface).semantics := by
  have normalizedSound := normalize_soundness architecture surface
  exact ⟨architecture.optimization.preservesTyping _ normalizedSound.1,
    architecture.optimization.preservesValidity _ normalizedSound.2.1,
    architecture.optimization.preservesObservation _ normalizedSound.2.1⟩

/-- Plan compilation establishes well-formedness and preserves observations. -/
theorem plan_compilation_soundness {Γ : Env} {inputTy outputTy : Ty}
    {mode : FlowMode} (architecture : Architecture Γ inputTy outputTy mode)
    (surface : Surface Γ inputTy outputTy mode) :
    (architecture.plan surface).wellFormed ∧
      ObsEq (architecture.optimizedGraph surface).semantics
        (architecture.plan surface).semantics := by
  have optimizedSound := optimize_soundness architecture surface
  exact ⟨architecture.compilation.preservesWellFormed _ optimizedSound.2.1,
    architecture.compilation.preservesObservation _ optimizedSound.2.1⟩

/-- A supported Kernel executes a well-formed Plan with the same observations. -/
theorem kernel_execution_soundness {Γ : Env} {inputTy outputTy : Ty}
    {mode : FlowMode} (architecture : Architecture Γ inputTy outputTy mode)
    (K : KernelCapabilities) (surface : Surface Γ inputTy outputTy mode)
    (capabilities : Capabilities K (architecture.plan surface).requirements) :
    ExecutionObsEqAt K (architecture.plan surface).semantics
      (architecture.execution K surface).semantics := by
  exact architecture.kernel.preservesObservation K _
    (plan_compilation_soundness architecture surface).1 capabilities

/-- The complete certified architecture preserves the Surface trace at `K`. -/
theorem architecture_observation_preservation {Γ : Env}
    {inputTy outputTy : Ty} {mode : FlowMode}
    (architecture : Architecture Γ inputTy outputTy mode)
    (K : KernelCapabilities) (surface : Surface Γ inputTy outputTy mode)
    (capabilities : Capabilities K (architecture.plan surface).requirements) :
    ExecutionObsEqAt K surface.semantics
      (architecture.execution K surface).semantics := by
  intro input
  have surfaceGraph :=
    (surface_soundness architecture surface).2.2 K input
  have graphNormalized :=
    (normalize_soundness architecture surface).2.2 K input
  have normalizedOptimized :=
    (optimize_soundness architecture surface).2.2 K input
  have optimizedPlan :=
    (plan_compilation_soundness architecture surface).2 K input
  have planExecution :=
    kernel_execution_soundness architecture K surface capabilities input
  exact Eq.trans surfaceGraph
    (Eq.trans graphNormalized
      (Eq.trans normalizedOptimized (Eq.trans optimizedPlan planExecution)))

end CMetaCFlowCalculus.CFlow
