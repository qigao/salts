module
public import CMeta.Optimize
import all CMeta.Optimize
import all CMeta.Flow
import all CMeta.Callable

/-!
# High-level lowering preservation

This module models the type-relevant part of `cflow/src/lower.c` for ZIP.
The implementation lowers ZIP into a structured relation with two subgraphs
that share one root source. Their outputs may differ; a binary value callable
combines them into the final output.
-/

namespace CMeta

/-- Surface ZIP typing: two computations share source `A`, produce possibly
    different outputs, and are combined by `L × R → O`. -/
public structure SurfaceZip (A O : CType) where
  leftOutput : CType
  rightOutput : CType
  left : Pipeline A leftOutput
  right : Pipeline A rightOutput
  combine : Callable [leftOutput, rightOutput] O

/-- Runtime/type-erased descriptor produced by ZIP lowering. -/
public structure ErasedInvokeRelation where
  left : List (Operator × Signature)
  right : List (Operator × Signature)
  combine : Signature
  deriving Repr, DecidableEq

/-- Dynamic validator for the type-relevant INVOKE relation rule. -/
public def checkInvokeRelation (input : CType)
    (rel : ErasedInvokeRelation) : Option CType :=
  match checkPipeline input rel.left, checkPipeline input rel.right with
  | some leftOut, some rightOut =>
      match rel.combine with
      | .binary leftParam rightParam output =>
          if leftParam = leftOut ∧ rightParam = rightOut
          then some output
          else none
      | _ => none
  | _, _ => none

namespace SurfaceZip

/-- Erasure performed by normalization: keep both branch programs and the
    exact binary callable signature. -/
public def lower {A O : CType} (zip : SurfaceZip A O) : ErasedInvokeRelation :=
  ⟨zip.left.steps, zip.right.steps, zip.combine.binaryBackendSignature⟩

/-- ZIP lowering preserves the statically known output type. -/
theorem lowering_preserves_type {A O : CType} (zip : SurfaceZip A O) :
    checkInvokeRelation A zip.lower = some O := by
  simp [SurfaceZip.lower, checkInvokeRelation,
    Pipeline.check_steps, Callable.binaryBackendSignature]

/-- Progress form: a well-typed surface ZIP cannot become type-stuck after
    lowering to RELATION(INVOKE). -/
theorem lowering_progress {A O : CType} (zip : SurfaceZip A O) :
    ∃ output, checkInvokeRelation A zip.lower = some output :=
  ⟨O, zip.lowering_preserves_type⟩

/-- Preservation also gives uniqueness of the dynamically recovered output. -/
theorem lowering_output_unique {A O O' : CType} (zip : SurfaceZip A O)
    (h : checkInvokeRelation A zip.lower = some O') : O = O' := by
  have hs : (some O : Option CType) = some O' :=
    zip.lowering_preserves_type.symm.trans h
  exact Option.some.inj hs

end SurfaceZip

end CMeta
