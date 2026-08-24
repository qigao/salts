import CMetaCFlowCalculus.CFlow.MachineRuntime

namespace CMetaCFlowCalculus.CFlow.ManagedMachineState

open CMetaCFlowCalculus.CMeta
open CMetaCFlowCalculus.CFlow.MachineRuntime

/-- Semantic identity of one lifecycle-managed state value. The token models
    one concrete constructed resource, not a descriptor address. -/
structure ManagedValue where
  ty : Ty
  token : Nat
  deriving Repr, DecidableEq

inductive ResourcePhase where
  | live
  | destroyed
  deriving Repr, DecidableEq

structure ResourceRecord where
  ty : Ty
  phase : ResourcePhase
  deriving Repr, DecidableEq

namespace ResourceRecord

def live (ty : Ty) : ResourceRecord :=
  { ty := ty, phase := .live }

def destroyed (ty : Ty) : ResourceRecord :=
  { ty := ty, phase := .destroyed }

end ResourceRecord

/-- The single fact source for the lifecycle state of every constructed token. -/
abbrev ResourceLedger := Nat → Option ResourceRecord

namespace ResourceLedger

def set (ledger : ResourceLedger) (value : ManagedValue)
    (phase : ResourcePhase) : ResourceLedger :=
  fun token =>
    if token = value.token then
      some { ty := value.ty, phase := phase }
    else
      ledger token

@[simp] theorem set_same (ledger : ResourceLedger) (value : ManagedValue)
    (phase : ResourcePhase) :
    (ledger.set value phase) value.token =
      some { ty := value.ty, phase := phase } := by
  simp [set]

@[simp] theorem set_other (ledger : ResourceLedger) (value : ManagedValue)
    (phase : ResourcePhase) (token : Nat) (different : token ≠ value.token) :
    (ledger.set value phase) token = ledger token := by
  simp [set, different]

end ResourceLedger

structure ManagedControl where
  lifecycle : ControlLifecycle
  worker : WorkerPhase
  source : Option ManagedValue
  staged : Option ManagedValue
  ledger : ResourceLedger
  constructed : Nat
  destroyed : Nat
  completed : Nat
  cancelled : Nat

namespace ManagedControl

def occupied : Option ManagedValue → Nat
  | none => 0
  | some _ => 1

def liveCount (control : ManagedControl) : Nat :=
  occupied control.source + occupied control.staged

/-- Construction/destruction conservation. Live resources are derived only
    from the authoritative source and staged slots. -/
def Balanced (control : ManagedControl) : Prop :=
  control.constructed = control.destroyed + control.liveCount

def ready (source : ManagedValue) : ManagedControl :=
  { lifecycle := .open
    worker := .idle
    source := some source
    staged := none
    ledger := fun token =>
      if token = source.token then some (ResourceRecord.live source.ty)
      else none
    constructed := 1
    destroyed := 0
    completed := 0
    cancelled := 0 }

/-- A failed or inadmissible copy is transactional. A successful copy
    constructs one fresh staged target while the control is open and idle. -/
def stageCopy (control : ManagedControl) (target : ManagedValue)
    (copied : Bool) : ManagedControl :=
  match copied, control.lifecycle, control.worker, control.source,
      control.staged, control.ledger target.token with
  | true, .open, .idle, some _, none, none =>
      { control with
        worker := .executing
        staged := some target
        ledger := control.ledger.set target .live
        constructed := control.constructed + 1 }
  | _, _, _, _, _, _ => control

def requestCancel (control : ManagedControl) : ManagedControl :=
  match control.lifecycle with
  | .terminal => control
  | .open | .closeRequested | .cancelRequested =>
      { control with lifecycle := .cancelRequested }

def beginCommit (control : ManagedControl) : Option ManagedControl :=
  match control.worker, control.lifecycle, control.source, control.staged with
  | .executing, .open, some _, some _
  | .executing, .closeRequested, some _, some _ =>
      some { control with worker := .committing }
  | _, _, _, _ => none

/-- Commit destroys the old committed value and moves the already-live staged
    value into the source slot. Cancellation after `beginCommit` terminates
    later work but cannot revoke this resource transfer. -/
def commit (control : ManagedControl) : Option ManagedControl :=
  match control.worker, control.lifecycle, control.source, control.staged with
  | .committing, .open, some source, some target =>
      some { control with
        worker := .idle
        source := some target
        staged := none
        ledger := control.ledger.set source .destroyed
        destroyed := control.destroyed + 1
        completed := control.completed + 1 }
  | .committing, .closeRequested, some source, some target
  | .committing, .cancelRequested, some source, some target =>
      some { control with
        lifecycle := .terminal
        worker := .idle
        source := some target
        staged := none
        ledger := control.ledger.set source .destroyed
        destroyed := control.destroyed + 1
        completed := control.completed + 1 }
  | _, _, _, _ => none

/-- Cancel-before-commit destroys only the staged target and preserves the
    committed source. -/
def discardCancelled (control : ManagedControl) : Option ManagedControl :=
  match control.worker, control.lifecycle, control.source, control.staged with
  | .executing, .cancelRequested, some source, some target =>
      some { control with
        lifecycle := .terminal
        worker := .idle
        source := some source
        staged := none
        ledger := control.ledger.set target .destroyed
        destroyed := control.destroyed + 1
        cancelled := control.cancelled + 1 }
  | _, _, _, _ => none

/-- Dispose the sole committed terminal value. Clearing the source slot makes
    a second disposal unrepresentable as another successful transition. -/
def dispose (control : ManagedControl) : Option ManagedControl :=
  match control.lifecycle, control.worker, control.source, control.staged with
  | .terminal, .idle, some source, none =>
      some { control with
        source := none
        ledger := control.ledger.set source .destroyed
        destroyed := control.destroyed + 1 }
  | _, _, _, _ => none

def cancelPath (source target : ManagedValue) : Option ManagedControl :=
  discardCancelled (requestCancel (stageCopy (ready source) target true))

def commitPath (source target : ManagedValue) : Option ManagedControl := do
  let staged := stageCopy (ready source) target true
  let begun ← beginCommit staged
  commit (requestCancel begun)

end ManagedControl

export ManagedControl
  (Balanced beginCommit cancelPath commit commitPath discardCancelled dispose
   liveCount ready requestCancel stageCopy)

end CMetaCFlowCalculus.CFlow.ManagedMachineState
