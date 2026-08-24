import CMetaCFlowCalculus.CFlow.ExecutorProtocol

namespace CMetaCFlowCalculus.CFlow.ExecutorProtocol

open CMetaCFlowCalculus.CFlow.MachineRuntime

theorem task_partition (tasks : List TaskPhase) :
    tasks.length = phaseCount .queued tasks + phaseCount .running tasks +
      phaseCount .completed tasks + phaseCount .cancelled tasks := by
  induction tasks with
  | nil => simp [phaseCount]
  | cons phase remaining inductionHypothesis =>
      cases phase <;>
        simp [phaseCount] at inductionHypothesis ⊢ <;>
        omega

theorem state_conserved (state : State) : state.Conserved :=
  task_partition state.tasks

theorem every_task_has_exactly_one_phase (phase : TaskPhase) :
    (phase = .queued ∨ phase = .running ∨ phase = .completed ∨
      phase = .cancelled) ∧
    ¬(phase = .completed ∧ phase = .cancelled) := by
  cases phase <;> simp

theorem initial_safe (kind : ExecutorKind) (policy : ShutdownPolicy)
    (capacity : Nat) : (initial kind policy capacity).Safe := by
  cases kind <;>
    simp [initial, State.Safe, State.Bounded, State.Conserved,
      State.SerialSafe, State.accepted, State.queued, State.running, State.completed,
      State.cancelled, phaseCount]

theorem phaseCount_append (wanted : TaskPhase) (left right : List TaskPhase) :
    phaseCount wanted (left ++ right) =
      phaseCount wanted left + phaseCount wanted right := by
  induction left with
  | nil => simp [phaseCount]
  | cons phase remaining inductionHypothesis =>
      simp [phaseCount, inductionHypothesis, Nat.add_assoc]

theorem accepted_post_is_bounded_and_conserved (state : State)
    (safe : state.Safe) (openState : state.lifecycle = .open)
    (room : state.queued < state.capacity) :
    let admitted := tryPost state .external
    admitted.result = .accepted ∧
      admitted.state.queued = state.queued + 1 ∧
      admitted.state.accepted = state.accepted + 1 ∧
      admitted.state.Safe := by
  rcases safe with ⟨bounded, _, serialSafe⟩
  change phaseCount .queued state.tasks < state.capacity at room
  let after : State := { state with tasks := state.tasks ++ [.queued] }
  have posted : tryPost state .external =
      { result := .accepted, state := after } := by
    simp [tryPost, openState, room, State.queued, after]
  rw [posted]
  change AdmissionResult.accepted = AdmissionResult.accepted ∧
    phaseCount .queued after.tasks = phaseCount .queued state.tasks + 1 ∧
    after.tasks.length = state.tasks.length + 1 ∧ after.Safe
  refine ⟨rfl, ?_, by simp [after], ?_⟩
  · simp [after, phaseCount_append, phaseCount]
  · refine ⟨?_, state_conserved _, ?_⟩
    · simpa [State.Bounded, State.queued, after, phaseCount_append,
        phaseCount] using Nat.succ_le_of_lt room
    · simpa [State.SerialSafe, State.running, after, phaseCount_append,
        phaseCount] using serialSafe

theorem tryPost_preserves_safe (state : State) (caller : CallerContext)
    (safe : state.Safe) : (tryPost state caller).state.Safe := by
  rcases safe with ⟨bounded, _, serialSafe⟩
  cases lifecycleEq : state.lifecycle
  · by_cases room : state.queued < state.capacity
    · simpa [tryPost, lifecycleEq, room] using
        (accepted_post_is_bounded_and_conserved state
          ⟨bounded, state_conserved state, serialSafe⟩
          lifecycleEq room).2.2.2
    · simp [tryPost, lifecycleEq, room]
      exact ⟨bounded, state_conserved _, serialSafe⟩
  · simp [tryPost, lifecycleEq]
    exact ⟨bounded, state_conserved _, serialSafe⟩
  · simp [tryPost, lifecycleEq]
    exact ⟨bounded, state_conserved _, serialSafe⟩

theorem replaceFirst_length (source target : TaskPhase)
    (tasks : List TaskPhase) :
    (replaceFirst source target tasks).length = tasks.length := by
  induction tasks with
  | nil => simp [replaceFirst]
  | cons phase remaining inductionHypothesis =>
      by_cases isSource : phase = source <;>
        simp [replaceFirst, isSource, inductionHypothesis]

theorem replaceFirst_source_count_le (source target : TaskPhase)
    (different : source ≠ target) (tasks : List TaskPhase) :
    phaseCount source (replaceFirst source target tasks) ≤
      phaseCount source tasks := by
  induction tasks with
  | nil => simp [replaceFirst]
  | cons phase remaining inductionHypothesis =>
      by_cases isSource : phase = source
      · subst phase
        simp [replaceFirst, Ne.symm different, phaseCount]
      · simp [replaceFirst, isSource, phaseCount,
          inductionHypothesis]

theorem replaceFirst_other_count (source target observed : TaskPhase)
    (notSource : observed ≠ source) (notTarget : observed ≠ target)
    (tasks : List TaskPhase) :
    phaseCount observed (replaceFirst source target tasks) =
      phaseCount observed tasks := by
  induction tasks with
  | nil => simp [replaceFirst]
  | cons phase remaining inductionHypothesis =>
      by_cases isSource : phase = source
      · subst phase
        simp [replaceFirst, Ne.symm notSource, Ne.symm notTarget, phaseCount]
      · simp [replaceFirst, isSource, phaseCount,
          inductionHypothesis]

theorem replaceFirst_target_count (source target : TaskPhase)
    (different : source ≠ target) (tasks : List TaskPhase)
    (present : phaseCount source tasks > 0) :
    phaseCount target (replaceFirst source target tasks) =
      phaseCount target tasks + 1 := by
  induction tasks with
  | nil => simp [phaseCount] at present
  | cons phase remaining inductionHypothesis =>
      by_cases isSource : phase = source
      · subst phase
        simp [replaceFirst, different, phaseCount]
        omega
      · have remainingPresent : phaseCount source remaining > 0 := by
          simp [phaseCount, isSource] at present
          exact present
        by_cases isTarget : phase = target
        · subst phase
          simp [replaceFirst, isSource, phaseCount,
            inductionHypothesis remainingPresent]
          omega
        · simp [replaceFirst, isSource, isTarget, phaseCount,
            inductionHypothesis remainingPresent]

theorem start_preserves_safe (state : State) (safe : state.Safe) :
    (start state).state.Safe := by
  rcases safe with ⟨bounded, _, serialSafe⟩
  by_cases queuedEmpty : state.queued = 0
  · simp [start, queuedEmpty]
    exact ⟨bounded, state_conserved _, serialSafe⟩
  · have queuedPresent : phaseCount TaskPhase.queued state.tasks > 0 := by
      change phaseCount TaskPhase.queued state.tasks ≠ 0 at queuedEmpty
      omega
    have boundedAfter :
        phaseCount .queued (replaceFirst .queued .running state.tasks) ≤
          state.capacity :=
      Nat.le_trans
        (replaceFirst_source_count_le .queued .running (by decide) state.tasks)
        bounded
    cases kindEq : state.kind
    · by_cases runningEmpty : state.running = 0
      · have runningAfter :
            phaseCount .running (replaceFirst .queued .running state.tasks) = 1 := by
          rw [replaceFirst_target_count .queued .running (by decide)
            state.tasks queuedPresent]
          change phaseCount TaskPhase.running state.tasks = 0 at runningEmpty
          omega
        simp [start, queuedEmpty, kindEq, runningEmpty, State.Safe]
        exact ⟨boundedAfter, state_conserved _, by
          simp only [State.SerialSafe, State.running]
          rw [runningAfter]
          decide⟩
      · simp [start, queuedEmpty, kindEq, runningEmpty]
        exact ⟨bounded, state_conserved _, serialSafe⟩
    · by_cases runningEmpty : state.running = 0
      · have runningAfter :
            phaseCount .running (replaceFirst .queued .running state.tasks) = 1 := by
          rw [replaceFirst_target_count .queued .running (by decide)
            state.tasks queuedPresent]
          change phaseCount TaskPhase.running state.tasks = 0 at runningEmpty
          omega
        simp [start, queuedEmpty, kindEq, runningEmpty, State.Safe]
        exact ⟨boundedAfter, state_conserved _, by
          simp only [State.SerialSafe, State.running]
          rw [runningAfter]
          decide⟩
      · simp [start, queuedEmpty, kindEq, runningEmpty]
        exact ⟨bounded, state_conserved _, serialSafe⟩
    · simp [start, queuedEmpty, kindEq, State.Safe]
      exact ⟨boundedAfter, state_conserved _, trivial⟩

theorem finish_preserves_safe (state : State) (safe : state.Safe) :
    (finish state).state.Safe := by
  rcases safe with ⟨bounded, _, serialSafe⟩
  by_cases runningEmpty : state.running = 0
  · simp [finish, runningEmpty]
    exact ⟨bounded, state_conserved _, serialSafe⟩
  · have queuedAfter :
        phaseCount .queued (replaceFirst .running .completed state.tasks) =
          phaseCount .queued state.tasks :=
      replaceFirst_other_count .running .completed .queued
        (by decide) (by decide) state.tasks
    have runningAfter :
        phaseCount .running (replaceFirst .running .completed state.tasks) ≤
          phaseCount .running state.tasks :=
      replaceFirst_source_count_le .running .completed (by decide) state.tasks
    simp [finish, runningEmpty, State.Safe]
    refine ⟨?_, state_conserved _, ?_⟩
    · simpa [State.Bounded, State.queued, queuedAfter] using bounded
    · cases kindEq : state.kind
      · simp [State.SerialSafe, kindEq] at serialSafe
        simpa [State.SerialSafe, State.running, kindEq] using
          Nat.le_trans runningAfter serialSafe
      · simp [State.SerialSafe, kindEq] at serialSafe
        simpa [State.SerialSafe, State.running, kindEq] using
          Nat.le_trans runningAfter serialSafe
      · simp [State.SerialSafe]

theorem cancelQueued_count_zero (tasks : List TaskPhase) :
    phaseCount .queued (tasks.map cancelQueued) = 0 := by
  induction tasks with
  | nil => simp [phaseCount]
  | cons phase remaining inductionHypothesis =>
      cases phase <;>
        simp [cancelQueued, phaseCount, inductionHypothesis]

theorem cancelQueued_running_count (tasks : List TaskPhase) :
    phaseCount .running (tasks.map cancelQueued) = phaseCount .running tasks := by
  induction tasks with
  | nil => simp [phaseCount]
  | cons phase remaining inductionHypothesis =>
      cases phase <;>
        simp [cancelQueued, phaseCount, inductionHypothesis]

theorem beginShutdown_preserves_safe (state : State) (safe : state.Safe) :
    (beginShutdown state).Safe := by
  rcases safe with ⟨bounded, _, serialSafe⟩
  cases lifecycleEq : state.lifecycle
  · cases policyEq : state.policy
    · simp [beginShutdown, lifecycleEq, policyEq, State.Safe]
      exact ⟨bounded, state_conserved _, serialSafe⟩
    · simp [beginShutdown, lifecycleEq, policyEq, State.Safe]
      refine ⟨?_, state_conserved _, ?_⟩
      · simp [State.Bounded, State.queued, cancelQueued_count_zero]
      · cases kindEq : state.kind
        · simp [State.SerialSafe, kindEq] at serialSafe
          simpa [State.SerialSafe, State.running, kindEq,
            cancelQueued_running_count] using serialSafe
        · simp [State.SerialSafe, kindEq] at serialSafe
          simpa [State.SerialSafe, State.running, kindEq,
            cancelQueued_running_count] using serialSafe
        · simp [State.SerialSafe]
  · simp [beginShutdown, lifecycleEq]
    exact ⟨bounded, state_conserved _, serialSafe⟩
  · simp [beginShutdown, lifecycleEq]
    exact ⟨bounded, state_conserved _, serialSafe⟩

theorem post_shutdown_rejected (state : State)
    (notOpen : state.lifecycle ≠ .open) (caller : CallerContext) :
    (tryPost state caller).result = .closed ∧
      (tryPost state caller).state.tasks = state.tasks := by
  cases lifecycleEq : state.lifecycle <;> simp_all [tryPost]

theorem full_post_preserves_task_ledger (state : State)
    (openState : state.lifecycle = .open)
    (full : state.capacity ≤ state.queued) (caller : CallerContext) :
    (tryPost state caller).result = .full ∧
      (tryPost state caller).state.tasks = state.tasks := by
  have noRoom : ¬state.queued < state.capacity := by omega
  simp [tryPost, openState, noRoom]

theorem self_blocking_operations_fail_fast (state : State)
    (openState : state.lifecycle = .open)
    (full : state.capacity ≤ state.queued) :
    (blockingPost state (.callback state.id)).result = .wouldBlock ∧
      (waitIdle state (.callback state.id)).result = .wouldBlock := by
  simp [blockingPost, waitIdle, sameCallback, openState, full]

theorem settleShutdown_settled (state : State)
    (closing : state.lifecycle = .closing) :
    (settleShutdown state).Settled := by
  unfold settleShutdown
  rw [closing]
  cases state.policy
  · unfold State.Settled
    intro phase member
    obtain ⟨original, _, rfl⟩ := List.mem_map.mp member
    cases original <;> simp
  · unfold State.Settled
    intro phase member
    obtain ⟨original, _, rfl⟩ := List.mem_map.mp member
    cases original <;> simp

theorem terminal_phase_counts_zero (tasks : List TaskPhase)
    (settled : ∀ phase ∈ tasks,
      phase = TaskPhase.completed ∨ phase = TaskPhase.cancelled) :
    phaseCount .queued tasks = 0 ∧ phaseCount .running tasks = 0 := by
  induction tasks with
  | nil => simp [phaseCount]
  | cons phase remaining inductionHypothesis =>
      have phaseTerminal := settled phase (by simp)
      have remainingSettled :
          ∀ candidate ∈ remaining,
            candidate = TaskPhase.completed ∨ candidate = TaskPhase.cancelled := by
        intro candidate member
        exact settled candidate (by simp [member])
      have remainingZero := inductionHypothesis remainingSettled
      rcases phaseTerminal with rfl | rfl <;>
        simpa [phaseCount] using remainingZero

theorem settled_quiescent (state : State) (settled : state.Settled) :
    state.Quiescent := by
  exact terminal_phase_counts_zero state.tasks settled

theorem settled_tasks_have_exactly_one_terminal_outcome (state : State)
    (settled : state.Settled) :
    state.accepted = state.completed + state.cancelled := by
  have conserved := state_conserved state
  have quiescent := settled_quiescent state settled
  unfold State.Conserved at conserved
  unfold State.Quiescent at quiescent
  omega

theorem settleShutdown_quiescent (state : State)
    (closing : state.lifecycle = .closing) :
    (settleShutdown state).Quiescent := by
  exact settled_quiescent _ (settleShutdown_settled state closing)

theorem settleShutdown_lifecycle (state : State)
    (closing : state.lifecycle = .closing) :
    (settleShutdown state).lifecycle = .closing := by
  unfold settleShutdown
  rw [closing]
  cases state.policy <;> rfl

theorem close_produces_closed_quiescent (state closedState : State)
    (closed : close state = some closedState) :
    closedState.lifecycle = .closed ∧ closedState.Quiescent := by
  simp [close] at closed
  rcases closed with ⟨⟨⟨_, queuedEmpty⟩, runningEmpty⟩, rfl⟩
  exact ⟨rfl, queuedEmpty, runningEmpty⟩

theorem scheduleMachine_scheduled_only_after_acceptance (control : MachineControl)
    (idle : control.worker = .idle)
    (scheduled : (scheduleMachine control).state.worker = .scheduled) :
    (scheduleMachine control).result = .accepted := by
  simp [scheduleMachine, idle] at scheduled ⊢
  exact scheduled

theorem startMachine_executing_only_after_start (control : MachineControl)
    (scheduledState : control.worker = .scheduled)
    (executing : (startMachine control).state.worker = .executing) :
    (startMachine control).result = .started := by
  simp [startMachine, scheduledState] at executing ⊢
  exact executing

end CMetaCFlowCalculus.CFlow.ExecutorProtocol
