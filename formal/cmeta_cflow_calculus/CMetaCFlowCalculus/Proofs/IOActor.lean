import CMetaCFlowCalculus.IO.Actor
import CMetaCFlowCalculus.Proofs.IOBoundedMpsc
import CMetaCFlowCalculus.Proofs.IOExecutor

namespace CMetaCFlowCalculus.IO.Actor

theorem containsLease_false_iff (requests : List Request) (lease : LeaseId) :
    containsLease requests lease = false ↔
      lease ∉ requests.map Request.lease := by
  induction requests with
  | nil => simp [containsLease]
  | cons request remaining inductionHypothesis =>
      by_cases same : request.lease = lease
      · simp [containsLease, same]
      · simp [containsLease, same, inductionHypothesis, Ne.symm same]

theorem modifyPhase_length (requests : List Request) (requestId : RequestId)
    (update : RequestPhase → RequestPhase) :
    (modifyPhase requests requestId update).length = requests.length := by
  induction requests with
  | nil => simp [modifyPhase]
  | cons request remaining inductionHypothesis =>
      by_cases same : request.id = requestId <;>
        simp [modifyPhase, same, inductionHypothesis]

theorem modifyPhase_ids (requests : List Request) (requestId : RequestId)
    (update : RequestPhase → RequestPhase) :
    (modifyPhase requests requestId update).map Request.id =
      requests.map Request.id := by
  induction requests with
  | nil => simp [modifyPhase]
  | cons request remaining inductionHypothesis =>
      by_cases same : request.id = requestId <;>
        simp [modifyPhase, same, inductionHypothesis]

theorem modifyPhase_leases (requests : List Request) (requestId : RequestId)
    (update : RequestPhase → RequestPhase) :
    (modifyPhase requests requestId update).map Request.lease =
      requests.map Request.lease := by
  induction requests with
  | nil => simp [modifyPhase]
  | cons request remaining inductionHypothesis =>
      by_cases same : request.id = requestId <;>
        simp [modifyPhase, same, inductionHypothesis]

theorem removeRequest_sublist (requests : List Request)
    (requestId : RequestId) :
    List.Sublist (removeRequest requests requestId) requests := by
  induction requests with
  | nil => simp [removeRequest]
  | cons request remaining inductionHypothesis =>
      by_cases same : request.id = requestId
      · simp [removeRequest, same]
      · simp [removeRequest, same, inductionHypothesis]

theorem removeRequest_length_of_find_some {requests : List Request}
    {requestId : RequestId} {request : Request}
    (found : findRequest requests requestId = some request) :
    (removeRequest requests requestId).length + 1 = requests.length := by
  induction requests with
  | nil => simp [findRequest] at found
  | cons head remaining inductionHypothesis =>
      by_cases same : head.id = requestId
      · simp [findRequest, removeRequest, same] at found ⊢
      · simp [findRequest, removeRequest, same] at found ⊢
        exact inductionHypothesis found

theorem cancelAll_length (requests : List Request) :
    (cancelAll requests).length = requests.length := by
  simp [cancelAll]

theorem cancelAll_ids (requests : List Request) :
    (cancelAll requests).map Request.id = requests.map Request.id := by
  simp [cancelAll]

theorem cancelAll_leases (requests : List Request) :
    (cancelAll requests).map Request.lease = requests.map Request.lease := by
  simp [cancelAll]

theorem trySubmit_preserves_valid (state : State) (lease : LeaseId)
    (valid : state.Valid) : (trySubmit state lease).state.Valid := by
  rcases valid with ⟨capacityPositive, requestsBounded, commandsValid⟩
  cases terminal : state.terminal with
  | closing =>
      simp [trySubmit, terminal, State.Valid, capacityPositive,
        requestsBounded, commandsValid]
  | running =>
      by_cases leaseUsed : containsLease state.requests lease
      · simp [trySubmit, terminal, leaseUsed, State.Valid, capacityPositive,
          requestsBounded, commandsValid]
      · by_cases hasCapacity : state.requests.length < state.capacity
        · cases publication :
            BoundedMpsc.tryPublish state.commands (.submit state.nextId) with
          | mk admission commandsAfter =>
              have commandsAfterValid : commandsAfter.Valid := by
                have preserved := BoundedMpsc.tryPublish_preserves_valid
                  state.commands (.submit state.nextId) commandsValid
                simpa [publication] using preserved
              cases admission with
              | accepted =>
                  simp [trySubmit, terminal, leaseUsed, hasCapacity,
                    publication, State.Valid, List.length_append,
                    capacityPositive, Nat.succ_le_of_lt hasCapacity,
                    commandsAfterValid]
              | full | closed =>
                  simp [trySubmit, terminal, leaseUsed, hasCapacity,
                    publication, State.Valid, capacityPositive,
                    requestsBounded, commandsValid]
        · simp [trySubmit, terminal, leaseUsed, hasCapacity, State.Valid,
            capacityPositive, requestsBounded, commandsValid]

theorem trySubmit_preserves_ownership (state : State) (lease : LeaseId)
    (valid : state.OwnershipValid) :
    (trySubmit state lease).state.OwnershipValid := by
  rcases valid with
    ⟨idsUnique, leasesUnique, idsBounded, nextIdPositive⟩
  have originalValid : state.OwnershipValid :=
    ⟨idsUnique, leasesUnique, idsBounded, nextIdPositive⟩
  cases terminal : state.terminal with
  | closing =>
      simpa only [trySubmit, terminal] using originalValid
  | running =>
      by_cases leaseUsed : containsLease state.requests lease
      · simpa only [trySubmit, terminal, leaseUsed, ↓reduceIte] using
          originalValid
      · by_cases hasCapacity : state.requests.length < state.capacity
        · cases publication :
            BoundedMpsc.tryPublish state.commands (.submit state.nextId) with
          | mk admission commandsAfter =>
              cases admission with
              | full | closed =>
                  simpa [trySubmit, terminal, leaseUsed, hasCapacity,
                    publication] using originalValid
              | accepted =>
                  let newRequest : Request :=
                    { id := state.nextId, lease := lease,
                      phase := .admitted }
                  have idNotUsed :
                      ∀ old ∈ state.requests.map Request.id,
                        old ≠ state.nextId := by
                    intro old member
                    exact Nat.ne_of_lt (idsBounded old member)
                  have newIdsUnique :
                      (state.requests.map Request.id ++ [state.nextId]).Nodup := by
                    rw [List.nodup_append]
                    exact ⟨idsUnique, by simp, by simpa using idNotUsed⟩
                  have leaseNotUsed :
                      lease ∉ state.requests.map Request.lease :=
                    (containsLease_false_iff state.requests lease).mp
                      (by simpa using leaseUsed)
                  have newLeasesUnique :
                      (state.requests.map Request.lease ++ [lease]).Nodup := by
                    rw [List.nodup_append]
                    exact ⟨leasesUnique, by simp, by
                      intro old oldMember fresh freshMember
                      simp only [List.mem_singleton] at freshMember
                      subst fresh
                      exact fun equality => leaseNotUsed (by
                        simpa [equality] using oldMember)⟩
                  have newIdsBounded :
                      ∀ requestId ∈
                          state.requests.map Request.id ++ [state.nextId],
                        requestId < state.nextId + 1 := by
                    intro requestId member
                    simp only [List.mem_append, List.mem_singleton] at member
                    rcases member with oldMember | rfl
                    · exact Nat.lt_succ_of_lt (idsBounded requestId oldMember)
                    · exact Nat.lt_succ_self _
                  simp only [trySubmit, terminal, leaseUsed, hasCapacity,
                    publication, ↓reduceIte]
                  rw [State.OwnershipValid]
                  refine ⟨?_, ?_, ?_, Nat.zero_lt_succ state.nextId⟩
                  · change
                      ((state.requests ++ [newRequest]).map Request.id).Nodup
                    simpa [List.map_append, newRequest] using newIdsUnique
                  · change
                      ((state.requests ++ [newRequest]).map
                        Request.lease).Nodup
                    simpa [List.map_append, newRequest] using newLeasesUnique
                  · simpa [List.map_append, newRequest] using newIdsBounded
        · simp only [trySubmit, terminal, leaseUsed, hasCapacity,
            ↓reduceIte]
          exact ⟨idsUnique, leasesUnique, idsBounded, nextIdPositive⟩

theorem trySubmit_preserves_lifecycle (state : State) (lease : LeaseId)
    (valid : state.LifecycleValid) :
    (trySubmit state lease).state.LifecycleValid := by
  cases actorTerminal : state.terminal with
  | closing => simpa [trySubmit, actorTerminal] using valid
  | running =>
      have mailboxOpen : state.commands.terminal = .open := by
        simpa [State.LifecycleValid, actorTerminal] using valid
      by_cases leaseUsed : containsLease state.requests lease
      · simp [trySubmit, actorTerminal, leaseUsed, State.LifecycleValid,
          mailboxOpen]
      · by_cases requestCapacity : state.requests.length < state.capacity
        · by_cases commandCapacity :
              state.commands.queue.length < state.commands.capacity <;>
            simp [trySubmit, actorTerminal, leaseUsed, requestCapacity,
              BoundedMpsc.tryPublish, mailboxOpen, commandCapacity,
              State.LifecycleValid]
        · simp [trySubmit, actorTerminal, leaseUsed, requestCapacity,
            State.LifecycleValid, mailboxOpen]

theorem failed_submit_unchanged (state : State) (lease : LeaseId)
    (rejected : (trySubmit state lease).status ≠ .accepted) :
    (trySubmit state lease).state = state := by
  cases terminal : state.terminal with
  | closing => simp [trySubmit, terminal]
  | running =>
      by_cases leaseUsed : containsLease state.requests lease
      · simp [trySubmit, terminal, leaseUsed]
      · by_cases hasCapacity : state.requests.length < state.capacity
        · cases publication :
            BoundedMpsc.tryPublish state.commands (.submit state.nextId) with
          | mk admission commandsAfter =>
              cases admission <;>
                simp [trySubmit, terminal, leaseUsed, hasCapacity,
                  publication] at rejected ⊢
        · simp [trySubmit, terminal, leaseUsed, hasCapacity]

theorem tryCancel_preserves_valid (state : State) (requestId : RequestId)
    (valid : state.Valid) : (tryCancel state requestId).state.Valid := by
  rcases valid with ⟨capacityPositive, requestsBounded, commandsValid⟩
  cases found : findRequest state.requests requestId with
  | none =>
      simp [tryCancel, found, State.Valid, capacityPositive,
        requestsBounded, commandsValid]
  | some request =>
      cases publication :
          BoundedMpsc.tryPublish state.commands (.cancel requestId) with
      | mk admission commandsAfter =>
          have commandsAfterValid : commandsAfter.Valid := by
            have preserved := BoundedMpsc.tryPublish_preserves_valid
              state.commands (.cancel requestId) commandsValid
            simpa [publication] using preserved
          cases admission with
          | accepted =>
              simp [tryCancel, found, publication, State.Valid,
                capacityPositive, requestsBounded, commandsAfterValid]
          | full | closed =>
              simp [tryCancel, found, publication, State.Valid,
                capacityPositive, requestsBounded, commandsValid]

theorem tryCancel_preserves_ownership (state : State)
    (requestId : RequestId) (valid : state.OwnershipValid) :
    (tryCancel state requestId).state.OwnershipValid := by
  cases found : findRequest state.requests requestId with
  | none => simpa [tryCancel, found] using valid
  | some request =>
      cases publication :
          BoundedMpsc.tryPublish state.commands (.cancel requestId) with
      | mk admission commandsAfter =>
          cases admission <;>
            simpa [tryCancel, found, publication, State.OwnershipValid]
              using valid

theorem active_requests_are_completion_credits (state : State)
    (valid : state.Valid) : state.requests.length ≤ state.capacity :=
  valid.2.1

theorem nodup_ids_same_request {requests : List Request}
    (idsUnique : (requests.map Request.id).Nodup)
    {left right : Request} (leftMember : left ∈ requests)
    (rightMember : right ∈ requests) (sameId : left.id = right.id) :
    left = right := by
  induction requests with
  | nil => simp at leftMember
  | cons head remaining inductionHypothesis =>
      simp only [List.map_cons, List.nodup_cons] at idsUnique
      rcases idsUnique with ⟨headFresh, remainingUnique⟩
      simp only [List.mem_cons] at leftMember rightMember
      rcases leftMember with rfl | leftRemaining
      · rcases rightMember with rfl | rightRemaining
        · rfl
        · exact False.elim (headFresh (List.mem_map.mpr
            ⟨right, rightRemaining, sameId.symm⟩))
      · rcases rightMember with rfl | rightRemaining
        · exact False.elim (headFresh (List.mem_map.mpr
            ⟨left, leftRemaining, sameId⟩))
        · exact inductionHypothesis remainingUnique leftRemaining rightRemaining

theorem completion_credit_is_exactly_one (state : State)
    (requestId : RequestId) (credit : HasCompletionCredit state requestId) :
    ∃ request ∈ state.requests, request.id = requestId ∧
      ∀ other ∈ state.requests, other.id = requestId → other = request := by
  rcases credit with ⟨ownership, request, member, requestIdEqual⟩
  refine ⟨request, member, requestIdEqual, ?_⟩
  intro other otherMember otherId
  exact nodup_ids_same_request ownership.1 otherMember member
    (otherId.trans requestIdEqual.symm)

theorem accepted_submit_has_completion_credit (before after : State)
    (lease : LeaseId) (requestId : RequestId)
    (valid : before.OwnershipValid)
    (transition : trySubmit before lease =
      { status := .accepted, requestId := some requestId, state := after }) :
    HasCompletionCredit after requestId := by
  have ownershipAfter : after.OwnershipValid := by
    have preserved := trySubmit_preserves_ownership before lease valid
    rw [transition] at preserved
    exact preserved
  refine ⟨ownershipAfter, ?_⟩
  cases terminal : before.terminal with
  | closing => simp [trySubmit, terminal] at transition
  | running =>
      by_cases leaseUsed : containsLease before.requests lease
      · simp [trySubmit, terminal, leaseUsed] at transition
      · by_cases hasCapacity : before.requests.length < before.capacity
        · cases publication :
            BoundedMpsc.tryPublish before.commands (.submit before.nextId) with
          | mk admission commandsAfter =>
              cases admission with
              | full | closed =>
                  simp [trySubmit, terminal, leaseUsed, hasCapacity,
                    publication] at transition
              | accepted =>
                  simp [trySubmit, terminal, leaseUsed, hasCapacity,
                    publication] at transition
                  rcases transition with ⟨rfl, rfl⟩
                  simp
        · simp [trySubmit, terminal, leaseUsed, hasCapacity] at transition

theorem processOne_preserves_valid (state : State) (valid : state.Valid) :
    (processOne state).state.Valid := by
  rcases valid with ⟨capacityPositive, requestsBounded, commandsValid⟩
  cases consumed : BoundedMpsc.tryConsume state.commands with
  | mk observation commandsAfter =>
      have commandsAfterValid : commandsAfter.Valid := by
        have preserved := BoundedMpsc.tryConsume_preserves_valid
          state.commands commandsValid
        simpa [consumed] using preserved
      cases observation with
      | empty | closed =>
          simp [processOne, consumed, State.Valid, capacityPositive,
            requestsBounded, commandsValid]
      | item command =>
          cases command with
          | submit requestId | cancel requestId =>
              simp [processOne, consumed, State.Valid, capacityPositive,
                requestsBounded, commandsAfterValid, modifyPhase_length]

theorem processOne_preserves_ownership (state : State)
    (valid : state.OwnershipValid) :
    (processOne state).state.OwnershipValid := by
  cases consumed : BoundedMpsc.tryConsume state.commands with
  | mk observation commandsAfter =>
      cases observation with
      | empty | closed => simpa [processOne, consumed] using valid
      | item command =>
          cases command with
          | submit requestId | cancel requestId =>
              simpa [processOne, consumed, State.OwnershipValid,
                modifyPhase_ids, modifyPhase_leases] using valid

theorem beginBackend_preserves_valid (state : State) (requestId : RequestId)
    (valid : state.Valid) : (beginBackend state requestId).state.Valid := by
  cases terminal : state.terminal with
  | closing => simpa [beginBackend, terminal] using valid
  | running =>
      cases found : findRequest state.requests requestId with
      | none => simpa [beginBackend, terminal, found] using valid
      | some request =>
          cases phase : request.phase <;>
            simpa [beginBackend, terminal, found, phase, State.Valid,
              modifyPhase_length] using valid

theorem beginBackend_preserves_ownership (state : State)
    (requestId : RequestId) (valid : state.OwnershipValid) :
    (beginBackend state requestId).state.OwnershipValid := by
  cases terminal : state.terminal with
  | closing => simpa [beginBackend, terminal] using valid
  | running =>
      cases found : findRequest state.requests requestId with
      | none => simpa [beginBackend, terminal, found] using valid
      | some request =>
          cases phase : request.phase <;>
            simpa [beginBackend, terminal, found, phase,
              State.OwnershipValid, modifyPhase_ids,
              modifyPhase_leases] using valid

theorem backendComplete_preserves_valid (state : State)
    (requestId : RequestId) (result : Completion) (valid : state.Valid) :
    (backendComplete state requestId result).state.Valid := by
  cases found : findRequest state.requests requestId with
  | none => simpa [backendComplete, found] using valid
  | some request =>
      cases phase : request.phase <;>
        simpa [backendComplete, found, phase, State.Valid,
          modifyPhase_length] using valid

theorem backendComplete_preserves_ownership (state : State)
    (requestId : RequestId) (result : Completion)
    (valid : state.OwnershipValid) :
    (backendComplete state requestId result).state.OwnershipValid := by
  cases found : findRequest state.requests requestId with
  | none => simpa [backendComplete, found] using valid
  | some request =>
      cases phase : request.phase <;>
        simpa [backendComplete, found, phase, State.OwnershipValid,
          modifyPhase_ids, modifyPhase_leases] using valid

theorem backend_terminal_is_stable (state : State) (request : Request)
    (found : findRequest state.requests request.id = some request)
    (terminal : (∃ result, request.phase = .completed result) ∨
      (∃ taskId result, request.phase = .dispatchQueued taskId result))
    (result : Completion) :
    (backendComplete state request.id result).status = .notPending ∧
      (backendComplete state request.id result).state = state := by
  rcases terminal with ⟨oldResult, phase⟩ | ⟨taskId, oldResult, phase⟩ <;>
    simp [backendComplete, found, phase]

theorem dispatch_full_preserves_actor (actor : State)
    (executor : Executor.State RequestId)
    (full : (tryDispatch actor executor).status = .full) :
    (tryDispatch actor executor).actor = actor := by
  cases completed : firstCompleted actor.requests with
  | none => simp [tryDispatch, completed] at full
  | some selected =>
      rcases selected with ⟨request, result⟩
      cases postedStatus : (Executor.tryPost executor request.id).status <;>
        cases postedId : (Executor.tryPost executor request.id).taskId <;>
        simp [tryDispatch, completed, postedStatus, postedId] at full ⊢

theorem dispatch_rejected_preserves_executor (actor : State)
    (executor : Executor.State RequestId) (status : DispatchStatus)
    (rejected : status ≠ .accepted)
    (resultStatus : (tryDispatch actor executor).status = status) :
    (tryDispatch actor executor).executor = executor := by
  cases completed : firstCompleted actor.requests with
  | none => simp [tryDispatch, completed]
  | some selected =>
      rcases selected with ⟨request, result⟩
      cases posted : Executor.tryPost executor request.id with
      | mk postStatus taskId executorAfter =>
          cases postStatus <;> cases taskId <;>
            simp [tryDispatch, completed, posted] at resultStatus ⊢
          exact False.elim (rejected resultStatus.symm)

theorem tryDispatch_preserves_actor_valid (actor : State)
    (executor : Executor.State RequestId) (valid : actor.Valid) :
    (tryDispatch actor executor).actor.Valid := by
  cases completed : firstCompleted actor.requests with
  | none => simpa [tryDispatch, completed] using valid
  | some selected =>
      rcases selected with ⟨request, result⟩
      cases posted : Executor.tryPost executor request.id with
      | mk status taskId executorAfter =>
          cases status <;> cases taskId <;>
            simpa [tryDispatch, completed, posted, State.Valid,
              modifyPhase_length] using valid

theorem tryDispatch_preserves_actor_ownership (actor : State)
    (executor : Executor.State RequestId) (valid : actor.OwnershipValid) :
    (tryDispatch actor executor).actor.OwnershipValid := by
  cases completed : firstCompleted actor.requests with
  | none => simpa [tryDispatch, completed] using valid
  | some selected =>
      rcases selected with ⟨request, result⟩
      cases posted : Executor.tryPost executor request.id with
      | mk status taskId executorAfter =>
          cases status <;> cases taskId <;>
            simpa [tryDispatch, completed, posted, State.OwnershipValid,
              modifyPhase_ids, modifyPhase_leases] using valid

theorem observeDispatchStart_preserves_valid (state : State)
    (started : Option (Executor.Task RequestId)) (valid : state.Valid) :
    (observeDispatchStart state started).state.Valid := by
  cases started with
  | none => simpa [observeDispatchStart] using valid
  | some task =>
      cases found : findRequest state.requests task.payload with
      | none => simpa [observeDispatchStart, found] using valid
      | some request =>
          cases phase : request.phase with
          | dispatchQueued taskId result =>
              by_cases sameTask : taskId = task.id <;>
                simpa [observeDispatchStart, found, phase, sameTask,
                  State.Valid, modifyPhase_length] using valid
          | admitted | ready | backendPending | completed | dispatchRunning |
              delivered =>
              simpa [observeDispatchStart, found, phase] using valid

theorem observeDispatchStart_preserves_ownership (state : State)
    (started : Option (Executor.Task RequestId))
    (valid : state.OwnershipValid) :
    (observeDispatchStart state started).state.OwnershipValid := by
  cases started with
  | none => simpa [observeDispatchStart] using valid
  | some task =>
      cases found : findRequest state.requests task.payload with
      | none => simpa [observeDispatchStart, found] using valid
      | some request =>
          cases phase : request.phase with
          | dispatchQueued taskId result =>
              by_cases sameTask : taskId = task.id <;>
                simpa [observeDispatchStart, found, phase, sameTask,
                  State.OwnershipValid, modifyPhase_ids,
                  modifyPhase_leases] using valid
          | admitted | ready | backendPending | completed | dispatchRunning |
              delivered =>
              simpa [observeDispatchStart, found, phase] using valid

theorem observeDispatchFinish_preserves_valid (state : State) (taskId : Nat)
    (finishStatus : Executor.FinishStatus) (valid : state.Valid) :
    (observeDispatchFinish state taskId finishStatus).state.Valid := by
  cases finishStatus with
  | notFound => simpa [observeDispatchFinish] using valid
  | finished =>
      cases found : firstDispatchRunning state.requests taskId with
      | none => simpa [observeDispatchFinish, found] using valid
      | some request =>
          cases phase : request.phase <;>
            simpa [observeDispatchFinish, found, phase, State.Valid,
              modifyPhase_length] using valid

theorem observeDispatchFinish_preserves_ownership (state : State)
    (taskId : Nat) (finishStatus : Executor.FinishStatus)
    (valid : state.OwnershipValid) :
    (observeDispatchFinish state taskId finishStatus).state.OwnershipValid := by
  cases finishStatus with
  | notFound => simpa [observeDispatchFinish] using valid
  | finished =>
      cases found : firstDispatchRunning state.requests taskId with
      | none => simpa [observeDispatchFinish, found] using valid
      | some request =>
          cases phase : request.phase <;>
            simpa [observeDispatchFinish, found, phase, State.OwnershipValid,
              modifyPhase_ids, modifyPhase_leases] using valid

theorem startDelivery_preserves_actor_valid (actor : State)
    (executor : Executor.State RequestId) (valid : actor.Valid) :
    (startDelivery actor executor).actor.Valid := by
  unfold startDelivery
  dsimp only
  split
  · exact observeDispatchStart_preserves_valid actor
      (Executor.start executor).task valid
  · exact valid

theorem startDelivery_preserves_actor_ownership (actor : State)
    (executor : Executor.State RequestId) (valid : actor.OwnershipValid) :
    (startDelivery actor executor).actor.OwnershipValid := by
  unfold startDelivery
  dsimp only
  split
  · exact observeDispatchStart_preserves_ownership actor
      (Executor.start executor).task valid
  · exact valid

theorem startDelivery_preserves_executor_valid (actor : State)
    (executor : Executor.State RequestId) (valid : executor.Valid) :
    (startDelivery actor executor).executor.Valid := by
  unfold startDelivery
  dsimp only
  split
  · exact Executor.start_preserves_valid executor valid
  · exact valid

theorem startDelivery_preserves_executor_identifiers (actor : State)
    (executor : Executor.State RequestId) (valid : executor.IdentifiersValid) :
    (startDelivery actor executor).executor.IdentifiersValid := by
  unfold startDelivery
  dsimp only
  split
  · exact Executor.start_preserves_identifiers executor valid
  · exact valid

theorem finishDelivery_preserves_actor_valid (actor : State)
    (executor : Executor.State RequestId) (taskId : Nat)
    (valid : actor.Valid) :
    (finishDelivery actor executor taskId).actor.Valid := by
  unfold finishDelivery
  dsimp only
  split
  · exact observeDispatchFinish_preserves_valid actor taskId
      (Executor.finish executor taskId).status valid
  · exact valid

theorem finishDelivery_preserves_actor_ownership (actor : State)
    (executor : Executor.State RequestId) (taskId : Nat)
    (valid : actor.OwnershipValid) :
    (finishDelivery actor executor taskId).actor.OwnershipValid := by
  unfold finishDelivery
  dsimp only
  split
  · exact observeDispatchFinish_preserves_ownership actor taskId
      (Executor.finish executor taskId).status valid
  · exact valid

theorem finishDelivery_preserves_executor_valid (actor : State)
    (executor : Executor.State RequestId) (taskId : Nat)
    (valid : executor.Valid) :
    (finishDelivery actor executor taskId).executor.Valid := by
  unfold finishDelivery
  dsimp only
  split
  · exact Executor.finish_preserves_valid executor taskId valid
  · exact valid

theorem finishDelivery_preserves_executor_identifiers (actor : State)
    (executor : Executor.State RequestId) (taskId : Nat)
    (valid : executor.IdentifiersValid) :
    (finishDelivery actor executor taskId).executor.IdentifiersValid := by
  unfold finishDelivery
  dsimp only
  split
  · exact Executor.finish_preserves_identifiers executor taskId valid
  · exact valid

theorem acknowledge_dispatchQueued_retains (state : State)
    (requestId taskId : Nat) (result : Completion)
    (phase : (findRequest state.requests requestId).map Request.phase =
      some (.dispatchQueued taskId result)) :
    (acknowledge state requestId).state = state := by
  cases found : findRequest state.requests requestId with
  | none => simp [found] at phase
  | some request =>
      have requestPhase : request.phase = .dispatchQueued taskId result := by
        simpa [found] using phase
      simp [acknowledge, found, requestPhase]

theorem acknowledge_dispatchRunning_retains (state : State)
    (requestId taskId : Nat) (result : Completion)
    (phase : (findRequest state.requests requestId).map Request.phase =
      some (.dispatchRunning taskId result)) :
    (acknowledge state requestId).state = state := by
  cases found : findRequest state.requests requestId with
  | none => simp [found] at phase
  | some request =>
      have requestPhase : request.phase = .dispatchRunning taskId result := by
        simpa [found] using phase
      simp [acknowledge, found, requestPhase]

theorem acknowledge_releases_sublist (state : State) (requestId : RequestId) :
    List.Sublist (acknowledge state requestId).state.requests state.requests := by
  cases found : findRequest state.requests requestId with
  | none => simp [acknowledge, found]
  | some request =>
      cases phase : request.phase <;>
        simp [acknowledge, found, phase, removeRequest_sublist]

theorem acknowledge_released_removes_one (state before : State)
    (requestId : RequestId)
    (transition : acknowledge state requestId =
      { status := .released, state := before }) :
    before.requests.length + 1 = state.requests.length := by
  cases found : findRequest state.requests requestId with
  | none => simp [acknowledge, found] at transition
  | some request =>
      cases phase : request.phase <;>
        simp [acknowledge, found, phase] at transition
      rename_i result
      subst before
      exact removeRequest_length_of_find_some found

theorem acknowledge_preserves_valid (state : State) (requestId : RequestId)
    (valid : state.Valid) : (acknowledge state requestId).state.Valid := by
  rcases valid with ⟨capacityPositive, requestsBounded, commandsValid⟩
  cases found : findRequest state.requests requestId with
  | none => simp [acknowledge, found, State.Valid, capacityPositive,
      requestsBounded, commandsValid]
  | some request =>
      cases phase : request.phase with
      | delivered result =>
          simp only [acknowledge, found, phase]
          exact ⟨capacityPositive,
            Nat.le_trans
              (removeRequest_sublist state.requests requestId).length_le
              requestsBounded,
            commandsValid⟩
      | admitted | ready | backendPending | completed | dispatchQueued |
          dispatchRunning =>
          simpa [acknowledge, found, phase, State.Valid] using
            ⟨capacityPositive, requestsBounded, commandsValid⟩

theorem acknowledge_preserves_ownership (state : State)
    (requestId : RequestId) (valid : state.OwnershipValid) :
    (acknowledge state requestId).state.OwnershipValid := by
  cases found : findRequest state.requests requestId with
  | none => simpa [acknowledge, found] using valid
  | some request =>
      cases phase : request.phase with
      | delivered result =>
          rcases valid with
            ⟨idsUnique, leasesUnique, idsBounded, nextIdPositive⟩
          have sublist := removeRequest_sublist state.requests requestId
          simp only [acknowledge, found, phase]
          exact ⟨(sublist.map Request.id).nodup idsUnique,
            (sublist.map Request.lease).nodup leasesUnique,
            fun existing member =>
              idsBounded existing ((sublist.map Request.id).subset member),
            nextIdPositive⟩
      | admitted | ready | backendPending | completed | dispatchQueued |
          dispatchRunning =>
          simpa [acknowledge, found, phase] using valid

theorem close_preserves_valid (state : State) (valid : state.Valid) :
    (close state).state.Valid := by
  cases terminal : state.terminal with
  | closing => simpa [close, terminal] using valid
  | running =>
      rcases valid with ⟨capacityPositive, requestsBounded, commandsValid⟩
      simp only [close, terminal]
      exact ⟨capacityPositive, by simpa [cancelAll_length] using requestsBounded,
        BoundedMpsc.close_preserves_valid state.commands commandsValid⟩

theorem close_preserves_ownership (state : State)
    (valid : state.OwnershipValid) :
    (close state).state.OwnershipValid := by
  cases terminal : state.terminal <;>
    simpa [close, terminal, State.OwnershipValid, cancelAll_ids,
      cancelAll_leases] using valid

theorem close_preserves_lifecycle (state : State)
    (valid : state.LifecycleValid) : (close state).state.LifecycleValid := by
  cases actorTerminal : state.terminal <;>
    cases mailboxTerminal : state.commands.terminal <;>
      simp [close, actorTerminal, State.LifecycleValid, mailboxTerminal,
        BoundedMpsc.close] at valid ⊢

theorem close_rejects_submit (state : State) (lease : LeaseId) :
    (trySubmit (close state).state lease).status = .closed := by
  cases terminal : state.terminal <;> simp [close, trySubmit, terminal]

end CMetaCFlowCalculus.IO.Actor
