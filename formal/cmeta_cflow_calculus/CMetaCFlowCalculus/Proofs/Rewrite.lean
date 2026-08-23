import CMetaCFlowCalculus.CFlow.Rewrite

namespace CMetaCFlowCalculus.CFlow

open CMetaCFlowCalculus.CMeta

private theorem list_map_identity (transform : α → α)
    (identity : ∀ value, transform value = value) (values : List α) :
    values.map transform = values := by
  induction values with
  | nil => rfl
  | cons head tail ih =>
      simp only [List.map]
      rw [identity head, ih]

private theorem list_map_idempotent (transform : α → α)
    (idempotent : ∀ value, transform (transform value) = transform value)
    (values : List α) :
    (values.map transform).map transform = values.map transform := by
  induction values with
  | nil => rfl
  | cons head tail ih =>
      simp only [List.map]
      rw [idempotent head, ih]

private theorem list_take_map (transform : α → β) (count : Nat)
    (values : List α) :
    (values.map transform).take count = (values.take count).map transform := by
  induction count generalizing values with
  | zero => simp
  | succ count ih =>
      cases values with
      | nil => simp
      | cons head tail =>
          simp only [List.map, List.take]
          exact congrArg (List.cons (transform head)) (ih tail)

private theorem list_filter_true (predicate : α → Bool)
    (always : ∀ value, predicate value = true) (values : List α) :
    values.filter predicate = values := by
  induction values with
  | nil => rfl
  | cons head tail ih => simp [always, ih]

private theorem list_filter_fusion (first second : α → Bool)
    (values : List α) :
    (values.filter first).filter second =
      values.filter (fun value => first value && second value) := by
  induction values with
  | nil => rfl
  | cons head tail ih =>
      cases firstHead : first head <;>
        cases secondHead : second head <;>
          simp [firstHead, secondHead, ih]

private theorem list_drop_drop_add (values : List α) (first second : Nat) :
    (values.drop second).drop first = values.drop (second + first) := by
  induction second generalizing values with
  | zero => simp
  | succ second ih =>
      cases values with
      | nil => simp
      | cons head tail => simp [ih, Nat.succ_add]

private theorem list_drop_eq_nil_of_length_le {values : List α} {count : Nat}
    (bounded : values.length ≤ count) : values.drop count = [] := by
  induction count generalizing values with
  | zero =>
      cases values with
      | nil => rfl
      | cons head tail => simp at bounded
  | succ count ih =>
      cases values with
      | nil => rfl
      | cons head tail =>
          simp only [List.drop_succ_cons]
          apply ih
          simpa using bounded

private theorem list_drop_satAdd (max left right : Nat) (values : List α)
    (bounded : values.length ≤ max) :
    values.drop (satAdd max left right) = values.drop (left + right) := by
  unfold satAdd
  by_cases fits : left + right ≤ max
  · rw [Nat.min_eq_right fits]
  · have maxLt : max < left + right := Nat.lt_of_not_ge fits
    rw [Nat.min_eq_left (Nat.le_of_lt maxLt)]
    rw [list_drop_eq_nil_of_length_le bounded]
    rw [list_drop_eq_nil_of_length_le
      (Nat.le_trans bounded (Nat.le_of_lt maxLt))]

/-- R1: a declared pure/total semantic identity can be removed. -/
theorem r1_map_identity {Γ : Env} {ty : Ty}
    (identity : UnaryMeaning Γ ty ty)
    (_pure : identity.callable.effect = .pure)
    (_total : identity.callable.properties .total)
    (identityLaw : ∀ value, identity.apply value = value)
    (stream : StreamResult (Value ty)) :
    stream.map identity.apply = stream := by
  cases stream
  simp [StreamResult.map,
    list_map_identity identity.apply identityLaw]

/-- R2: pure/total maps fuse without changing encounter order. -/
theorem r2_map_fusion {Γ : Env} {inputTy middleTy outputTy : Ty}
    (first : UnaryMeaning Γ inputTy middleTy)
    (second : UnaryMeaning Γ middleTy outputTy)
    (_firstPure : first.callable.effect = .pure)
    (_firstTotal : first.callable.properties .total)
    (_secondPure : second.callable.effect = .pure)
    (_secondTotal : second.callable.properties .total)
    (stream : StreamResult (Value inputTy)) :
    (stream.map first.apply).map second.apply =
      stream.map (fun value => second.apply (first.apply value)) := by
  cases stream
  simp [StreamResult.map, List.map_map]

/-- A duplicate pure/total endomap may be removed only with its semantic law. -/
theorem map_idempotent_elimination {Γ : Env} {ty : Ty}
    (premises : IdempotentEndomapPremises Γ ty)
    (stream : StreamResult (Value ty)) :
    (stream.map premises.meaning.apply).map premises.meaning.apply =
      stream.map premises.meaning.apply := by
  cases stream
  simp [StreamResult.map,
    list_map_idempotent premises.meaning.apply premises.idempotentLaw]

/-- Every finite composition of certified optimizer rewrites preserves all
`StreamResult` observations, including effects, terminal outcome and ownership. -/
theorem certified_rewrite_preserves_observations {α : Type}
    {before after : StreamResult α}
    (certificate : CertifiedRewrite before after) : before = after := by
  induction certificate with
  | refl => rfl
  | idempotentMap premises stream =>
      exact map_idempotent_elimination premises stream
  | trans _ _ firstSecond secondThird =>
      exact Eq.trans firstSecond secondThird

/-- R3: a declared pure/total always-true predicate can be removed. -/
theorem r3_filter_true {Γ : Env} {ty : Ty}
    (predicate : PredicateMeaning Γ ty)
    (_pure : predicate.callable.effect = .pure)
    (_total : predicate.callable.properties .total)
    (always : ∀ value, predicate.apply value = true)
    (stream : StreamResult (Value ty)) :
    stream.filter predicate.apply = stream := by
  cases stream
  simp [StreamResult.filter, list_filter_true predicate.apply always]

/-- R4: fusion evaluates `first` before `second` through Boolean `and`. -/
theorem r4_filter_fusion {Γ : Env} {ty : Ty}
    (first second : PredicateMeaning Γ ty)
    (_firstPure : first.callable.effect = .pure)
    (_firstTotal : first.callable.properties .total)
    (_secondPure : second.callable.effect = .pure)
    (_secondTotal : second.callable.properties .total)
    (stream : StreamResult (Value ty)) :
    (stream.filter first.apply).filter second.apply =
      stream.filter (fun value => first.apply value && second.apply value) := by
  cases stream
  simp [StreamResult.filter,
    list_filter_fusion first.apply second.apply]

/-- R5: zero limit removes traversal but retains construction observations. -/
theorem r5_limit_zero (stream : StreamResult α) :
    stream.limit 0 = stream.emptyTraversal := by
  cases stream
  rfl

/-- R6: nested limits retain the smaller bound. -/
theorem r6_nested_limit (outer inner : Nat) (stream : StreamResult α) :
    (stream.limit inner).limit outer = stream.limit (min outer inner) := by
  cases stream
  simp [StreamResult.limit, List.take_take]

/-- R7: skipping zero values is observationally inert. -/
theorem r7_skip_zero (stream : StreamResult α) :
    stream.skip 0 = stream := by
  cases stream
  rfl

/-- R8: bounded streams make saturated addition equivalent to nested skip. -/
theorem r8_nested_skip (max outer inner : Nat) (stream : StreamResult α)
    (bounded : stream.values.length ≤ max) :
    (stream.skip inner).skip outer = stream.skip (satAdd max outer inner) := by
  cases stream with
  | mk effects values outcome safe =>
      simp only [StreamResult.skip]
      rw [list_drop_drop_add]
      rw [Nat.add_comm inner outer]
      rw [← list_drop_satAdd max outer inner values bounded]

/-- R9: a pure/total Map can move after Limit without changing callbacks kept. -/
theorem r9_limit_map {Γ : Env} {inputTy outputTy : Ty}
    (meaning : UnaryMeaning Γ inputTy outputTy)
    (_pure : meaning.callable.effect = .pure)
    (_total : meaning.callable.properties .total)
    (count : Nat) (stream : StreamResult (Value inputTy)) :
    (stream.map meaning.apply).limit count =
      (stream.limit count).map meaning.apply := by
  cases stream
  simp only [StreamResult.map, StreamResult.limit]
  rw [list_take_map]

private theorem list_foldl_append (combine : α → β → α) (initial : α)
    (left right : List β) :
    (left ++ right).foldl combine initial =
      right.foldl combine (left.foldl combine initial) := by
  induction left generalizing initial with
  | nil => rfl
  | cons head tail ih =>
      simp only [List.cons_append, List.foldl]
      exact ih (combine initial head)

private theorem reductionTree_foldl (combine : α → α → α)
    (associativeLaw : ∀ first second third,
      combine (combine first second) third =
        combine first (combine second third))
    (initial : α) (tree : ReductionTree α) :
    tree.flatten.foldl combine initial =
      combine initial (tree.eval combine) := by
  induction tree generalizing initial with
  | leaf value => rfl
  | node left right leftIH rightIH =>
      simp only [ReductionTree.flatten, ReductionTree.eval]
      rw [list_foldl_append]
      rw [leftIH, rightIH]
      exact associativeLaw initial (left.eval combine) (right.eval combine)

/-- R10: associativity changes parenthesization, never encounter order. -/
theorem r10_reduce_reassociation {Γ : Env} {ty : Ty}
    (meaning : BinaryMeaning Γ ty)
    (_pure : meaning.callable.effect = .pure)
    (_total : meaning.callable.properties .total)
    (_associative : meaning.callable.properties .associative)
    (associativeLaw : ∀ first second third,
      meaning.apply (meaning.apply first second) third =
        meaning.apply first (meaning.apply second third))
    (initial : Value ty) (remaining : ReductionForest (Value ty)) :
    remaining.flatten.foldl meaning.apply initial =
      remaining.evalFrom meaning.apply initial := by
  cases remaining with
  | empty => rfl
  | tree reduction =>
      exact reductionTree_foldl meaning.apply associativeLaw initial reduction

/-- Every R11–R14 execution refinement retains the same semantic result. -/
theorem execution_refinement_preserves_semantics {Γ : Env}
    {K : KernelCapabilities} {α : Type}
    {before after : ExecutionVariant α}
    (refinement : ExecutionRefines Γ K before after) :
    before.result = after.result := by
  cases refinement <;> rfl

/-- R15 exposes the ownership condition used at every async boundary. -/
theorem r15_async_promotion_requires_owned {boundary : AsyncBoundary}
    {context : OwnershipContext} {liveRoots : List PackedValue}
    (wellFormed : AsyncPromotionWellFormed boundary context liveRoots) :
    SuspendSafe context liveRoots :=
  wellFormed

end CMetaCFlowCalculus.CFlow
