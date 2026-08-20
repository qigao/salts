import CMeta.OptimizerConformance
import CMeta.OptimizerGatingGeneratedC

/-!
# Optimizer effect/property gating conformance

This module checks the authorization boundary around real `opt.c` rewrites.
The C witness computes purity/stability through `cflow_node_effects` and
`cflow_node_properties`, runs the real optimizer, inspects its stats/IR, and
executes both normalized and optimized graphs.

The formal side models only the permission predicates and the witness value
semantics.  A rewrite may be semantically harmless yet still be forbidden when
its required effect/property contract is absent.
-/

namespace CMeta

private def mapFusionAuthorized (leftPure rightPure : Bool) : Bool :=
  leftPure && rightPure

private def relationSimplificationAuthorized (isPure isStable : Bool) : Bool :=
  isPure && isStable

/-- MAP fusion requires purity on both adjacent maplike nodes. -/
theorem OptimizerGatingConformance.map_fusion_authorized_iff
    (leftPure rightPure : Bool) :
    mapFusionAuthorized leftPure rightPure = true ↔
      leftPure = true ∧ rightPure = true := by
  cases leftPure <;> cases rightPure <;> decide

/-- Single-branch relation simplification requires both purity and STABLE
    (`DETERMINISTIC | TOTAL`) properties. -/
theorem OptimizerGatingConformance.relation_simplification_authorized_iff
    (isPure isStable : Bool) :
    relationSimplificationAuthorized isPure isStable = true ↔
      isPure = true ∧ isStable = true := by
  cases isPure <;> cases isStable <;> decide

private def runBlockedMapPair (xs : List Int) : List Int :=
  xs.map (fun x => (x + 1) * 2)

private def runRelationBranch (xs : List Int) : List Int :=
  xs.map (fun x => x * 3)

private def mapEffectWitnessConforms
    (w : COptimizerGatingGenerated.MapEffectWitness) : Bool :=
  w.name == "impure_map_blocks_fusion" &&
  !mapFusionAuthorized w.firstPure w.secondPure &&
  w.firstPure == false &&
  w.secondPure == true &&
  w.nodesBefore == 3 &&
  w.nodesAfter == 3 &&
  w.mapNodesFused == 0 &&
  w.effectBlocked == 1 &&
  w.before == runBlockedMapPair w.input &&
  w.after == runBlockedMapPair w.input

private structure RelationGateModel where
  isPure : Bool
  isStable : Bool
  afterCoordination : String
  simplified : Nat
  effectBlocked : Nat
  propertyBlocked : Nat

private def relationGateModel (name : String) : Option RelationGateModel :=
  match name with
  | "relation_pure_stable_simplifies" =>
      some ⟨true, true, "ANY", 1, 0, 0⟩
  | "relation_pure_unstable_blocks" =>
      some ⟨true, false, "SEQUENCE", 0, 0, 1⟩
  | "relation_impure_stable_blocks" =>
      some ⟨false, true, "SEQUENCE", 0, 1, 0⟩
  | _ => none

private def relationGateWitnessConforms
    (w : COptimizerGatingGenerated.RelationGateWitness) : Bool :=
  match relationGateModel w.name with
  | some expected =>
      w.isPure == expected.isPure &&
      w.isStable == expected.isStable &&
      w.beforeCoordination == "SEQUENCE" &&
      w.afterCoordination == expected.afterCoordination &&
      w.simplified == expected.simplified &&
      w.effectBlocked == expected.effectBlocked &&
      w.propertyBlocked == expected.propertyBlocked &&
      (relationSimplificationAuthorized w.isPure w.isStable ==
        (w.simplified == 1)) &&
      w.before == runRelationBranch w.input &&
      w.after == runRelationBranch w.input
  | none => false

/-- The real impure MAP pair is not fused, records the effect block, and has the
    same values before and after optimization. -/
theorem OptimizerGatingConformance.map_effect_gate_matches :
    COptimizerGatingGenerated.mapEffectWitnesses.all
      mapEffectWitnessConforms = true := by
  native_decide

/-- The relation suite contains the authorized case plus independent property-
    and effect-blocked cases. -/
theorem OptimizerGatingConformance.relation_coverage :
    COptimizerGatingGenerated.relationGateWitnesses.map (fun w => w.name) =
      ["relation_pure_stable_simplifies",
       "relation_pure_unstable_blocks",
       "relation_impure_stable_blocks"] := by
  native_decide

/-- Real relation simplification follows exactly the PURE+STABLE authorization
    rule, reports the appropriate blocked counter otherwise, and preserves
    runtime values in every case. -/
theorem OptimizerGatingConformance.relation_gate_matches :
    COptimizerGatingGenerated.relationGateWitnesses.all
      relationGateWitnessConforms = true := by
  native_decide

/-- Public optimizer gating implementation gate. -/
theorem CImplementationConformance.optimizer_effect_property_gates :
    COptimizerGatingGenerated.mapEffectWitnesses.all mapEffectWitnessConforms = true ∧
    COptimizerGatingGenerated.relationGateWitnesses.all relationGateWitnessConforms = true := by
  exact ⟨OptimizerGatingConformance.map_effect_gate_matches,
    OptimizerGatingConformance.relation_gate_matches⟩

end CMeta
