import CMetaCFlowCalculus.CFlow.MachineSchema
import CMetaCFlowCalculus.CFlow.Mailbox

namespace CMetaCFlowCalculus.CFlow.Machine

open CMetaCFlowCalculus.CMeta
open CMetaCFlowCalculus.CFlow.Mailbox

abbrev StateId := Nat
abbrev GuardId := Nat
abbrev ActionId := Nat

structure StateDecl where
  id : StateId
  valueTy : Ty
  kind : MachineStateKind
  deriving Repr, DecidableEq

structure GuardContract where
  pureEffect : Bool
  deterministic : Bool
  total : Bool
  noAlias : Bool
  deriving Repr, DecidableEq

structure GuardDecl where
  id : GuardId
  stateTy : Ty
  eventId : Nat
  eventTy : Ty
  contract : GuardContract
  deriving Repr, DecidableEq

inductive ActionObservationDecl where
  | none
  | value (outputTy : Ty)
  | event (eventId : Nat) (payloadTy : Ty)
  deriving Repr, DecidableEq

structure ActionDecl where
  id : ActionId
  sourceTy : Ty
  eventId : Nat
  eventTy : Ty
  targetTy : Ty
  deterministic : Bool
  noAlias : Bool
  mayFail : Bool
  observation : ActionObservationDecl
  deriving Repr, DecidableEq

structure Transition where
  source : StateId
  event : Nat
  guard : GuardId
  action : ActionId
  target : StateId
  priority : Nat
  deriving Repr, DecidableEq

structure Machine where
  states : List StateDecl
  initial : StateId
  events : Schema
  guards : List GuardDecl
  actions : List ActionDecl
  transitions : List Transition
  deriving Repr, DecidableEq

def lookupState : List StateDecl → StateId → Option StateDecl
  | [], _ => none
  | state :: remaining, id =>
      if state.id = id then some state else lookupState remaining id

theorem lookupState_some_id {states : List StateDecl} {id : StateId}
    {state : StateDecl} (found : lookupState states id = some state) :
    state.id = id := by
  induction states with
  | nil => simp [lookupState] at found
  | cons candidate remaining inductionHypothesis =>
      by_cases same : candidate.id = id
      · simp [lookupState, same] at found
        subst state
        exact same
      · simp [lookupState, same] at found
        exact inductionHypothesis found

def lookupGuard : List GuardDecl → GuardId → Option GuardDecl
  | [], _ => none
  | guard :: remaining, id =>
      if guard.id = id then some guard else lookupGuard remaining id

def lookupAction : List ActionDecl → ActionId → Option ActionDecl
  | [], _ => none
  | action :: remaining, id =>
      if action.id = id then some action else lookupAction remaining id

def StateKnown (machine : Machine) (id : StateId) : Prop :=
  ∃ state, lookupState machine.states id = some state

def EventTyped (machine : Machine) (event : TypedEvent) : Prop :=
  ∃ expected, machine.events.lookup event.id = some expected ∧
    expected.payloadTy = event.payloadTy

def GuardTyped (machine : Machine) (transition : Transition)
    (source : StateDecl) (event : EventType) : Prop :=
  transition.guard = 0 ∨ ∃ guard,
    lookupGuard machine.guards transition.guard = some guard ∧
    guard.stateTy = source.valueTy ∧ guard.eventId = transition.event ∧
    guard.eventTy = event.payloadTy ∧
    guard.contract = ({
      pureEffect := true
      deterministic := true
      total := true
      noAlias := true } : GuardContract)

def ActionObservationTyped (machine : Machine) : ActionObservationDecl → Prop
  | .none => True
  | .value _ => True
  | .event eventId payloadTy => ∃ event,
      machine.events.lookup eventId = some event ∧ event.payloadTy = payloadTy

def ActionTyped (machine : Machine) (transition : Transition)
    (source target : StateDecl) (event : EventType) : Prop :=
  (transition.action = 0 ∧ source.valueTy = target.valueTy) ∨ ∃ action,
    lookupAction machine.actions transition.action = some action ∧
    action.sourceTy = source.valueTy ∧ action.eventId = transition.event ∧
    action.eventTy = event.payloadTy ∧ action.targetTy = target.valueTy ∧
    action.deterministic = true ∧ action.noAlias = true

inductive Reachable (machine : Machine) : StateId → Prop where
  | initial : Reachable machine machine.initial
  | target {transition : Transition} :
      transition ∈ machine.transitions →
      Reachable machine transition.source → Reachable machine transition.target

/-- Proof-facing validity mirrors the references and type alignment checked by
    transactional C construction. Reachability and declaration-use are finite
    normalization checks that do not participate in one-step preservation. -/
structure Machine.Valid (machine : Machine) : Prop where
  statesNonempty : machine.states ≠ []
  stateIdsNonzero : ∀ state ∈ machine.states, state.id ≠ 0
  stateIdsUnique : (machine.states.map StateDecl.id).Nodup
  eventSchemaValid : machine.events.Valid
  guardIdsNonzero : ∀ guard ∈ machine.guards, guard.id ≠ 0
  guardIdsUnique : (machine.guards.map GuardDecl.id).Nodup
  actionIdsNonzero : ∀ action ∈ machine.actions, action.id ≠ 0
  actionIdsUnique : (machine.actions.map ActionDecl.id).Nodup
  actionObservationsTyped : ∀ action ∈ machine.actions,
    ActionObservationTyped machine action.observation
  initialKnown : StateKnown machine machine.initial
  sourceKnown : ∀ transition ∈ machine.transitions,
    StateKnown machine transition.source
  targetKnown : ∀ transition ∈ machine.transitions,
    StateKnown machine transition.target
  eventKnown : ∀ transition ∈ machine.transitions, ∃ event,
    machine.events.lookup transition.event = some event
  guardTyped : ∀ transition ∈ machine.transitions,
    ∀ source event,
      lookupState machine.states transition.source = some source →
      machine.events.lookup transition.event = some event →
      GuardTyped machine transition source event
  actionTyped : ∀ transition ∈ machine.transitions,
    ∀ source target event,
      lookupState machine.states transition.source = some source →
      lookupState machine.states transition.target = some target →
      machine.events.lookup transition.event = some event →
      ActionTyped machine transition source target event
  activeSource : ∀ transition ∈ machine.transitions,
    ∀ source, lookupState machine.states transition.source = some source →
      source.kind = .active
  priorityKeysUnique :
    (machine.transitions.map fun transition =>
      (transition.source, transition.event, transition.priority)).Nodup
  allStatesReachable : ∀ state ∈ machine.states, Reachable machine state.id
  guardsUsed : ∀ guard ∈ machine.guards, ∃ transition ∈ machine.transitions,
    transition.guard = guard.id
  actionsUsed : ∀ action ∈ machine.actions, ∃ transition ∈ machine.transitions,
    transition.action = action.id

abbrev GuardValuation := GuardId → Bool

def transitionEnabled (state : StateId) (eventId : Nat)
    (guards : GuardValuation) (transition : Transition) : Bool :=
  transition.source = state && transition.event = eventId &&
    (transition.guard = 0 || guards transition.guard)

private def selectFrom (state : StateId) (eventId : Nat)
    (guards : GuardValuation) : List Transition → Option Transition
  | [] => none
  | transition :: remaining =>
      let selected := selectFrom state eventId guards remaining
      if transitionEnabled state eventId guards transition then
        match selected with
        | none => some transition
        | some current =>
            if transition.priority < current.priority
            then some transition else some current
      else selected

def selectTransition (machine : Machine) (state : StateId)
    (eventId : Nat) (guards : GuardValuation) : Option Transition :=
  selectFrom state eventId guards machine.transitions

theorem selectFrom_mem {state eventId : Nat} {guards : GuardValuation}
    {transitions : List Transition} {selected : Transition}
    (selection : selectFrom state eventId guards transitions = some selected) :
    selected ∈ transitions := by
  induction transitions with
  | nil => simp [selectFrom] at selection
  | cons transition remaining inductionHypothesis =>
      simp only [selectFrom] at selection
      by_cases enabled : transitionEnabled state eventId guards transition
      · simp [enabled] at selection
        cases recursive : selectFrom state eventId guards remaining with
        | none =>
            simp [recursive] at selection
            subst selected
            simp
        | some current =>
            simp [recursive] at selection
            by_cases earlier : transition.priority < current.priority
            · simp [earlier] at selection
              subst selected
              simp
            · simp [earlier] at selection
              subst selected
              exact List.mem_cons_of_mem transition
                (inductionHypothesis recursive)
      · simp [enabled] at selection
        exact List.mem_cons_of_mem transition (inductionHypothesis selection)

theorem selectTransition_mem {machine : Machine} {state eventId : Nat}
    {guards : GuardValuation} {selected : Transition}
    (selection : selectTransition machine state eventId guards = some selected) :
    selected ∈ machine.transitions :=
  selectFrom_mem selection

inductive MachineObservation where
  | value (ty : Ty) (token : Nat)
  | event (id : Nat) (payloadTy : Ty) (token : Nat)
  | state (id : StateId)
  | error (message : String)
  | done
  deriving Repr, DecidableEq

inductive Terminal where
  | running
  | done
  | error (message : String)
  deriving Repr, DecidableEq

structure Config where
  state : StateId
  stateToken : Nat
  terminal : Terminal
  trace : List MachineObservation
  consumedEvents : Nat
  deriving Repr, DecidableEq

def Config.WellTyped (machine : Machine) (config : Config) : Prop :=
  StateKnown machine config.state

inductive ActionResult where
  | success (stateToken : Nat) (observations : List MachineObservation)
  | error (message : String)
  deriving Repr, DecidableEq

abbrev ActionEvaluation := ActionId → ActionResult

def failureConfig (before : Config) (message : String) : Config :=
  { before with
    terminal := .error message
    trace := before.trace ++ [.error message]
    consumedEvents := before.consumedEvents + 1 }

def commitTarget (machine : Machine) (before : Config)
    (transition : Transition) (stateToken : Nat)
    (observations : List MachineObservation) : Config :=
  match lookupState machine.states transition.target with
  | none => failureConfig before "invalid transition target"
  | some target =>
      let tracePrefix := before.trace ++ observations ++ [.state target.id]
      match target.kind with
      | .active =>
          { before with
            state := target.id
            stateToken := stateToken
            trace := tracePrefix
            consumedEvents := before.consumedEvents + 1 }
      | .done =>
          { before with
            state := target.id
            stateToken := stateToken
            terminal := .done
            trace := tracePrefix ++ [.done]
            consumedEvents := before.consumedEvents + 1 }
      | .error =>
          { before with
            state := target.id
            stateToken := stateToken
            terminal := .error "entered error state"
            trace := tracePrefix ++ [.error "entered error state"]
            consumedEvents := before.consumedEvents + 1 }

def applyTransition (machine : Machine) (actions : ActionEvaluation)
    (before : Config) (transition : Transition) : Config :=
  let result := if transition.action = 0
    then ActionResult.success before.stateToken []
    else actions transition.action
  match result with
  | .error message => failureConfig before message
  | .success stateToken observations =>
      commitTarget machine before transition stateToken observations

def step (machine : Machine) (guards : GuardValuation)
    (actions : ActionEvaluation) (before : Config)
    (event : TypedEvent) : Option Config :=
  match before.terminal with
  | .done | .error _ => none
  | .running =>
      match machine.events.lookup event.id with
      | none => none
      | some expected =>
          if expected.payloadTy = event.payloadTy then
            match selectTransition machine before.state event.id guards with
            | none => some (failureConfig before "no enabled transition")
            | some transition =>
                some (applyTransition machine actions before transition)
          else none

def SmallStep (machine : Machine) (guards : GuardValuation)
    (actions : ActionEvaluation) (before : Config) (event : TypedEvent)
    (after : Config) : Prop :=
  step machine guards actions before event = some after

end CMetaCFlowCalculus.CFlow.Machine
