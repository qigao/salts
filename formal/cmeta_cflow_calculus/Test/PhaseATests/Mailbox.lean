import CMetaCFlowCalculus.Proofs.Mailbox

open CMetaCFlowCalculus.CMeta
open CMetaCFlowCalculus.CFlow.Mailbox

namespace CMetaCFlowCalculus.Tests.Mailbox

def intTy : Ty := .named "Int"
def boolTy : Ty := .bool

def intEventType : EventType where
  id := 1
  payloadTy := intTy

def boolEventType : EventType where
  id := 2
  payloadTy := boolTy

def intEvent : TypedEvent where
  id := 1
  payloadTy := intTy
  payload := { token := 11 }

def boolEvent : TypedEvent where
  id := 2
  payloadTy := boolTy
  payload := { token := 12 }

def mismatchedIntEvent : TypedEvent where
  id := 1
  payloadTy := boolTy
  payload := { token := 13 }

def emptyMailbox : State where
  schema := [intEventType, boolEventType]
  capacity := 2
  queue := []
  terminal := .open

example : State.Valid emptyMailbox := by
  simp [State.Valid, emptyMailbox]

example : Schema.Valid emptyMailbox.schema := by
  simp [Schema.Valid, emptyMailbox, intEventType, boolEventType]

example : (send emptyMailbox intEvent).1 = .ok := by
  rfl

example : (send (send emptyMailbox intEvent).2 boolEvent).2.queue =
    [intEvent, boolEvent] := by
  rfl

example : (send (send (send emptyMailbox intEvent).2 boolEvent).2 intEvent).1 =
    .full := by
  rfl

example : (send emptyMailbox mismatchedIntEvent).1 = .typeMismatch := by
  rfl

example : (receive (send emptyMailbox intEvent).2).1 = .received intEvent := by
  rfl

example : (send (close emptyMailbox).2 intEvent).1 = .closed := by
  rfl

example : (cancel (send emptyMailbox intEvent).2).2.queue = [] := by
  rfl

example : (send emptyMailbox intEvent).2.Valid :=
  send_preserves_valid emptyMailbox intEvent (by
    simp [State.Valid, emptyMailbox])

example : (close (send emptyMailbox intEvent).2).2.queue =
    (send emptyMailbox intEvent).2.queue :=
  close_preserves_queue _

example : (cancel (send emptyMailbox intEvent).2).2.terminal = .cancelled :=
  (cancel_empties_queue _).2

end CMetaCFlowCalculus.Tests.Mailbox
