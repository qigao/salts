import CMetaCFlowCalculus.CFlow.Rewrite
import CMetaCFlowCalculus.Proofs.Rewrite
import PhaseATests.PhaseD

open CMetaCFlowCalculus.CMeta
open CMetaCFlowCalculus.CFlow

namespace CMetaCFlowCalculus.Tests.PhaseF

def idempotentProperties : PropertySet :=
  PropertySet.ofList [.total, .idempotent]

def clampCallable : UnaryCallable PhaseD.permissiveEnv PhaseD.scalarTy
    PhaseD.scalarTy where
  name := "clampNonnegative"
  effect := .pure
  properties := idempotentProperties
  declared := trivial

def clampMeaning : UnaryMeaning PhaseD.permissiveEnv PhaseD.scalarTy
    PhaseD.scalarTy where
  callable := clampCallable
  apply := fun value => { token := min value.token 10 }

theorem clampDeclaredTotal : clampMeaning.callable.properties .total := by
  simp [clampMeaning, clampCallable, idempotentProperties, PropertySet.ofList]

theorem clampDeclaredIdempotent :
    clampMeaning.callable.properties .idempotent := by
  simp [clampMeaning, clampCallable, idempotentProperties, PropertySet.ofList]

theorem clampIdempotentLaw : IdempotentLaw clampMeaning := by
  intro value
  cases value
  simp [clampMeaning, Nat.min_assoc]

def clampPremises : IdempotentEndomapPremises
    PhaseD.permissiveEnv PhaseD.scalarTy where
  meaning := clampMeaning
  pure := rfl
  total := clampDeclaredTotal
  declaredIdempotent := clampDeclaredIdempotent
  idempotentLaw := clampIdempotentLaw

example :
    (PhaseD.valueStream.map clampMeaning.apply).map clampMeaning.apply =
      PhaseD.valueStream.map clampMeaning.apply :=
  map_idempotent_elimination clampPremises PhaseD.valueStream

theorem firstElimination : CertifiedRewrite
    (((PhaseD.valueStream.map clampMeaning.apply).map clampMeaning.apply).map
      clampMeaning.apply)
    ((PhaseD.valueStream.map clampMeaning.apply).map clampMeaning.apply) :=
  .idempotentMap clampPremises (PhaseD.valueStream.map clampMeaning.apply)

theorem secondElimination : CertifiedRewrite
    ((PhaseD.valueStream.map clampMeaning.apply).map clampMeaning.apply)
    (PhaseD.valueStream.map clampMeaning.apply) :=
  .idempotentMap clampPremises PhaseD.valueStream

theorem repeatedElimination : CertifiedRewrite
    (((PhaseD.valueStream.map clampMeaning.apply).map clampMeaning.apply).map
      clampMeaning.apply)
    (PhaseD.valueStream.map clampMeaning.apply) :=
  .trans firstElimination secondElimination

example :
    ((PhaseD.valueStream.map clampMeaning.apply).map clampMeaning.apply).map
        clampMeaning.apply =
      PhaseD.valueStream.map clampMeaning.apply :=
  certified_rewrite_preserves_observations repeatedElimination

/-- A property declaration is not itself the semantic law consumed by Lean. -/
def dishonestCallable : UnaryCallable PhaseD.permissiveEnv PhaseD.scalarTy
    PhaseD.scalarTy where
  name := "dishonestSuccessor"
  effect := .pure
  properties := idempotentProperties
  declared := trivial

def dishonestMeaning : UnaryMeaning PhaseD.permissiveEnv PhaseD.scalarTy
    PhaseD.scalarTy where
  callable := dishonestCallable
  apply := fun value => { token := value.token + 1 }

example : dishonestMeaning.callable.properties .idempotent := by
  simp [dishonestMeaning, dishonestCallable, idempotentProperties,
    PropertySet.ofList]

example : ¬IdempotentLaw dishonestMeaning := by
  intro law
  have impossible := congrArg Value.token (law { token := 0 })
  simp [dishonestMeaning] at impossible

end CMetaCFlowCalculus.Tests.PhaseF
