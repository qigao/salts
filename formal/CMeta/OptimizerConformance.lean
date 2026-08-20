import CMeta.StructuredPolicyConformance
import CMeta.OptimizerGeneratedC

/-!
# Real C optimizer ↔ property rewrite conformance

This module ties the actual duplicate-MAP rewrite in `cflow/src/opt.c` to the
`IdempotentEndomap` law already proved in `Optimize.lean`.

The two generated witnesses intentionally use the same identity value semantics
but different contracts.  The idempotent callable authorizes duplicate
elimination; the ordinary value callable does not.  Thus the conformance gate
checks that optimization follows declared semantic properties rather than
attempting to infer extensional function laws from C code.
-/

namespace CMeta

private def identityI : Callable1 CType.int CType.int :=
  ⟨fun (x : Int) => x⟩

private def identityEndomap : IdempotentEndomap CType.int :=
  { fn := identityI,
    law := by intro x; rfl }

/-- The semantic law that licenses the real duplicate-elimination witness. -/
theorem OptimizerConformance.identity_duplicate_elimination_sound
    (x : CType.int.denote) :
    identityEndomap.fn.run (identityEndomap.fn.run x) =
      identityEndomap.fn.run x := by
  exact duplicate_idempotent_elimination_sound identityEndomap x

private def runIdentityOnce (xs : List Int) : List Int :=
  xs.map identityI.run

private def runIdentityTwice (xs : List Int) : List Int :=
  xs.map (fun x => identityI.run (identityI.run x))

/-- The list-level witness semantics follows immediately from the endomap law. -/
theorem OptimizerConformance.identity_lists_equal (xs : List Int) :
    runIdentityTwice xs = runIdentityOnce xs := by
  simp [runIdentityTwice, runIdentityOnce, identityI]

private structure OptimizerModelResult where
  inputType : String
  outputType : String
  nodesBefore : Nat
  nodesAfter : Nat
  mapNodesFused : Nat
  fnChainCount : Nat
  idempotentEliminated : Nat
  propertyBlocked : Nat
  before : List Int
  after : List Int

private def optimizerModel (name : String) (input : List Int) :
    Option OptimizerModelResult :=
  match name with
  | "duplicate_idempotent_identity_i" =>
      some ⟨"I", "I", 3, 2, 1, 1, 1, 0,
        runIdentityTwice input, runIdentityOnce input⟩
  | "duplicate_plain_identity_i" =>
      some ⟨"I", "I", 3, 2, 1, 2, 0, 1,
        runIdentityTwice input, runIdentityTwice input⟩
  | _ => none

private def optimizerWitnessConforms
    (w : COptimizerGenerated.OptimizerWitness) : Bool :=
  match optimizerModel w.name w.input with
  | some expected =>
      w.inputType == expected.inputType &&
      w.outputType == expected.outputType &&
      w.nodesBefore == expected.nodesBefore &&
      w.nodesAfter == expected.nodesAfter &&
      w.mapNodesFused == expected.mapNodesFused &&
      w.fnChainCount == expected.fnChainCount &&
      w.idempotentEliminated == expected.idempotentEliminated &&
      w.propertyBlocked == expected.propertyBlocked &&
      w.before == expected.before &&
      w.after == expected.after
  | none => false

/-- Both authorization and non-authorization cases remain present in CI. -/
theorem OptimizerConformance.coverage :
    COptimizerGenerated.optimizerWitnesses.map (fun w => w.name) =
      ["duplicate_idempotent_identity_i", "duplicate_plain_identity_i"] := by
  native_decide

/-- The real optimizer fuses both duplicate MAP pairs, eliminates only the pair
    carrying the idempotence contract, preserves the unlicensed duplicate
    callback, and preserves actual runtime values in both cases. -/
theorem OptimizerConformance.runtime_and_property_rewrite_match :
    COptimizerGenerated.optimizerWitnesses.all optimizerWitnessConforms = true := by
  native_decide

/-- Public optimizer implementation gate. -/
theorem CImplementationConformance.optimizer_property_rewrites :
    COptimizerGenerated.optimizerWitnesses.all optimizerWitnessConforms = true := by
  exact OptimizerConformance.runtime_and_property_rewrite_match

end CMeta
