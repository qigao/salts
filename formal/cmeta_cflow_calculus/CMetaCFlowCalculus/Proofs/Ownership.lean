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

theorem reflected_unknown_result_not_admitted {ty : Ty}
    (context : OwnershipContext) (value : Value ty) :
    admitResult context value .unknown = none := by
  rfl

theorem reflected_result_admission_updates_source {ty : Ty}
    (context : OwnershipContext) (value : Value ty)
    (result : ResultOwnership) (ownership : Ownership)
    (mapped : result.toState = some ownership) :
    ∃ post,
      admitResult context value result = some post ∧
      HasOwnership post value ownership := by
  refine ⟨context.set value ownership, ?_, ?_⟩
  · simp [admitResult, mapped]
  · change
      (context.set value ownership) value.token =
        some { ty := ty, ownership := ownership }
    simp [OwnershipContext.set]

theorem admitted_borrowed_result_is_bound
    {borrowTy ownerTy : Ty}
    (context : OwnershipContext) (relations : BorrowRelations)
    (borrowed : Value borrowTy) (owner : Value ownerTy)
    (different : borrowed.token ≠ owner.token)
    (authority : ContextAuthority context owner) :
    let post :=
      admitBorrowedResult
        context relations borrowed owner different authority
    BorrowedFrom post.1 post.2 borrowed owner := by
  dsimp [admitBorrowedResult]
  constructor
  · change
      (context.set borrowed .borrowed) borrowed.token =
        some { ty := borrowTy, ownership := .borrowed }
    simp [OwnershipContext.set]
  · constructor
    · simp [BorrowRelations.set]
    · rcases authority with ⟨ownership, atOwner, authorityState⟩
      refine ⟨ownership, ?_, authorityState⟩
      change
        (context.set borrowed .borrowed) owner.token =
          some { ty := ownerTy, ownership := ownership }
      simp [OwnershipContext.set, Ne.symm different, atOwner]

theorem admitted_borrowed_result_escape_safe
    {borrowTy ownerTy : Ty}
    (context : OwnershipContext) (relations : BorrowRelations)
    (borrowed : Value borrowTy) (owner : Value ownerTy)
    (different : borrowed.token ≠ owner.token)
    (authority : ContextAuthority context owner) :
    let post :=
      admitBorrowedResult
        context relations borrowed owner different authority
    BorrowEscapeSafe post.1 post.2 borrowed := by
  dsimp
  refine ⟨ownerTy, owner, ?_⟩
  exact
    admitted_borrowed_result_is_bound
      context relations borrowed owner different authority

theorem borrowed_result_has_no_cleanup
    {borrowTy ownerTy : Ty}
    (context : OwnershipContext) (relations : BorrowRelations)
    (borrowed : Value borrowTy) (owner : Value ownerTy)
    (bound : BorrowedFrom context relations borrowed owner) :
    ¬ContextNeedsCleanup context borrowed := by
  intro cleanup
  rcases cleanup with ⟨ownership, atBorrowed, required⟩
  rw [bound.1] at atBorrowed
  cases atBorrowed
  exact borrowed_not_cleanup required

theorem live_borrow_blocks_owner_release
    {borrowTy ownerTy : Ty}
    (context : OwnershipContext) (relations : BorrowRelations)
    (borrowed : Value borrowTy) (owner : Value ownerTy)
    (bound : BorrowedFrom context relations borrowed owner) :
    ¬OwnerReleaseSafe context relations owner := by
  intro safe
  exact safe borrowed bound.2.1 bound.1

theorem borrowed_parameter_preserves_caller
    {ty : Ty} (context : OwnershipContext) (value : Value ty)
    (readable : ContextReadable context value) :
    admitBorrowedParameter context value readable = context := by
  rfl

theorem shared_readable : Readable .shared := .shared

/-- The post-move ownership state has no readability constructor. -/
theorem moved_not_readable : ¬Readable .moved := by
  intro readable
  cases readable

/-- A released binding is terminal and cannot be read. -/
theorem released_not_readable : ¬Readable .released := by
  intro readable
  cases readable

theorem borrowed_not_cleanup : ¬NeedsCleanup .borrowed := by
  intro required
  cases required

theorem moved_not_cleanup : ¬NeedsCleanup .moved := by
  intro required
  cases required

theorem released_not_cleanup : ¬NeedsCleanup .released := by
  intro required
  cases required

theorem released_not_authority : ¬LifetimeAuthority .released := by
  intro authority
  cases authority

/-- The moved binding is updated in the post-context. -/
theorem move_updates_source {Γ : Env} {ty : Ty}
    (context : OwnershipContext) (value : Value ty)
    (owned : HasOwnership context value .owned)
    (movable : Γ.hasCapability ty .move) :
    move context value owned movable value.token =
      some { ty := ty, ownership := .moved } := by
  simp [move, OwnershipContext.set]

theorem owned_parameter_consumes_after_admission {Γ : Env} {ty : Ty}
    (context : OwnershipContext) (value : Value ty)
    (owned : HasOwnership context value .owned)
    (movable : Γ.hasCapability ty .move) :
    admitOwnedParameter context value owned movable value.token =
      some { ty := ty, ownership := .moved } := by
  simpa [admitOwnedParameter] using
    move_updates_source context value owned movable

theorem join_owned_owned_preserves :
    joinOwnership .owned .owned = some .owned := by
  simp [joinOwnership]

theorem join_moved_moved_preserves :
    joinOwnership .moved .moved = some .moved := by
  simp [joinOwnership]

theorem join_owned_moved_rejected :
    joinOwnership .owned .moved = none := by
  simp [joinOwnership]

theorem join_moved_owned_rejected :
    joinOwnership .moved .owned = none := by
  simp [joinOwnership]

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


/-- Discharging a cleanup obligation makes the current binding terminal. -/
theorem discharge_updates_source {ty : Ty}
    (context : OwnershipContext) (value : Value ty)
    (ownership : Ownership)
    (current : HasOwnership context value ownership)
    (required : NeedsCleanup ownership) :
    discharge context value ownership current required value.token =
      some { ty := ty, ownership := .released } := by
  simp [discharge, OwnershipContext.set]

theorem discharge_source_not_readable {ty : Ty}
    (context : OwnershipContext) (value : Value ty)
    (ownership : Ownership)
    (current : HasOwnership context value ownership)
    (required : NeedsCleanup ownership) :
    ¬ContextReadable (discharge context value ownership current required) value := by
  intro sourceReadable
  rcases sourceReadable with ⟨postOwnership, atSource, readable⟩
  have releasedAt :=
    discharge_updates_source context value ownership current required
  change discharge context value ownership current required value.token =
    some { ty := ty, ownership := postOwnership } at atSource
  rw [releasedAt] at atSource
  cases atSource
  exact released_not_readable readable

theorem discharge_removes_cleanup {ty : Ty}
    (context : OwnershipContext) (value : Value ty)
    (ownership : Ownership)
    (current : HasOwnership context value ownership)
    (required : NeedsCleanup ownership) :
    ¬ContextNeedsCleanup
      (discharge context value ownership current required) value := by
  intro remaining
  rcases remaining with ⟨postOwnership, atSource, postRequired⟩
  have releasedAt :=
    discharge_updates_source context value ownership current required
  change discharge context value ownership current required value.token =
    some { ty := ty, ownership := postOwnership } at atSource
  rw [releasedAt] at atSource
  cases atSource
  exact released_not_cleanup postRequired

theorem discharge_source_not_authority {ty : Ty}
    (context : OwnershipContext) (value : Value ty)
    (ownership : Ownership)
    (current : HasOwnership context value ownership)
    (required : NeedsCleanup ownership) :
    ¬ContextAuthority
      (discharge context value ownership current required) value := by
  intro remaining
  rcases remaining with ⟨postOwnership, atSource, authority⟩
  have releasedAt :=
    discharge_updates_source context value ownership current required
  change discharge context value ownership current required value.token =
    some { ty := ty, ownership := postOwnership } at atSource
  rw [releasedAt] at atSource
  cases atSource
  exact released_not_authority authority

/-- Discharging one binding leaves every other token unchanged. -/
theorem discharge_preserves_other {ty : Ty}
    (context : OwnershipContext) (value : Value ty)
    (ownership : Ownership)
    (current : HasOwnership context value ownership)
    (required : NeedsCleanup ownership) {candidate : Nat}
    (different : candidate ≠ value.token) :
    discharge context value ownership current required candidate =
      context candidate := by
  simp [discharge, OwnershipContext.set, different]

/-- A borrow anchored to an owner is invalid after that owner's cleanup. -/
theorem borrowed_from_invalid_after_owner_discharge
    {borrowTy ownerTy : Ty}
    (context : OwnershipContext) (relations : BorrowRelations)
    (borrowed : Value borrowTy) (owner : Value ownerTy)
    (ownerOwnership : Ownership)
    (ownerCurrent : HasOwnership context owner ownerOwnership)
    (required : NeedsCleanup ownerOwnership) :
    ¬BorrowedFrom
      (discharge context owner ownerOwnership ownerCurrent required)
      relations borrowed owner := by
  intro valid
  exact
    discharge_source_not_authority
      context owner ownerOwnership ownerCurrent required valid.2.2

end CMetaCFlowCalculus.CMeta
