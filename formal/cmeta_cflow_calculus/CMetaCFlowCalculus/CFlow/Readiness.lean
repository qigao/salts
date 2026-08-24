namespace CMetaCFlowCalculus.CFlow.Readiness

/-- A bounded generation advances only when a strictly larger representable
    value exists. `none` is retirement, never wraparound. -/
def nextGeneration (current limit : Nat) : Option Nat :=
  if current < limit then some (current + 1) else none

inductive Lifecycle where
  | free
  | open
  | closing
  | retired
  deriving Repr, DecidableEq

inductive Interest where
  | idle
  | arming
  | armed
  | unarming
  deriving Repr, DecidableEq

inductive Delivery where
  | idle
  | callback
  deriving Repr, DecidableEq

inductive Terminal where
  | none
  | reserved
  | delivering
  deriving Repr, DecidableEq

inductive Control where
  | none
  | register
  | arm
  | unarm
  | close
  deriving Repr, DecidableEq

/-- Logical facts owned by one bounded readiness slot. Booleans abstract
    callback/token/native ownership; counters retain their unbounded proof
    obligations instead of imposing a test-only finite capacity. -/
structure Slot where
  lifecycle : Lifecycle
  interest : Interest
  delivery : Delivery
  terminal : Terminal
  control : Control
  hasCallback : Bool
  hasArmToken : Bool
  nativeRegistered : Bool
  orphaned : Bool
  armWaiters : Nat
  apiBorrows : Nat
  deriving Repr, DecidableEq

namespace Slot

def inactive (slot : Slot) : Prop :=
  slot.lifecycle = .free ∨ slot.lifecycle = .retired

/-- The formal counterpart of the C state validator and reclaim predicate. -/
def Valid (slot : Slot) : Prop :=
  (slot.inactive →
    slot.interest = .idle ∧ slot.delivery = .idle ∧
    slot.terminal = .none ∧ slot.control = .none ∧
    slot.hasCallback = false ∧ slot.hasArmToken = false ∧
    slot.nativeRegistered = false ∧ slot.orphaned = false ∧
    slot.armWaiters = 0 ∧ slot.apiBorrows = 0) ∧
  ((slot.interest = .armed ∨ slot.interest = .arming) →
    slot.hasCallback = true ∧ slot.hasArmToken = true) ∧
  (slot.delivery = .callback → slot.hasCallback = true) ∧
  (slot.terminal = .reserved →
    slot.lifecycle = .open ∧ slot.interest = .armed ∧
    slot.delivery = .idle) ∧
  (slot.terminal = .delivering →
    slot.interest = .idle ∧ slot.delivery = .callback) ∧
  ((slot.interest = .arming) ↔ slot.control = .arm) ∧
  ((slot.interest = .unarming) ↔ slot.control = .unarm) ∧
  (slot.control = .register →
    slot.lifecycle = .open ∧ slot.interest = .idle) ∧
  (slot.control = .close → slot.lifecycle = .closing) ∧
  (slot.orphaned = true →
    slot.lifecycle = .closing ∧ slot.nativeRegistered = true)

def zero : Slot where
  lifecycle := .free
  interest := .idle
  delivery := .idle
  terminal := .none
  control := .none
  hasCallback := false
  hasArmToken := false
  nativeRegistered := false
  orphaned := false
  armWaiters := 0
  apiBorrows := 0

def beginRegister (slot : Slot) : Slot :=
  { slot with lifecycle := .open, control := .register }

def commitRegister (slot : Slot) : Slot :=
  { slot with control := .none, nativeRegistered := true }

def beginArm (slot : Slot) : Slot :=
  { slot with
    interest := .arming
    control := .arm
    hasCallback := true
    hasArmToken := true }

def commitArm (slot : Slot) : Slot :=
  { slot with interest := .armed, control := .none }

def rollbackArm (slot : Slot) : Slot :=
  { slot with
    interest := .idle
    control := .none
    hasCallback := false
    hasArmToken := false }

def dispatch (slot : Slot) : Slot :=
  { slot with
    interest := .idle
    delivery := .callback
    hasArmToken := false }

def completeCallback (slot : Slot) : Slot :=
  { slot with delivery := .idle, hasCallback := false }

def reserveTerminal (slot : Slot) : Slot :=
  { slot with terminal := .reserved }

def dispatchTerminal (slot : Slot) : Slot :=
  { slot with
    interest := .idle
    delivery := .callback
    terminal := .delivering
    hasArmToken := false }

def completeTerminal (slot : Slot) : Slot :=
  { slot with
    delivery := .idle
    terminal := .none
    hasCallback := false }

def beginUnarm (slot : Slot) : Slot :=
  { slot with interest := .unarming, control := .unarm }

def commitUnarm (slot : Slot) : Slot :=
  { slot with
    interest := .idle
    control := .none
    hasCallback := false
    hasArmToken := false }

def rollbackUnarmArmed (slot : Slot) : Slot :=
  { slot with interest := .armed, control := .none }

def rollbackUnarmIdle (slot : Slot) : Slot :=
  { slot with interest := .idle, control := .none }

def beginClose (slot : Slot) : Slot :=
  { slot with lifecycle := .closing, control := .close }

def rollbackClose (slot : Slot) : Slot :=
  { slot with lifecycle := .open, control := .none }

def commitNativeClose (slot : Slot) : Slot :=
  { slot with
    interest := .idle
    hasCallback := if slot.delivery = .callback then slot.hasCallback else false
    hasArmToken := false
    nativeRegistered := false }

def finishClose (slot : Slot) : Slot :=
  { slot with control := .none, apiBorrows := 0 }

def markOrphan (slot : Slot) : Slot :=
  { slot with control := .none, orphaned := true }

def retryOrphan (slot : Slot) : Slot :=
  { slot with control := .close }

def commitOrphanClose (slot : Slot) : Slot :=
  { slot with
    control := .none
    nativeRegistered := false
    orphaned := false }

def reclaim (slot : Slot) : Slot :=
  { slot with lifecycle := .free }

def retire (slot : Slot) : Slot :=
  { slot with lifecycle := .retired }

def acquireApiBorrow (slot : Slot) : Slot :=
  { slot with apiBorrows := slot.apiBorrows + 1 }

def releaseApiBorrow (slot : Slot) : Slot :=
  { slot with apiBorrows := slot.apiBorrows - 1 }

def addArmWaiter (slot : Slot) : Slot :=
  { slot with armWaiters := slot.armWaiters + 1 }

def removeArmWaiter (slot : Slot) : Slot :=
  { slot with armWaiters := slot.armWaiters - 1 }

end Slot

/-- Legal state changes. These constructors correspond to the locked commits
    in `platform/src/readiness.c`: register/arm/unarm/close begin, commit and
    rollback; `readiness_dispatch_slot`; terminal snapshot/fanout; orphan
    retry; slot reclaim/retire; and API-borrow/waiter accounting. Backend
    failure has an explicit rollback constructor; terminal reservation cannot
    be entered through close or unarm. Stale/duplicate backend events are
    stuttering steps and therefore do not need a state-changing constructor. -/
inductive Step : Slot → Slot → Prop where
  | registerBegin (slot : Slot)
      (free : slot.lifecycle = .free) :
      Step slot slot.beginRegister
  | registerCommit (slot : Slot)
      (control : slot.control = .register)
      (delivery : slot.delivery = .idle)
      (terminal : slot.terminal = .none)
      (callback : slot.hasCallback = false)
      (token : slot.hasArmToken = false)
      (native : slot.nativeRegistered = false)
      (orphan : slot.orphaned = false)
      (waiters : slot.armWaiters = 0)
      (borrows : slot.apiBorrows = 0) :
      Step slot slot.commitRegister
  | registerRollback (slot : Slot)
      (control : slot.control = .register)
      (delivery : slot.delivery = .idle)
      (terminal : slot.terminal = .none)
      (callback : slot.hasCallback = false)
      (token : slot.hasArmToken = false)
      (native : slot.nativeRegistered = false)
      (orphan : slot.orphaned = false)
      (waiters : slot.armWaiters = 0)
      (borrows : slot.apiBorrows = 0) :
      Step slot Slot.zero
  | armBegin (slot : Slot)
      (lifecycle : slot.lifecycle = .open)
      (interest : slot.interest = .idle)
      (delivery : slot.delivery = .idle)
      (terminal : slot.terminal = .none)
      (control : slot.control = .none)
      (native : slot.nativeRegistered = true) :
      Step slot slot.beginArm
  | armCommit (slot : Slot)
      (lifecycle : slot.lifecycle = .open)
      (interest : slot.interest = .arming)
      (delivery : slot.delivery = .idle)
      (terminal : slot.terminal = .none)
      (control : slot.control = .arm) :
      Step slot slot.commitArm
  | armRollback (slot : Slot)
      (lifecycle : slot.lifecycle = .open)
      (interest : slot.interest = .arming)
      (delivery : slot.delivery = .idle)
      (control : slot.control = .arm)
      (terminal : slot.terminal = .none) :
      Step slot slot.rollbackArm
  | normalDispatch (slot : Slot)
      (lifecycle : slot.lifecycle = .open)
      (interest : slot.interest = .armed)
      (delivery : slot.delivery = .idle)
      (terminal : slot.terminal = .none)
      (control : slot.control = .none) :
      Step slot slot.dispatch
  | callbackComplete (slot : Slot)
      (notArmed : slot.interest ≠ .armed)
      (delivery : slot.delivery = .callback)
      (terminal : slot.terminal = .none)
      (notArm : slot.control ≠ .arm) :
      Step slot slot.completeCallback
  | terminalReserve (slot : Slot)
      (lifecycle : slot.lifecycle = .open)
      (interest : slot.interest = .armed)
      (delivery : slot.delivery = .idle)
      (terminal : slot.terminal = .none)
      (control : slot.control = .none) :
      Step slot slot.reserveTerminal
  | terminalDispatch (slot : Slot)
      (terminal : slot.terminal = .reserved)
      (control : slot.control = .none)
      (orphan : slot.orphaned = false) :
      Step slot slot.dispatchTerminal
  | terminalComplete (slot : Slot)
      (delivery : slot.delivery = .callback)
      (terminal : slot.terminal = .delivering)
      (control : slot.control = .none) :
      Step slot slot.completeTerminal
  | unarmBeginArmed (slot : Slot)
      (lifecycle : slot.lifecycle = .open)
      (interest : slot.interest = .armed)
      (delivery : slot.delivery = .idle)
      (terminal : slot.terminal = .none)
      (control : slot.control = .none) :
      Step slot slot.beginUnarm
  | unarmBeginDelivery (slot : Slot)
      (lifecycle : slot.lifecycle = .open)
      (interest : slot.interest = .idle)
      (delivery : slot.delivery = .callback)
      (terminal : slot.terminal = .none)
      (control : slot.control = .none) :
      Step slot slot.beginUnarm
  | unarmCommit (slot : Slot)
      (lifecycle : slot.lifecycle = .open)
      (interest : slot.interest = .unarming)
      (delivery : slot.delivery = .idle)
      (control : slot.control = .unarm) :
      Step slot slot.commitUnarm
  | unarmRollbackArmed (slot : Slot)
      (lifecycle : slot.lifecycle = .open)
      (interest : slot.interest = .unarming)
      (delivery : slot.delivery = .idle)
      (control : slot.control = .unarm)
      (callback : slot.hasCallback = true)
      (token : slot.hasArmToken = true) :
      Step slot slot.rollbackUnarmArmed
  | unarmRollbackIdle (slot : Slot)
      (lifecycle : slot.lifecycle = .open)
      (interest : slot.interest = .unarming)
      (delivery : slot.delivery = .idle)
      (control : slot.control = .unarm)
      (callback : slot.hasCallback = false)
      (token : slot.hasArmToken = false) :
      Step slot slot.rollbackUnarmIdle
  | closeBegin (slot : Slot)
      (lifecycle : slot.lifecycle = .open)
      (terminal : slot.terminal = .none)
      (control : slot.control = .none) :
      Step slot slot.beginClose
  | closeRollback (slot : Slot)
      (lifecycle : slot.lifecycle = .closing)
      (terminal : slot.terminal = .none)
      (control : slot.control = .close)
      (orphan : slot.orphaned = false) :
      Step slot slot.rollbackClose
  | closeCommit (slot : Slot)
      (lifecycle : slot.lifecycle = .closing)
      (terminal : slot.terminal = .none)
      (control : slot.control = .close)
      (native : slot.nativeRegistered = true)
      (orphan : slot.orphaned = false) :
      Step slot slot.commitNativeClose
  | closeFinish (slot : Slot)
      (lifecycle : slot.lifecycle = .closing)
      (interest : slot.interest = .idle)
      (delivery : slot.delivery = .idle)
      (terminal : slot.terminal = .none)
      (control : slot.control = .close)
      (callback : slot.hasCallback = false)
      (token : slot.hasArmToken = false)
      (native : slot.nativeRegistered = false)
      (orphan : slot.orphaned = false)
      (waiters : slot.armWaiters = 0)
      (selfBorrow : slot.apiBorrows = 1) :
      Step slot slot.finishClose
  | orphanMark (slot : Slot)
      (lifecycle : slot.lifecycle = .closing)
      (interest : slot.interest = .idle)
      (delivery : slot.delivery = .idle)
      (terminal : slot.terminal = .none)
      (control : slot.control = .close)
      (callback : slot.hasCallback = false)
      (token : slot.hasArmToken = false)
      (native : slot.nativeRegistered = true)
      (waiters : slot.armWaiters = 0)
      (borrows : slot.apiBorrows = 0) :
      Step slot slot.markOrphan
  | orphanRetry (slot : Slot)
      (lifecycle : slot.lifecycle = .closing)
      (control : slot.control = .none)
      (orphan : slot.orphaned = true) :
      Step slot slot.retryOrphan
  | orphanCommit (slot : Slot)
      (lifecycle : slot.lifecycle = .closing)
      (control : slot.control = .close)
      (orphan : slot.orphaned = true)
      (native : slot.nativeRegistered = true) :
      Step slot slot.commitOrphanClose
  | reclaim (slot : Slot)
      (lifecycle : slot.lifecycle = .closing)
      (interest : slot.interest = .idle)
      (delivery : slot.delivery = .idle)
      (terminal : slot.terminal = .none)
      (control : slot.control = .none)
      (callback : slot.hasCallback = false)
      (token : slot.hasArmToken = false)
      (native : slot.nativeRegistered = false)
      (orphan : slot.orphaned = false)
      (waiters : slot.armWaiters = 0)
      (borrows : slot.apiBorrows = 0) :
      Step slot slot.reclaim
  | retire (slot : Slot)
      (free : slot.lifecycle = .free) :
      Step slot slot.retire
  | apiBorrowAcquire (slot : Slot)
      (active : ¬slot.inactive) :
      Step slot slot.acquireApiBorrow
  | apiBorrowRelease (slot : Slot)
      (active : ¬slot.inactive)
      (borrowed : 0 < slot.apiBorrows) :
      Step slot slot.releaseApiBorrow
  | armWaiterAdd (slot : Slot)
      (lifecycle : slot.lifecycle = .open)
      (delivery : slot.delivery = .callback)
      (terminal : slot.terminal = .none)
      (control : slot.control = .none) :
      Step slot slot.addArmWaiter
  | armWaiterRemove (slot : Slot)
      (active : ¬slot.inactive)
      (waiting : 0 < slot.armWaiters) :
      Step slot slot.removeArmWaiter

inductive Trace : Slot → List Slot → Slot → Prop where
  | nil (slot : Slot) : Trace slot [] slot
  | cons {before middle after : Slot} {remaining : List Slot} :
      Step before middle → Trace middle remaining after →
      Trace before (middle :: remaining) after

def OrdinaryDispatchEnabled (slot : Slot) : Prop :=
  slot.lifecycle = .open ∧ slot.interest = .armed ∧
  slot.delivery = .idle ∧ slot.terminal = .none ∧ slot.control = .none

def TerminalDispatchEnabled (slot : Slot) : Prop :=
  slot.lifecycle = .open ∧ slot.interest = .armed ∧
  slot.delivery = .idle ∧ slot.terminal = .reserved ∧ slot.control = .none

inductive ArmDecision where
  | proceed
  | wait
  | busy
  | already
  | shutdown
  deriving Repr, DecidableEq

/-- Pure arm admission decision. A wake does not authorize a transition: an
    external waiter calls this function again after callback completion. -/
def decideArm (selfCallback reactorOpen : Bool) (slot : Slot) : ArmDecision :=
  if selfCallback && slot.delivery = .callback then .busy
  else if slot.lifecycle ≠ .open then .busy
  else if reactorOpen = false then .shutdown
  else if slot.terminal ≠ .none then .busy
  else if slot.delivery = .callback then .wait
  else if slot.interest = .armed then .already
  else if slot.interest = .idle ∧ slot.control = .none then .proceed
  else .busy

inductive ShutdownDecision where
  | proceed
  | wait
  | busy
  | already
  deriving Repr, DecidableEq

/-- Pure reactor-shutdown admission decision. The callback check precedes the
    terminalization wait because a reactor callback cannot wait for itself to
    finish or join its own backend thread. -/
def decideShutdown (selfCallback terminalizing shutdownComplete
    shutdownInflight : Bool) : ShutdownDecision :=
  if selfCallback then .busy
  else if terminalizing then .wait
  else if shutdownComplete || shutdownInflight then .already
  else .proceed

/-- Public-handle admission abstraction. `slotEpoch = none` is the zero state;
    epochs distinguish a new registration from the consumed one. -/
structure Admission where
  closed : Bool
  entrants : Nat
  slotEpoch : Option Nat
  deriving Repr, DecidableEq

namespace Admission

def zero : Admission := { closed := false, entrants := 0, slotEpoch := none }

def enter (state : Admission) (limit : Nat) : Option Admission :=
  if state.closed then none
  else if state.entrants < limit then
    some { state with entrants := state.entrants + 1 }
  else none

def leave (state : Admission) : Option Admission :=
  if state.entrants = 0 then none
  else some { state with entrants := state.entrants - 1 }

/-- Register owns `closed + zero entrants` before clearing or publishing. -/
def reserveRegister (state : Admission) : Option Admission :=
  if state.closed = false ∧ state.entrants = 0 ∧ state.slotEpoch = none then
    some { state with closed := true }
  else none

def publishRegister (state : Admission) (epoch : Nat) : Option Admission :=
  if state.closed = true ∧ state.entrants = 0 ∧ state.slotEpoch = none then
    some { state with slotEpoch := some epoch }
  else none

def openRegister (state : Admission) : Option Admission :=
  if state.closed = true ∧ state.entrants = 0 ∧ state.slotEpoch.isSome then
    some { state with closed := false }
  else none

def closeGate (state : Admission) : Option Admission :=
  if state.closed then none else some { state with closed := true }

def reopenAfterFailure (state : Admission) : Option Admission :=
  if state.closed then some { state with closed := false } else none

/-- The close owner is the final entrant when it consumes the slot. -/
def consumeByClose (state : Admission) : Option Admission :=
  if state.closed = true ∧ state.entrants = 1 ∧ state.slotEpoch.isSome then
    some { state with entrants := 0, slotEpoch := none }
  else none

def reset (state : Admission) : Option Admission :=
  if state.closed = true ∧ state.entrants = 0 ∧ state.slotEpoch = none then
    some zero
  else none

end Admission

end CMetaCFlowCalculus.CFlow.Readiness
