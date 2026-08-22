import CMetaCFlowCalculus.CMeta.Types
import CMetaCFlowCalculus.CMeta.Effects
import CMetaCFlowCalculus.CMeta.Properties

namespace CMetaCFlowCalculus.CMeta

/-- `Γ`, the trusted CMeta semantic environment used by Phase A judgements. -/
structure Env where
  hasCapability : Ty → TypeCapability → Prop
  declaresUnary : String → Ty → Ty → Effect → PropertySet → Prop
  declaresBinary : String → Ty → Ty → Ty → Effect → PropertySet → Prop
  declaresSource : String → Ty → Prop
  declaresCollector : String → Ty → Ty → Prop

/-- Evidence for `Γ ⊢ f : A → B ! ε [P]`. -/
structure UnaryCallable (Γ : Env) (input output : Ty) where
  name : String
  effect : Effect
  properties : PropertySet
  declared : Γ.declaresUnary name input output effect properties

/-- Evidence for `Γ ⊢ f : A × B → C ! ε [P]`. -/
structure BinaryCallable (Γ : Env) (left right output : Ty) where
  name : String
  effect : Effect
  properties : PropertySet
  declared : Γ.declaresBinary name left right output effect properties

/-- Evidence for `Γ ⊢ S : Source<A>`. -/
structure SourceDecl (Γ : Env) (output : Ty) where
  name : String
  declared : Γ.declaresSource name output

/-- Evidence for `Γ ⊢ c : Collector<A,R>`. -/
structure CollectorDecl (Γ : Env) (input result : Ty) where
  name : String
  declared : Γ.declaresCollector name input result

end CMetaCFlowCalculus.CMeta
