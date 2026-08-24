import CMetaCFlowCalculus.CFlow.TimerEvent
import CMetaCFlowCalculus.Proofs.TimerEvent

namespace CMetaCFlowCalculus.Tests.TimerEvent

open CMetaCFlowCalculus.CMeta
open CMetaCFlowCalculus.CFlow.Mailbox
open CMetaCFlowCalculus.CFlow.TimerEvent

def event : TypedEvent :=
  { id := 1, payloadTy := .bool, payload := { token := 7 } }

def mailbox : CMetaCFlowCalculus.CFlow.Mailbox.State :=
  { schema := [{ id := 1, payloadTy := .bool }]
    capacity := 1
    queue := []
    terminal := .open }

def initial : CMetaCFlowCalculus.CFlow.TimerEvent.State :=
  { capacity := 2
    pending := []
    claimed := none
    nextId := 1
    nextOrder := 0
    terminal := .open
    mailbox := mailbox }

example : (schedule initial 10 event).status = .ok := by native_decide

def secondEvent : TypedEvent :=
  { id := 1, payloadTy := .bool, payload := { token := 8 } }

def firstScheduled : ScheduleResult := schedule initial 10 event
def bothScheduled : ScheduleResult := schedule firstScheduled.state 10 secondEvent
def firstClaimed : ClaimResult := claim bothScheduled.state 10

example : firstScheduled.timerId = some 1 := by native_decide
example : bothScheduled.timerId = some 2 := by native_decide
example : firstClaimed.timer.map Timer.id = some 1 := by native_decide
example : firstClaimed.state.pending.map Timer.id = [2] := by native_decide
example : (cancel firstClaimed.state 1).status = .fireWon := by native_decide
example : (commit firstClaimed.state).status = .delivered := by native_decide
example : (commit firstClaimed.state).state.mailbox.queue.length = 1 := by
  native_decide
example : (commit (commit firstClaimed.state).state).status =
    .invalidArgument := by native_decide

def cancelledBeforeClaim : ControlResult := cancel firstScheduled.state 1

example : cancelledBeforeClaim.status = .ok := by native_decide
example : (claim cancelledBeforeClaim.state 10).status = .notReady := by
  native_decide
example : (claim firstScheduled.state 9).status = .notReady := by native_decide
example : (claim firstScheduled.state 10).status = .ok := by native_decide

example : (close bothScheduled.state).state.pending = [] := by native_decide
example : (claim (close bothScheduled.state).state 10).status = .closed := by
  native_decide

def fullMailbox : CMetaCFlowCalculus.CFlow.Mailbox.State :=
  { mailbox with queue := [event] }

def fullState : CMetaCFlowCalculus.CFlow.TimerEvent.State :=
  { initial with mailbox := fullMailbox }

def fullClaim : ClaimResult := claim (schedule fullState 0 secondEvent).state 0

example : (commit fullClaim.state).status = .mailboxRejected := by native_decide
example : (commit fullClaim.state).mailboxStatus = some .full := by native_decide

theorem initialValid : initial.Valid := by
  simp [CMetaCFlowCalculus.CFlow.TimerEvent.State.Valid, initial]

example : initial.Valid := initialValid
example : (schedule initial 10 event).state.Valid :=
  schedule_preserves_valid initial 10 event initialValid
example : firstClaimed.state.Valid :=
  claim_preserves_valid bothScheduled.state 10
    (schedule_preserves_valid firstScheduled.state 10 secondEvent
      (schedule_preserves_valid initial 10 event initialValid))

end CMetaCFlowCalculus.Tests.TimerEvent
