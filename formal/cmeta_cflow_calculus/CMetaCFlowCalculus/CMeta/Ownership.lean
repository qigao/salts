import CMetaCFlowCalculus.CMeta.Environment

namespace CMetaCFlowCalculus.CMeta

/-- Resource state tracked by the v1 ownership calculus. -/
inductive Ownership where
  | borrowed
  | owned
  | moved
  deriving Repr, DecidableEq

/-- A typed reference into the authoritative ownership context. -/
structure Value (ty : Ty) where
  token : Nat

/-- Existentially packed live value used by suspension checks. -/
structure PackedValue where
  ty : Ty
  token : Nat

def Value.pack {ty : Ty} (value : Value ty) : PackedValue where
  ty := ty
  token := value.token

/-- Borrowed and owned bindings are readable; a moved binding is not. -/
inductive Readable : Ownership → Prop where
  | borrowed : Readable .borrowed
  | owned : Readable .owned

/-- The authoritative state for one binding in an ownership context. -/
structure BindingState where
  ty : Ty
  ownership : Ownership
  deriving Repr, DecidableEq

/-- A token has at most one current binding state. -/
abbrev OwnershipContext := Nat → Option BindingState

namespace OwnershipContext

def set {ty : Ty} (context : OwnershipContext) (value : Value ty)
    (ownership : Ownership) : OwnershipContext :=
  fun candidate =>
    if candidate = value.token then some { ty := ty, ownership := ownership }
    else context candidate

end OwnershipContext

abbrev HasOwnership {ty : Ty} (context : OwnershipContext)
    (value : Value ty) (ownership : Ownership) : Prop :=
  context value.token = some { ty := ty, ownership := ownership }

def ContextReadable {ty : Ty} (context : OwnershipContext)
    (value : Value ty) : Prop :=
  ∃ ownership, HasOwnership context value ownership ∧ Readable ownership

/-- A suspension frame may contain only owned live values. -/
def SuspendSafe (context : OwnershipContext) (live : List PackedValue) : Prop :=
  ∀ value, value ∈ live →
    context value.token = some { ty := value.ty, ownership := .owned }

/-- The retain rule updates a borrowed binding to owned in the post-context. -/
def retain {Γ : Env} {ty : Ty} (context : OwnershipContext)
    (value : Value ty) (_borrowed : HasOwnership context value .borrowed)
    (_copyable : Γ.hasCapability ty .copy) : OwnershipContext :=
  context.set value .owned

/--
The move rule updates the single post-state fact source for a binding.
The pre-context remains available for meta-level reasoning but is not the
post-state used by subsequent calculus judgements.
-/
def move {Γ : Env} {ty : Ty} (context : OwnershipContext) (value : Value ty)
    (_owned : HasOwnership context value .owned)
    (_movable : Γ.hasCapability ty .move) : OwnershipContext :=
  context.set value .moved

end CMetaCFlowCalculus.CMeta
