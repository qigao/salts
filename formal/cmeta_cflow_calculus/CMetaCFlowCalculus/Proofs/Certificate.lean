import CMetaCFlowCalculus.CFlow.Certificate
import CMetaCFlowCalculus.Proofs.Rewrite

namespace CMetaCFlowCalculus.CFlow

open CMetaCFlowCalculus.CMeta

theorem certificateRowsRefine_refl {inputTy outputTy : Ty}
    (rows : List CertificateRow)
    (semantics : Semantics inputTy outputTy) :
    CertificateRowsRefine rows semantics semantics := by
  induction rows with
  | nil => exact .nil semantics
  | cons row rows ih =>
      exact .cons row (by intro K input; rfl) ih

/-- Exact row agreement composes to whole-Plan observational equivalence. -/
theorem certificateRowsRefine_preserves_observation {inputTy outputTy : Ty}
    {rows : List CertificateRow}
    {before after : Semantics inputTy outputTy}
    (refinement : CertificateRowsRefine rows before after) :
    ObsEq before after := by
  induction refinement with
  | nil semantics =>
      intro K input
      rfl
  | cons row head tail ih =>
      intro K input
      exact Eq.trans (head K input) (ih K input)

/-- A valid sequential certificate refines its normalized Graph denotation. -/
theorem certificate_preserves_observation {inputTy outputTy : Ty}
    {certificate : PlanCertificate}
    {graphVersion graphFingerprint : Nat}
    {graphRows planRows : List CertificateRow}
    {graphSemantics planSemantics : Semantics inputTy outputTy}
    (valid : CertificateValid certificate graphVersion graphFingerprint
      graphRows planRows graphSemantics planSemantics)
    (_sequential : certificate.path = .sequential) :
    ObsEq graphSemantics planSemantics :=
  certificateRowsRefine_preserves_observation valid.refinement

/-- Ordered chunk reduction is sequential left reduction with a different
parenthesization. No commutativity or chunk permutation premise is present. -/
theorem ordered_chunks_reduce_eq {Γ : Env} {K : KernelCapabilities} {ty : Ty}
    (premises : ParallelReducePremises Γ K ty)
    (initial : Value ty) (chunks : ContiguousNonemptyChunks (Value ty)) :
    chunks.source.foldl premises.meaning.apply initial =
      premises.meaning.apply initial
        (chunks.reduction.eval premises.meaning.apply) := by
  rw [← chunks.preservesEncounterOrder]
  simpa [ReductionForest.flatten, ReductionForest.evalFrom] using
    (r10_reduce_reassociation premises.meaning premises.pure premises.total
      premises.associative premises.associativeLaw initial
      (.tree chunks.reduction))

/-- The certificate establishes Graph-to-Plan observation equality; R11 then
changes only the physical execution form from Reduce to Parallel Reduce. -/
theorem parallel_certificate_preserves_observation
    {Γ : Env} {K : KernelCapabilities} {ty inputTy outputTy : Ty}
    {certificate : PlanCertificate}
    {graphVersion graphFingerprint : Nat}
    {graphRows planRows : List CertificateRow}
    {graphSemantics planSemantics : Semantics inputTy outputTy}
    (valid : CertificateValid certificate graphVersion graphFingerprint
      graphRows planRows graphSemantics planSemantics)
    (_parallel : certificate.path = .orderedParallelReduce)
    (premises : ParallelReducePremises Γ K ty)
    (result : StreamResult (Value ty)) :
    ObsEq graphSemantics planSemantics ∧
      ({ form := .reduce, result := result } : ExecutionVariant _).result =
        ({ form := .parallelReduce, result := result } :
          ExecutionVariant _).result := by
  have refinement : ExecutionRefines Γ K
      ({ form := .reduce, result := result } : ExecutionVariant _)
      ({ form := .parallelReduce, result := result } : ExecutionVariant _) :=
    .r11 premises result
  exact ⟨certificateRowsRefine_preserves_observation valid.refinement,
    execution_refinement_preserves_semantics refinement⟩

end CMetaCFlowCalculus.CFlow
