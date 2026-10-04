import CMetaCFlowCalculus.CMeta.Environment

namespace CMetaCFlowCalculus.CMeta

/-- Resource state tracked by the ownership calculus.
    Shared and owned states carry one live cleanup obligation; moved and
    released are terminal for the current binding. -/
inductive Ownership where
  | borrowed
  | shared
  | owned
  | moved
  | released
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

/-- Borrowed, shared, and owned bindings are readable. -/
inductive Readable : Ownership → Prop where
  | borrowed : Readable .borrowed
  | shared : Readable .shared
  | owned : Readable .owned

/-- States that still carry exactly one cleanup obligation for this binding. -/
inductive NeedsCleanup : Ownership → Prop where
  | shared : NeedsCleanup .shared
  | owned : NeedsCleanup .owned

/-- States that may authoritatively anchor a borrowed lifetime. -/
inductive LifetimeAuthority : Ownership → Prop where
  | shared : LifetimeAuthority .shared
  | owned : LifetimeAuthority .owned

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

def ContextNeedsCleanup {ty : Ty} (context : OwnershipContext)
    (value : Value ty) : Prop :=
  ∃ ownership, HasOwnership context value ownership ∧ NeedsCleanup ownership

def ContextAuthority {ty : Ty} (context : OwnershipContext)
    (value : Value ty) : Prop :=
  ∃ ownership, HasOwnership context value ownership ∧
    LifetimeAuthority ownership

/-- A generic borrow graph. The key is the borrowed token and the value is the
    authoritative owner token. The relation is compiler/control-plane state,
    not runtime pointer metadata. -/
abbrev BorrowRelations := Nat → Option Nat

def BorrowedFrom {borrowTy ownerTy : Ty}
    (context : OwnershipContext) (relations : BorrowRelations)
    (borrowed : Value borrowTy) (owner : Value ownerTy) : Prop :=
  HasOwnership context borrowed .borrowed ∧
    relations borrowed.token = some owner.token ∧
    ContextAuthority context owner


namespace BorrowRelations

/-- Bind one borrowed token to its authoritative owner token. -/
def set (relations : BorrowRelations) (borrowed owner : Nat) : BorrowRelations :=
  fun candidate =>
    if candidate = borrowed then some owner else relations candidate

/-- End one borrow edge without changing unrelated borrow relations. -/
def clear (relations : BorrowRelations) (borrowed : Nat) : BorrowRelations :=
  fun candidate =>
    if candidate = borrowed then none else relations candidate

end BorrowRelations

/-- A borrowed result may escape only when it is tied to one live authority. -/
def BorrowEscapeSafe {borrowTy : Ty}
    (context : OwnershipContext) (relations : BorrowRelations)
    (borrowed : Value borrowTy) : Prop :=
  ∃ ownerTy : Ty, ∃ owner : Value ownerTy,
    BorrowedFrom context relations borrowed owner

/-- Final release of an authority is safe only when no live borrowed token
    still points at that authority. -/
def OwnerReleaseSafe {ownerTy : Ty}
    (context : OwnershipContext) (relations : BorrowRelations)
    (owner : Value ownerTy) : Prop :=
  ∀ {borrowTy : Ty} (borrowed : Value borrowTy),
    relations borrowed.token = some owner.token →
    ¬HasOwnership context borrowed .borrowed

/-- Admit an authoritative borrowed result together with its owner relation.
    The relation remains compiler/control-plane state and is not embedded in
    runtime Reflection descriptors. -/
def admitBorrowedResult {borrowTy ownerTy : Ty}
    (context : OwnershipContext) (relations : BorrowRelations)
    (borrowed : Value borrowTy) (owner : Value ownerTy)
    (_different : borrowed.token ≠ owner.token)
    (_authority : ContextAuthority context owner) :
    OwnershipContext × BorrowRelations :=
  (context.set borrowed .borrowed,
   relations.set borrowed.token owner.token)


/--
End one valid borrow before its authoritative owner becomes terminal.

The borrowed binding becomes released and its owner edge is cleared. The owner
itself is untouched. This is compiler/control-plane lifetime state only; no
runtime release/destroy operation is implied for a borrowed view.
-/
def endBorrow {borrowTy ownerTy : Ty}
    (context : OwnershipContext) (relations : BorrowRelations)
    (borrowed : Value borrowTy) (owner : Value ownerTy)
    (_bound : BorrowedFrom context relations borrowed owner) :
    OwnershipContext × BorrowRelations :=
  (context.set borrowed .released,
   relations.clear borrowed.token)


/-- Canonical semantic classes for a reflected function result.
    Nullability is orthogonal and therefore intentionally absent here. -/
inductive ResultOwnership where
  | unknown
  | value
  | borrowed
  | shared
  | owned
  deriving Repr, DecidableEq

/-- Canonical parameter ownership semantics used at an admitted call boundary.
    UNKNOWN is non-authoritative; BORROWED preserves caller ownership; OWNED
    consumes one caller-owned value after admission. -/
inductive ParameterOwnership where
  | unknown
  | borrowed
  | owned
  deriving Repr, DecidableEq

/-- Passing a borrowed parameter never changes caller ownership. -/
def admitBorrowedParameter {ty : Ty}
    (context : OwnershipContext) (value : Value ty)
    (_readable : ContextReadable context value) : OwnershipContext :=
  context


/-- Translate authoritative reflected result semantics into the ownership
    calculus. UNKNOWN deliberately yields no automatic ownership proof.
    VALUE is caller-owned value state; its concrete destroy may be trivial. -/
def ResultOwnership.toState : ResultOwnership → Option Ownership
  | .unknown => none
  | .value => some .owned
  | .borrowed => some .borrowed
  | .shared => some .shared
  | .owned => some .owned

/-- Admit one reflected result into the ownership context only when its
    semantic ownership class is authoritative. -/
def admitResult {ty : Ty} (context : OwnershipContext) (value : Value ty)
    (result : ResultOwnership) : Option OwnershipContext :=
  match result.toState with
  | none => none
  | some ownership => some (context.set value ownership)

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


/-- An OWNED parameter consumes the caller binding only after the call boundary
    has been admitted; the post-context is the ordinary move transition. -/
def admitOwnedParameter {Γ : Env} {ty : Ty}
    (context : OwnershipContext) (value : Value ty)
    (owned : HasOwnership context value .owned)
    (movable : Γ.hasCapability ty .move) : OwnershipContext :=
  move context value owned movable

/-- Conservative branch join: only identical ownership states join.
    In particular, an owned/moved mismatch is rejected rather than resurrecting
    ownership on the moved path. -/
def joinOwnership (left right : Ownership) : Option Ownership :=
  if left = right then some left else none

/-- Discharge one existing owned/shared cleanup obligation.
    The concrete destroy/release operation is outside the calculus; this
    transition records its successful exactly-once semantic effect. -/
def discharge {ty : Ty} (context : OwnershipContext) (value : Value ty)
    (ownership : Ownership)
    (_current : HasOwnership context value ownership)
    (_required : NeedsCleanup ownership) : OwnershipContext :=
  context.set value .released

end CMetaCFlowCalculus.CMeta
