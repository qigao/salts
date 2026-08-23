import CMetaCFlowCalculus.CMeta.Ownership

namespace CMetaCFlowCalculus.CMeta

/-- Retaining a borrowed binding establishes the suspension invariant. -/
theorem retain_updates_source {Γ : Env} {ty : Ty}
    (context : OwnershipContext) (value : Value ty)
    (borrowed : HasOwnership context value .borrowed)
    (copyable : Γ.hasCapability ty .copy) :
    retain context value borrowed copyable value.token =
      some { ty := ty, ownership := .owned } := by
  simp [retain, OwnershipContext.set]

theorem retain_suspend_safe {Γ : Env} {ty : Ty}
    (context : OwnershipContext) (value : Value ty)
    (borrowed : HasOwnership context value .borrowed)
    (copyable : Γ.hasCapability ty .copy) :
    SuspendSafe (retain context value borrowed copyable) [value.pack] := by
  intro liveValue membership
  simp only [List.mem_cons, List.not_mem_nil, or_false] at membership
  subst liveValue
  exact retain_updates_source context value borrowed copyable

/-- A borrowed singleton cannot inhabit a valid suspension frame. -/
theorem borrowed_not_suspend_safe {ty : Ty} (context : OwnershipContext)
    (value : Value ty) (borrowed : HasOwnership context value .borrowed) :
    ¬SuspendSafe context [value.pack] := by
  intro safe
  have ownedAt := safe value.pack (by simp)
  change context value.token = some { ty := ty, ownership := .owned } at ownedAt
  rw [borrowed] at ownedAt
  cases ownedAt

/-- The post-move ownership state has no readability constructor. -/
theorem moved_not_readable : ¬Readable .moved := by
  intro readable
  cases readable

/-- The moved binding is updated in the post-context. -/
theorem move_updates_source {Γ : Env} {ty : Ty}
    (context : OwnershipContext) (value : Value ty)
    (owned : HasOwnership context value .owned)
    (movable : Γ.hasCapability ty .move) :
    move context value owned movable value.token =
      some { ty := ty, ownership := .moved } := by
  simp [move, OwnershipContext.set]

/-- The source token cannot be read in the post-move context. -/
theorem move_source_not_readable {Γ : Env} {ty : Ty}
    (context : OwnershipContext) (value : Value ty)
    (owned : HasOwnership context value .owned)
    (movable : Γ.hasCapability ty .move) :
    ¬ContextReadable (move context value owned movable) value := by
  intro sourceReadable
  rcases sourceReadable with ⟨ownership, atSource, readable⟩
  have movedAt := move_updates_source context value owned movable
  change move context value owned movable value.token =
    some { ty := ty, ownership := ownership } at atSource
  rw [movedAt] at atSource
  cases atSource
  exact moved_not_readable readable

/-- A stale value reference cannot bypass the post-context at suspension. -/
theorem move_source_not_suspend_safe {Γ : Env} {ty : Ty}
    (context : OwnershipContext) (value : Value ty)
    (owned : HasOwnership context value .owned)
    (movable : Γ.hasCapability ty .move) :
    ¬SuspendSafe (move context value owned movable) [value.pack] := by
  intro safe
  have ownedAt := safe value.pack (by simp)
  have movedAt := move_updates_source context value owned movable
  change move context value owned movable value.token =
    some { ty := ty, ownership := .owned } at ownedAt
  rw [movedAt] at ownedAt
  cases ownedAt

/-- Moving one binding leaves every other token unchanged. -/
theorem move_preserves_other {Γ : Env} {ty : Ty}
    (context : OwnershipContext) (value : Value ty)
    (owned : HasOwnership context value .owned)
    (movable : Γ.hasCapability ty .move) {candidate : Nat}
    (different : candidate ≠ value.token) :
    move context value owned movable candidate = context candidate := by
  simp [move, OwnershipContext.set, different]

end CMetaCFlowCalculus.CMeta
