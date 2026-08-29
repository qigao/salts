import CMetaCFlowCalculus.IO.ReadinessDriver
import CMetaCFlowCalculus.IO.CompletionDriver
import CMetaCFlowCalculus.IO.BlockingDriver
import CMetaCFlowCalculus.Proofs.IOBoundedMpsc

namespace CMetaCFlowCalculus.IO.Communication

theorem publish_preserves_terminal (state : BoundedMpsc.State α)
    (value : α) :
    (BoundedMpsc.tryPublish state value).2.terminal = state.terminal := by
  cases terminal : state.terminal with
  | draining => simp [BoundedMpsc.tryPublish, terminal]
  | «open» =>
      by_cases hasCapacity : state.queue.length < state.capacity <;>
        simp [BoundedMpsc.tryPublish, terminal, hasCapacity]

theorem eraseIdentity_sublist (pending : List Identity) (identity : Identity) :
    List.Sublist (eraseIdentity pending identity) pending := by
  induction pending with
  | nil => simp [eraseIdentity]
  | cons current remaining inductionHypothesis =>
      by_cases same : current = identity
      · simp [eraseIdentity, same]
      · simp [eraseIdentity, same, inductionHypothesis]

theorem eraseIdentity_preserves_nodup (pending : List Identity)
    (identity : Identity) (unique : pending.Nodup) :
    (eraseIdentity pending identity).Nodup :=
  (eraseIdentity_sublist pending identity).nodup unique

theorem eraseIdentity_length_of_mem (pending : List Identity)
    (identity : Identity) (member : identity ∈ pending) :
    (eraseIdentity pending identity).length + 1 = pending.length := by
  induction pending with
  | nil => simp at member
  | cons current remaining inductionHypothesis =>
      by_cases same : current = identity
      · simp [eraseIdentity, same]
      · have remainingMember : identity ∈ remaining := by
          simp only [List.mem_cons] at member
          cases member with
          | inl headEqual => exact False.elim (same headEqual.symm)
          | inr tailMember => exact tailMember
        simp [eraseIdentity, same,
          inductionHypothesis remainingMember]

theorem tryAdmit_preserves_valid (state : Contract) (identity : Identity)
    (valid : state.Valid) : (tryAdmit state identity).state.Valid := by
  rcases valid with
    ⟨capacityPositive, pendingBounded, pendingUnique, commandsValid,
      eventsValid, lifecycleValid⟩
  by_cases identityValid : identity.Valid
  · cases lifecycle : state.lifecycle with
    | draining =>
        have commandsDraining :
            state.commands.terminal = BoundedMpsc.Terminal.draining := by
          simpa [lifecycle] using lifecycleValid
        simp [tryAdmit, identityValid, lifecycle, Contract.Valid,
          capacityPositive, pendingBounded, pendingUnique, commandsValid,
          eventsValid, commandsDraining]
    | «open» =>
        have commandsOpen :
            state.commands.terminal = BoundedMpsc.Terminal.open := by
          simpa [lifecycle] using lifecycleValid
        by_cases duplicate : identity ∈ state.pending
        · simp [tryAdmit, identityValid, lifecycle, duplicate,
            Contract.Valid, capacityPositive, pendingBounded,
            pendingUnique, commandsValid, eventsValid, commandsOpen]
        · by_cases pendingCapacity :
            state.pending.length < state.capacity
          · cases publication :
                BoundedMpsc.tryPublish state.commands identity with
            | mk admission commandsAfter =>
                have commandsAfterValid : commandsAfter.Valid := by
                  simpa [publication] using
                    BoundedMpsc.tryPublish_preserves_valid
                      state.commands identity commandsValid
                have commandsTerminal :
                    commandsAfter.terminal = BoundedMpsc.Terminal.open := by
                  have preserved := publish_preserves_terminal
                    state.commands identity
                  rw [publication] at preserved
                  exact preserved.trans commandsOpen
                have appendedUnique :
                    (state.pending ++ [identity]).Nodup := by
                  rw [List.nodup_append]
                  refine ⟨pendingUnique, by simp, ?_⟩
                  intro current currentMember appended appendedMember
                  simp only [List.mem_singleton] at appendedMember
                  subst appended
                  exact fun currentEqual =>
                    duplicate (currentEqual ▸ currentMember)
                cases admission with
                | accepted =>
                    simp only [tryAdmit, identityValid,
                      lifecycle, duplicate, pendingCapacity, publication]
                    exact ⟨capacityPositive,
                      by simpa using Nat.succ_le_of_lt pendingCapacity,
                      appendedUnique,
                      commandsAfterValid, eventsValid, commandsTerminal⟩
                | full | closed =>
                    simpa [tryAdmit, identityValid, lifecycle, duplicate,
                      pendingCapacity, publication, Contract.Valid] using
                        ⟨capacityPositive, pendingBounded, pendingUnique,
                          commandsValid, eventsValid, commandsOpen⟩
          · simpa [tryAdmit, identityValid, lifecycle, duplicate,
              pendingCapacity, Contract.Valid] using
                ⟨capacityPositive, pendingBounded, pendingUnique,
                  commandsValid, eventsValid, commandsOpen⟩
  · simpa [tryAdmit, identityValid, Contract.Valid] using
      ⟨capacityPositive, pendingBounded, pendingUnique, commandsValid,
        eventsValid, lifecycleValid⟩

theorem accepted_admission_adds_once {before after : Contract}
    {identity : Identity}
    (transition : tryAdmit before identity =
      { status := .accepted, state := after }) :
    after.pending = before.pending ++ [identity] ∧
      after.commands.queue = before.commands.queue ++ [identity] := by
  by_cases identityValid : identity.Valid
  · cases lifecycle : before.lifecycle with
    | draining => simp [tryAdmit, identityValid, lifecycle] at transition
    | «open» =>
        by_cases duplicate : identity ∈ before.pending
        · simp [tryAdmit, identityValid, lifecycle, duplicate] at transition
        · by_cases pendingCapacity :
            before.pending.length < before.capacity
          · cases publication :
                BoundedMpsc.tryPublish before.commands identity with
            | mk admission commandsAfter =>
                cases admission with
                | accepted =>
                    simp [tryAdmit, identityValid, lifecycle, duplicate,
                      pendingCapacity, publication] at transition
                    subst after
                    exact ⟨rfl,
                      (BoundedMpsc.accepted_appends_once publication).1⟩
                | full | closed =>
                    simp [tryAdmit, identityValid, lifecycle, duplicate,
                      pendingCapacity, publication] at transition
          · simp [tryAdmit, identityValid, lifecycle, duplicate,
              pendingCapacity] at transition
  · simp [tryAdmit, identityValid] at transition

theorem rejected_admission_unchanged (state : Contract)
    (identity : Identity)
    (rejected : (tryAdmit state identity).status ≠ .accepted) :
    (tryAdmit state identity).state = state := by
  by_cases identityValid : identity.Valid
  · cases lifecycle : state.lifecycle with
    | draining => simp [tryAdmit, identityValid, lifecycle]
    | «open» =>
        by_cases duplicate : identity ∈ state.pending
        · simp [tryAdmit, identityValid, lifecycle, duplicate]
        · by_cases pendingCapacity :
            state.pending.length < state.capacity
          · cases publication :
                BoundedMpsc.tryPublish state.commands identity with
            | mk admission commandsAfter =>
                cases admission with
                | accepted =>
                    simp [tryAdmit, identityValid, lifecycle, duplicate,
                      pendingCapacity, publication] at rejected
                | full | closed =>
                    simp [tryAdmit, identityValid, lifecycle, duplicate,
                      pendingCapacity, publication]
          · simp [tryAdmit, identityValid, lifecycle, duplicate,
              pendingCapacity]
  · simp [tryAdmit, identityValid]

theorem tryTerminate_preserves_valid (state : Contract) (event : Event)
    (valid : state.Valid) : (tryTerminate state event).state.Valid := by
  rcases valid with
    ⟨capacityPositive, pendingBounded, pendingUnique, commandsValid,
      eventsValid, lifecycleValid⟩
  by_cases eventValid : event.Valid
  · by_cases present : event.identity ∈ state.pending
    · cases publication : BoundedMpsc.tryPublish state.events event with
      | mk admission eventsAfter =>
          have eventsAfterValid : eventsAfter.Valid := by
            simpa [publication] using
              BoundedMpsc.tryPublish_preserves_valid
                state.events event eventsValid
          cases admission with
          | accepted =>
              simp only [tryTerminate, eventValid,
                present, publication]
              exact ⟨capacityPositive,
                Nat.le_trans
                  (eraseIdentity_sublist state.pending event.identity).length_le
                  pendingBounded,
                eraseIdentity_preserves_nodup state.pending event.identity
                  pendingUnique,
                commandsValid, eventsAfterValid, lifecycleValid⟩
          | full | closed =>
              simpa [tryTerminate, eventValid, present, publication,
                Contract.Valid] using
                  ⟨capacityPositive, pendingBounded, pendingUnique,
                    commandsValid, eventsValid, lifecycleValid⟩
    · simpa [tryTerminate, eventValid, present, Contract.Valid] using
        ⟨capacityPositive, pendingBounded, pendingUnique, commandsValid,
          eventsValid, lifecycleValid⟩
  · simpa [tryTerminate, eventValid, Contract.Valid] using
      ⟨capacityPositive, pendingBounded, pendingUnique, commandsValid,
        eventsValid, lifecycleValid⟩

theorem accepted_terminal_releases_once {before after : Contract}
    {event : Event}
    (transition : tryTerminate before event =
      { status := .accepted, state := after }) :
    after.pending = eraseIdentity before.pending event.identity ∧
      after.events.queue = before.events.queue ++ [event] := by
  by_cases eventValid : event.Valid
  · by_cases present : event.identity ∈ before.pending
    · cases publication : BoundedMpsc.tryPublish before.events event with
      | mk admission eventsAfter =>
          cases admission with
          | accepted =>
              simp [tryTerminate, eventValid, present, publication] at transition
              subst after
              exact ⟨rfl,
                (BoundedMpsc.accepted_appends_once publication).1⟩
          | full | closed =>
              simp [tryTerminate, eventValid, present, publication] at transition
    · simp [tryTerminate, eventValid, present] at transition
  · simp [tryTerminate, eventValid] at transition

theorem stale_terminal_unchanged (state : Contract) (event : Event)
    (stale : event.identity ∉ state.pending) :
    (tryTerminate state event).state = state := by
  by_cases eventValid : event.Valid <;>
    simp [tryTerminate, eventValid, stale]

theorem tryObserve_preserves_valid (state : Contract)
    (valid : state.Valid) : (tryObserve state).state.Valid := by
  rcases valid with
    ⟨capacityPositive, pendingBounded, pendingUnique, commandsValid,
      eventsValid, lifecycleValid⟩
  have eventsAfterValid :=
    BoundedMpsc.tryConsume_preserves_valid state.events eventsValid
  simpa [tryObserve, Contract.Valid] using
    ⟨capacityPositive, pendingBounded, pendingUnique, commandsValid,
      eventsAfterValid, lifecycleValid⟩

theorem close_preserves_pending (state : Contract) :
    (close state).pending = state.pending := rfl

theorem close_preserves_valid (state : Contract) (valid : state.Valid) :
    (close state).Valid := by
  rcases valid with
    ⟨capacityPositive, pendingBounded, pendingUnique, commandsValid,
      eventsValid, lifecycleValid⟩
  exact ⟨capacityPositive, pendingBounded, pendingUnique,
    BoundedMpsc.close_preserves_valid state.commands commandsValid,
    eventsValid, by
      cases terminal : state.commands.terminal <;>
        simp [close, BoundedMpsc.close, terminal]⟩

theorem close_rejects_admission (state : Contract) (identity : Identity) :
    (tryAdmit (close state) identity).status =
      if identity.Valid then .closed else .invalid := by
  by_cases identityValid : identity.Valid <;>
    simp [close, tryAdmit, identityValid]

end CMetaCFlowCalculus.IO.Communication

namespace CMetaCFlowCalculus.IO.ReadinessDriver

theorem eraseRegistration_projects (pending : List Registration)
    (identity : Communication.Identity) :
    (eraseRegistration pending identity).map Registration.identity =
      Communication.eraseIdentity
        (pending.map Registration.identity) identity := by
  induction pending with
  | nil => rfl
  | cons current remaining inductionHypothesis =>
      by_cases same : current.identity = identity
      · simp [eraseRegistration, Communication.eraseIdentity, same]
      · simp [eraseRegistration, Communication.eraseIdentity, same,
          inductionHypothesis]

theorem signalRegistration_preserves_identities (pending : List Registration)
    (identity : Communication.Identity) :
    (signalRegistration pending identity).map Registration.identity =
      pending.map Registration.identity := by
  induction pending with
  | nil => rfl
  | cons current remaining inductionHypothesis =>
      by_cases same : current.identity = identity <;>
        simp [signalRegistration, same, inductionHypothesis]

theorem signal_preserves_projection (state : State)
    (identity : Communication.Identity) :
    (signal state identity).project = state.project := by
  simp [signal, State.project, signalRegistration_preserves_identities]

theorem tryWatch_refines (state : State)
    (identity : Communication.Identity) (interests : Nat)
    (validInterests : interests ≠ 0) :
    (tryWatch state identity interests).state.project =
      (Communication.tryAdmit state.project identity).state := by
  by_cases validIdentity : identity.Valid
  · cases lifecycle : state.lifecycle with
    | draining =>
        simp [tryWatch, Communication.tryAdmit, State.project,
          validInterests, validIdentity, lifecycle]
    | «open» =>
        by_cases duplicate :
            identity ∈ state.pending.map Registration.identity
        · simp [tryWatch, Communication.tryAdmit, State.project,
            validInterests, validIdentity, lifecycle, duplicate]
        · by_cases hasCapacity : state.pending.length < state.capacity
          · cases publication :
                BoundedMpsc.tryPublish state.commands identity with
            | mk admission after =>
                cases admission <;>
                  simp [tryWatch, Communication.tryAdmit, State.project,
                    validInterests, validIdentity, lifecycle, duplicate,
                    hasCapacity, publication]
          · simp [tryWatch, Communication.tryAdmit, State.project,
              validInterests, validIdentity, lifecycle, duplicate,
              hasCapacity]
  · simp [tryWatch, Communication.tryAdmit, State.project,
      validInterests, validIdentity]

theorem tryTerminate_refines (state : State)
    (event : Communication.Event) :
    (tryTerminate state event).state.project =
      (Communication.tryTerminate state.project event).state := by
  by_cases validEvent : event.Valid
  · by_cases present :
        event.identity ∈ state.pending.map Registration.identity
    · cases publication : BoundedMpsc.tryPublish state.events event with
      | mk admission after =>
          cases admission <;>
            simp [tryTerminate, Communication.tryTerminate, State.project,
              validEvent, present, publication, eraseRegistration_projects]
    · simp [tryTerminate, Communication.tryTerminate, State.project,
        validEvent, present]
  · simp [tryTerminate, Communication.tryTerminate, State.project,
      validEvent]

theorem stale_terminal_unchanged (state : State)
    (event : Communication.Event)
    (stale : event.identity ∉ state.pending.map Registration.identity) :
    (tryTerminate state event).state = state := by
  by_cases validEvent : event.Valid <;>
    simp [tryTerminate, validEvent, stale]

theorem close_refines (state : State) :
    (close state).project = Communication.close state.project := by
  simp [close, Communication.close, State.project]

theorem tryObserve_refines (state : State) :
    (tryObserve state).observation =
        (Communication.tryObserve state.project).observation ∧
      (tryObserve state).state.project =
        (Communication.tryObserve state.project).state := by
  simp [tryObserve, Communication.tryObserve, State.project]

theorem tryWatch_preserves_valid (state : State)
    (identity : Communication.Identity) (interests : Nat)
    (validInterests : interests ≠ 0) (valid : state.Valid) :
    (tryWatch state identity interests).state.Valid := by
  unfold State.Valid at valid ⊢
  rw [tryWatch_refines state identity interests validInterests]
  exact Communication.tryAdmit_preserves_valid state.project identity valid

theorem tryTerminate_preserves_valid (state : State)
    (event : Communication.Event) (valid : state.Valid) :
    (tryTerminate state event).state.Valid := by
  unfold State.Valid at valid ⊢
  rw [tryTerminate_refines state event]
  exact Communication.tryTerminate_preserves_valid state.project event valid

theorem tryObserve_preserves_valid (state : State) (valid : state.Valid) :
    (tryObserve state).state.Valid := by
  unfold State.Valid at valid ⊢
  rw [(tryObserve_refines state).2]
  exact Communication.tryObserve_preserves_valid state.project valid

theorem close_preserves_valid (state : State) (valid : state.Valid) :
    (close state).Valid := by
  unfold State.Valid at valid ⊢
  rw [close_refines state]
  exact Communication.close_preserves_valid state.project valid

end CMetaCFlowCalculus.IO.ReadinessDriver

namespace CMetaCFlowCalculus.IO.CompletionDriver

theorem eraseOperation_projects (pending : List Operation)
    (identity : Communication.Identity) :
    (eraseOperation pending identity).map Operation.identity =
      Communication.eraseIdentity
        (pending.map Operation.identity) identity := by
  induction pending with
  | nil => rfl
  | cons current remaining inductionHypothesis =>
      by_cases same : current.identity = identity
      · simp [eraseOperation, Communication.eraseIdentity, same]
      · simp [eraseOperation, Communication.eraseIdentity, same,
          inductionHypothesis]

theorem beginOperation_preserves_identities (pending : List Operation)
    (identity : Communication.Identity) :
    (beginOperation pending identity).map Operation.identity =
      pending.map Operation.identity := by
  induction pending with
  | nil => rfl
  | cons current remaining inductionHypothesis =>
      by_cases same : current.identity = identity <;>
        simp [beginOperation, same, inductionHypothesis]

theorem begin_preserves_projection (state : State)
    (identity : Communication.Identity) :
    (begin state identity).project = state.project := by
  simp [begin, State.project, beginOperation_preserves_identities]

theorem trySubmit_refines (state : State)
    (identity : Communication.Identity) (operation : Nat)
    (validOperation : operation ≠ 0) :
    (trySubmit state identity operation).state.project =
      (Communication.tryAdmit state.project identity).state := by
  by_cases validIdentity : identity.Valid
  · cases lifecycle : state.lifecycle with
    | draining =>
        simp [trySubmit, Communication.tryAdmit, State.project,
          validOperation, validIdentity, lifecycle]
    | «open» =>
        by_cases duplicate : identity ∈ state.pending.map Operation.identity
        · simp [trySubmit, Communication.tryAdmit, State.project,
            validOperation, validIdentity, lifecycle, duplicate]
        · by_cases hasCapacity : state.pending.length < state.capacity
          · cases publication :
                BoundedMpsc.tryPublish state.commands identity with
            | mk admission after =>
                cases admission <;>
                  simp [trySubmit, Communication.tryAdmit, State.project,
                    validOperation, validIdentity, lifecycle, duplicate,
                    hasCapacity, publication]
          · simp [trySubmit, Communication.tryAdmit, State.project,
              validOperation, validIdentity, lifecycle, duplicate,
              hasCapacity]
  · simp [trySubmit, Communication.tryAdmit, State.project,
      validOperation, validIdentity]

theorem tryComplete_refines (state : State)
    (event : Communication.Event) :
    (tryComplete state event).state.project =
      (Communication.tryTerminate state.project event).state := by
  by_cases validEvent : event.Valid
  · by_cases present : event.identity ∈ state.pending.map Operation.identity
    · cases publication : BoundedMpsc.tryPublish state.events event with
      | mk admission after =>
          cases admission <;>
            simp [tryComplete, Communication.tryTerminate, State.project,
              validEvent, present, publication, eraseOperation_projects]
    · simp [tryComplete, Communication.tryTerminate, State.project,
        validEvent, present]
  · simp [tryComplete, Communication.tryTerminate, State.project,
      validEvent]

theorem stale_terminal_unchanged (state : State)
    (event : Communication.Event)
    (stale : event.identity ∉ state.pending.map Operation.identity) :
    (tryComplete state event).state = state := by
  by_cases validEvent : event.Valid <;>
    simp [tryComplete, validEvent, stale]

theorem close_refines (state : State) :
    (close state).project = Communication.close state.project := by
  simp [close, Communication.close, State.project]

theorem tryObserve_refines (state : State) :
    (tryObserve state).observation =
        (Communication.tryObserve state.project).observation ∧
      (tryObserve state).state.project =
        (Communication.tryObserve state.project).state := by
  simp [tryObserve, Communication.tryObserve, State.project]

theorem trySubmit_preserves_valid (state : State)
    (identity : Communication.Identity) (operation : Nat)
    (validOperation : operation ≠ 0) (valid : state.Valid) :
    (trySubmit state identity operation).state.Valid := by
  unfold State.Valid at valid ⊢
  rw [trySubmit_refines state identity operation validOperation]
  exact Communication.tryAdmit_preserves_valid state.project identity valid

theorem tryComplete_preserves_valid (state : State)
    (event : Communication.Event) (valid : state.Valid) :
    (tryComplete state event).state.Valid := by
  unfold State.Valid at valid ⊢
  rw [tryComplete_refines state event]
  exact Communication.tryTerminate_preserves_valid state.project event valid

theorem tryObserve_preserves_valid (state : State) (valid : state.Valid) :
    (tryObserve state).state.Valid := by
  unfold State.Valid at valid ⊢
  rw [(tryObserve_refines state).2]
  exact Communication.tryObserve_preserves_valid state.project valid

theorem close_preserves_valid (state : State) (valid : state.Valid) :
    (close state).Valid := by
  unfold State.Valid at valid ⊢
  rw [close_refines state]
  exact Communication.close_preserves_valid state.project valid

end CMetaCFlowCalculus.IO.CompletionDriver

namespace CMetaCFlowCalculus.IO.BlockingDriver

theorem eraseJob_projects (pending : List Job)
    (identity : Communication.Identity) :
    (eraseJob pending identity).map Job.identity =
      Communication.eraseIdentity (pending.map Job.identity) identity := by
  induction pending with
  | nil => rfl
  | cons current remaining inductionHypothesis =>
      by_cases same : current.identity = identity
      · simp [eraseJob, Communication.eraseIdentity, same]
      · simp [eraseJob, Communication.eraseIdentity, same,
          inductionHypothesis]

theorem startJob_preserves_identities (pending : List Job)
    (identity : Communication.Identity) :
    (startJob pending identity).map Job.identity = pending.map Job.identity := by
  induction pending with
  | nil => rfl
  | cons current remaining inductionHypothesis =>
      by_cases same : current.identity = identity <;>
        simp [startJob, same, inductionHypothesis]

theorem start_preserves_projection (state : State)
    (identity : Communication.Identity) :
    (start state identity).project = state.project := by
  simp [start, State.project, startJob_preserves_identities]

theorem tryExecute_refines (state : State)
    (identity : Communication.Identity) (job : Nat)
    (validJob : job ≠ 0) :
    (tryExecute state identity job).state.project =
      (Communication.tryAdmit state.project identity).state := by
  by_cases validIdentity : identity.Valid
  · cases lifecycle : state.lifecycle with
    | draining =>
        simp [tryExecute, Communication.tryAdmit, State.project,
          validJob, validIdentity, lifecycle]
    | «open» =>
        by_cases duplicate : identity ∈ state.pending.map Job.identity
        · simp [tryExecute, Communication.tryAdmit, State.project,
            validJob, validIdentity, lifecycle, duplicate]
        · by_cases hasCapacity : state.pending.length < state.capacity
          · cases publication :
                BoundedMpsc.tryPublish state.commands identity with
            | mk admission after =>
                cases admission <;>
                  simp [tryExecute, Communication.tryAdmit, State.project,
                    validJob, validIdentity, lifecycle, duplicate,
                    hasCapacity, publication]
          · simp [tryExecute, Communication.tryAdmit, State.project,
              validJob, validIdentity, lifecycle, duplicate, hasCapacity]
  · simp [tryExecute, Communication.tryAdmit, State.project,
      validJob, validIdentity]

theorem tryComplete_refines (state : State)
    (event : Communication.Event) :
    (tryComplete state event).state.project =
      (Communication.tryTerminate state.project event).state := by
  by_cases validEvent : event.Valid
  · by_cases present : event.identity ∈ state.pending.map Job.identity
    · cases publication : BoundedMpsc.tryPublish state.events event with
      | mk admission after =>
          cases admission <;>
            simp [tryComplete, Communication.tryTerminate, State.project,
              validEvent, present, publication, eraseJob_projects]
    · simp [tryComplete, Communication.tryTerminate, State.project,
        validEvent, present]
  · simp [tryComplete, Communication.tryTerminate, State.project,
      validEvent]

theorem stale_terminal_unchanged (state : State)
    (event : Communication.Event)
    (stale : event.identity ∉ state.pending.map Job.identity) :
    (tryComplete state event).state = state := by
  by_cases validEvent : event.Valid <;>
    simp [tryComplete, validEvent, stale]

theorem close_refines (state : State) :
    (close state).project = Communication.close state.project := by
  simp [close, Communication.close, State.project]

theorem tryObserve_refines (state : State) :
    (tryObserve state).observation =
        (Communication.tryObserve state.project).observation ∧
      (tryObserve state).state.project =
        (Communication.tryObserve state.project).state := by
  simp [tryObserve, Communication.tryObserve, State.project]

theorem tryExecute_preserves_valid (state : State)
    (identity : Communication.Identity) (job : Nat)
    (validJob : job ≠ 0) (valid : state.Valid) :
    (tryExecute state identity job).state.Valid := by
  unfold State.Valid at valid ⊢
  rw [tryExecute_refines state identity job validJob]
  exact Communication.tryAdmit_preserves_valid state.project identity valid

theorem tryComplete_preserves_valid (state : State)
    (event : Communication.Event) (valid : state.Valid) :
    (tryComplete state event).state.Valid := by
  unfold State.Valid at valid ⊢
  rw [tryComplete_refines state event]
  exact Communication.tryTerminate_preserves_valid state.project event valid

theorem tryObserve_preserves_valid (state : State) (valid : state.Valid) :
    (tryObserve state).state.Valid := by
  unfold State.Valid at valid ⊢
  rw [(tryObserve_refines state).2]
  exact Communication.tryObserve_preserves_valid state.project valid

theorem close_preserves_valid (state : State) (valid : state.Valid) :
    (close state).Valid := by
  unfold State.Valid at valid ⊢
  rw [close_refines state]
  exact Communication.close_preserves_valid state.project valid

end CMetaCFlowCalculus.IO.BlockingDriver
