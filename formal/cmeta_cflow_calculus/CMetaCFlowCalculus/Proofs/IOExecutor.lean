import CMetaCFlowCalculus.IO.Executor

namespace CMetaCFlowCalculus.IO.Executor

theorem tryPost_preserves_valid (state : State α) (payload : α)
    (valid : state.Valid) : (tryPost state payload).state.Valid := by
  rcases valid with
    ⟨capacityPositive, parallelismPositive, queueBounded, runningBounded,
      capabilityBound⟩
  cases terminal : state.terminal with
  | draining | closed =>
      simp [tryPost, terminal, State.Valid, capacityPositive,
        parallelismPositive, queueBounded, runningBounded, capabilityBound]
  | «open» =>
      by_cases hasCapacity : state.queue.length < state.capacity
      · simp [tryPost, terminal, hasCapacity, State.Valid,
          List.length_append, capacityPositive, parallelismPositive,
          Nat.succ_le_of_lt hasCapacity, runningBounded, capabilityBound]
      · simp [tryPost, terminal, hasCapacity, State.Valid,
          capacityPositive, parallelismPositive, queueBounded,
          runningBounded, capabilityBound]

theorem start_preserves_valid (state : State α) (valid : state.Valid) :
    (start state).state.Valid := by
  rcases valid with
    ⟨capacityPositive, parallelismPositive, queueBounded, runningBounded,
      capabilityBound⟩
  cases terminal : state.terminal with
  | closed =>
      simp [start, terminal, State.Valid, capacityPositive,
        parallelismPositive, queueBounded, runningBounded, capabilityBound]
  | «open» | draining =>
      by_cases permit : state.running.length < state.parallelism
      · cases queue : state.queue with
        | nil =>
            simp [start, terminal, permit, queue, State.Valid,
              capacityPositive, parallelismPositive, runningBounded,
              capabilityBound]
        | cons task remaining =>
            have remainingBounded : remaining.length ≤ state.capacity := by
              rw [queue] at queueBounded
              simp only [List.length_cons] at queueBounded
              exact Nat.le_trans (Nat.le_succ remaining.length) queueBounded
            simp [start, terminal, permit, queue, State.Valid,
              List.length_append, capacityPositive, parallelismPositive,
              remainingBounded, Nat.succ_le_of_lt permit, capabilityBound]
      · simp [start, terminal, permit, State.Valid, capacityPositive,
          parallelismPositive, queueBounded, runningBounded, capabilityBound]

theorem finish_preserves_valid (state : State α) (taskId : Nat)
    (valid : state.Valid) : (finish state taskId).state.Valid := by
  by_cases present : taskId ∈ state.running
  · rcases valid with
      ⟨capacityPositive, parallelismPositive, queueBounded, runningBounded,
        capabilityBound⟩
    simp only [finish, present, ↓reduceIte]
    exact ⟨capacityPositive, parallelismPositive, queueBounded,
      Nat.le_trans List.length_erase_le runningBounded, capabilityBound⟩
  · simpa [finish, present] using valid

theorem shutdown_preserves_valid (state : State α) (valid : state.Valid) :
    (shutdown state).state.Valid := by
  cases terminal : state.terminal <;>
    simpa [shutdown, terminal, State.Valid] using valid

theorem settle_preserves_valid (state : State α) (valid : state.Valid) :
    (settle state).state.Valid := by
  cases terminal : state.terminal with
  | «open» | closed => simpa [settle, terminal, State.Valid] using valid
  | draining =>
      by_cases idle : state.queue.isEmpty && state.running.isEmpty <;>
        simpa [settle, terminal, idle, State.Valid] using valid

theorem tryPost_preserves_identifiers (state : State α) (payload : α)
    (valid : state.IdentifiersValid) :
    (tryPost state payload).state.IdentifiersValid := by
  rcases valid with ⟨idsUnique, idsBounded, nextIdPositive⟩
  cases terminal : state.terminal with
  | draining | closed =>
      simpa [tryPost, terminal, State.IdentifiersValid] using
        ⟨idsUnique, idsBounded, nextIdPositive⟩
  | «open» =>
      by_cases hasCapacity : state.queue.length < state.capacity
      · simp only [tryPost, terminal, hasCapacity, ↓reduceIte]
        have oldNotFresh :
            ∀ old ∈ state.knownIds, old ≠ state.nextId := by
          intro old member
          exact Nat.ne_of_lt (idsBounded old member)
        have newUnique :
            (state.knownIds ++ [state.nextId]).Nodup := by
          rw [List.nodup_append]
          exact ⟨idsUnique, by simp, by simpa using oldNotFresh⟩
        have newBounded :
            ∀ taskId ∈ state.knownIds ++ [state.nextId],
              taskId < state.nextId + 1 := by
          intro taskId member
          simp only [List.mem_append, List.mem_singleton] at member
          rcases member with oldMember | rfl
          · exact Nat.lt_succ_of_lt (idsBounded taskId oldMember)
          · exact Nat.lt_succ_self _
        rw [State.IdentifiersValid]
        simp only [State.knownIds, List.map_append, List.map_cons,
          List.map_nil]
        have knownIdsEqual :
            state.running ++ (state.queue.map Task.id ++ [state.nextId]) =
              state.knownIds ++ [state.nextId] := by
          simp [State.knownIds, List.append_assoc]
        rw [knownIdsEqual]
        exact ⟨newUnique, newBounded, Nat.zero_lt_succ state.nextId⟩
      · simpa [tryPost, terminal, hasCapacity, State.IdentifiersValid] using
          ⟨idsUnique, idsBounded, nextIdPositive⟩

theorem start_preserves_identifiers (state : State α)
    (valid : state.IdentifiersValid) :
    (start state).state.IdentifiersValid := by
  cases terminal : state.terminal with
  | closed => simpa [start, terminal, State.IdentifiersValid] using valid
  | «open» | draining =>
      by_cases permit : state.running.length < state.parallelism
      · cases queue : state.queue with
        | nil =>
            simpa [start, terminal, permit, queue,
              State.IdentifiersValid] using valid
        | cons task remaining =>
            simp only [start, terminal, permit, queue, ↓reduceIte]
            simpa [State.IdentifiersValid, State.knownIds, queue,
              List.append_assoc] using valid
      · simpa [start, terminal, permit, State.IdentifiersValid] using valid

theorem finish_preserves_identifiers (state : State α) (taskId : Nat)
    (valid : state.IdentifiersValid) :
    (finish state taskId).state.IdentifiersValid := by
  by_cases present : taskId ∈ state.running
  · rcases valid with ⟨idsUnique, idsBounded, nextIdPositive⟩
    have runningSublist :
        List.Sublist (state.running.erase taskId) state.running :=
      List.erase_sublist
    have knownSublist :
        List.Sublist
          (state.running.erase taskId ++ state.queue.map Task.id)
          state.knownIds := by
      simpa [State.knownIds] using
        runningSublist.append
          (List.Sublist.refl (state.queue.map Task.id))
    simp only [finish, present, ↓reduceIte]
    exact ⟨knownSublist.nodup idsUnique,
      fun knownId member => idsBounded knownId (knownSublist.subset member),
      nextIdPositive⟩
  · simpa [finish, present] using valid

theorem accepted_post_appends_once {before after : State α} {payload : α}
    {taskId : Nat}
    (transition : tryPost before payload =
      { status := .accepted, taskId := some taskId, state := after }) :
    taskId = before.nextId ∧
      after.queue = before.queue ++ [{ id := taskId, payload := payload }] := by
  cases terminal : before.terminal with
  | draining | closed => simp [tryPost, terminal] at transition
  | «open» =>
      by_cases hasCapacity : before.queue.length < before.capacity
      · simp [tryPost, terminal, hasCapacity] at transition
        rcases transition with ⟨rfl, rfl⟩
        simp
      · simp [tryPost, terminal, hasCapacity] at transition

theorem started_task_is_fifo_head {before after : State α} {task : Task α}
    (transition : start before =
      { status := .started, task := some task, state := after }) :
    ∃ remaining, before.queue = task :: remaining ∧
      after.queue = remaining := by
  cases terminal : before.terminal with
  | closed => simp [start, terminal] at transition
  | «open» | draining =>
      by_cases permit : before.running.length < before.parallelism
      · cases queue : before.queue with
        | nil => simp [start, terminal, permit, queue] at transition
        | cons head remaining =>
            simp [start, terminal, permit, queue] at transition
            rcases transition with ⟨rfl, rfl⟩
            exact ⟨remaining, rfl, rfl⟩
      · simp [start, terminal, permit] at transition

theorem finish_removes_running_id (state : State α) (taskId : Nat)
    (present : taskId ∈ state.running) :
    (finish state taskId).status = .finished ∧
      (finish state taskId).state.running = state.running.erase taskId := by
  simp [finish, present]

theorem finish_twice_is_not_found (state : State α) (taskId : Nat)
    (present : taskId ∈ state.running)
    (unique : state.running.Nodup) :
    (finish (finish state taskId).state taskId).status = .notFound := by
  simp [finish, present, List.Nodup.not_mem_erase unique]

theorem serial_running_at_most_one (state : State α) (valid : state.Valid)
    (serial : state.capability = .serial) : state.running.length ≤ 1 := by
  rcases valid with ⟨_, _, _, runningBounded, capabilityBound⟩
  rcases capabilityBound with concurrent | one
  · simp [serial] at concurrent
  · simpa [one] using runningBounded

end CMetaCFlowCalculus.IO.Executor
