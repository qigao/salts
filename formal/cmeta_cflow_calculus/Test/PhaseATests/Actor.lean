import CMetaCFlowCalculus.CFlow.Actor
import CMetaCFlowCalculus.Proofs.Actor
import PhaseATests.Machine
import PhaseATests.MachineRuntime

namespace CMetaCFlowCalculus.Tests.ActorLifecycle

open CMetaCFlowCalculus.CFlow.Actor
open CMetaCFlowCalculus.CFlow.Mailbox
open CMetaCFlowCalculus.CFlow.MachineRuntime

example :
    [Lifecycle.start, .running, .stopping, .stopped, .failed].length = 5 := by
  native_decide

def eventTy : CMeta.Ty := .named "ActorEvent"

def eventType : EventType := { id := 1, payloadTy := eventTy }

def firstEvent : TypedEvent :=
  { id := 1, payloadTy := eventTy, payload := { token := 10 } }

def secondEvent : TypedEvent :=
  { id := 1, payloadTy := eventTy, payload := { token := 11 } }

def wrongTypeEvent : TypedEvent :=
  { id := 1, payloadTy := .bool, payload := { token := 12 } }

def unknownEvent : TypedEvent :=
  { id := 2, payloadTy := eventTy, payload := { token := 13 } }

def mailbox (capacity : Nat) : CMetaCFlowCalculus.CFlow.Mailbox.State :=
  { schema := [eventType], capacity := capacity, queue := [], terminal := .open }

def initial : CMetaCFlowCalculus.CFlow.Actor.State := initState (mailbox 1)
def running : CMetaCFlowCalculus.CFlow.Actor.State := (start initial true).2
def accepted : SendResult := send running firstEvent
def full : SendResult := send accepted.state secondEvent
def stopping : CMetaCFlowCalculus.CFlow.Actor.State := (requestStop running).2
def stopped : CMetaCFlowCalculus.CFlow.Actor.State := settle stopping
def failed : CMetaCFlowCalculus.CFlow.Actor.State := fail running
def stale : CMetaCFlowCalculus.CFlow.Actor.State := destroy running
def stoppedBeforeStart : LifecycleStatus ×
    CMetaCFlowCalculus.CFlow.Actor.State := requestStop initial

example : initial.Valid := by
  apply initState_valid
  · simp [CMetaCFlowCalculus.CFlow.Mailbox.State.Valid, mailbox]
  · rfl

example : (send initial firstEvent).status = .notStarted := by native_decide
example : stoppedBeforeStart.1 = .ok := by native_decide
example : stoppedBeforeStart.2.lifecycle = .stopped := by native_decide
example : (start initial false).1 = .failed := by native_decide
example : (start initial false).2.lifecycle = .failed := by native_decide
example : (send running unknownEvent).status = .invalidArgument := by native_decide
example : (send running wrongTypeEvent).status = .typeMismatch := by native_decide
example : accepted.status = .accepted := by native_decide
example : accepted.state.mailbox.queue = [firstEvent] := by rfl
example : full.status = .full := by native_decide
example : full.state.mailbox.queue.length = 1 := by native_decide
example : full.state.mailbox.queue = [firstEvent] := by rfl
example : (send stopping firstEvent).status = .stopping := by native_decide
example : stopping.mailbox.queue = [] := by rfl
example : stopping.mailbox.queue.length = 0 := by native_decide
example : stopping.mailbox.terminal = .cancelled := by native_decide
example : stopped.lifecycle = .stopped := by native_decide
example : (send stopped firstEvent).status = .stopped := by native_decide
example : failed.lifecycle = .failed := by native_decide
example : (send failed firstEvent).status = .failed := by native_decide
example : (settle running).lifecycle = .failed := by native_decide
example : stale.live = false := by native_decide
example : (send stale unknownEvent).status = .stale := by native_decide
example : (start stopped true).1 = .stopped := by native_decide
example : (start failed true).1 = .failed := by native_decide

def replayMailbox : CMetaCFlowCalculus.CFlow.Mailbox.State := mailbox 6
def replayFirst : CMetaCFlowCalculus.CFlow.Actor.State :=
  (start (initState replayMailbox) true).2
def replaySecond : CMetaCFlowCalculus.CFlow.Actor.State :=
  (start (initState (mailbox 6)) true).2
def replayEvents : List TypedEvent :=
  [ { firstEvent with payload := { token := 4 } }
  , { firstEvent with payload := { token := 1 } }
  , { firstEvent with payload := { token := 7 } }
  , { firstEvent with payload := { token := 3 } }
  , { firstEvent with payload := { token := 9 } }
  , { firstEvent with payload := { token := 2 } } ]

example : (sendMany replayFirst replayEvents).mailbox.queue = replayEvents := by
  rfl

example :
    (sendMany replayFirst replayEvents).mailbox.queue.map
        (fun event => event.payload.token) =
      (sendMany replaySecond replayEvents).mailbox.queue.map
        (fun event => event.payload.token) := by
  native_decide

def machineMailbox : CMetaCFlowCalculus.CFlow.Mailbox.State :=
  { schema := CMetaCFlowCalculus.Tests.Machine.machine.events
    capacity := 1
    queue := []
    terminal := .open }

def machineActorBefore : CMetaCFlowCalculus.CFlow.Actor.State :=
  (start (initState machineMailbox) true).2

def machineActorAfter : CMetaCFlowCalculus.CFlow.Actor.State :=
  (send machineActorBefore CMetaCFlowCalculus.Tests.Machine.trigger).state

def machineMailboxAfter : CMetaCFlowCalculus.CFlow.Mailbox.State :=
  (CMetaCFlowCalculus.CFlow.Mailbox.receive machineActorAfter.mailbox).2

example : MachineHandoff CMetaCFlowCalculus.Tests.Machine.machine
    CMetaCFlowCalculus.Tests.Machine.onlyFirstEnabled
    CMetaCFlowCalculus.Tests.Machine.successfulActions
    machineActorBefore machineActorAfter
    CMetaCFlowCalculus.Tests.Machine.initialConfig
    CMetaCFlowCalculus.Tests.MachineRuntime.doneConfig
    CMetaCFlowCalculus.Tests.Machine.trigger machineMailboxAfter := by
  apply MachineHandoff.delivered
  · rfl
  · rfl
  · apply RuntimeStep.transition
    · show CMetaCFlowCalculus.CFlow.Machine.step
        CMetaCFlowCalculus.Tests.Machine.machine
        CMetaCFlowCalculus.Tests.Machine.onlyFirstEnabled
        CMetaCFlowCalculus.Tests.Machine.successfulActions
        CMetaCFlowCalculus.Tests.Machine.initialConfig
        CMetaCFlowCalculus.Tests.Machine.trigger =
          some CMetaCFlowCalculus.Tests.MachineRuntime.doneConfig
      native_decide
    · native_decide

end CMetaCFlowCalculus.Tests.ActorLifecycle
