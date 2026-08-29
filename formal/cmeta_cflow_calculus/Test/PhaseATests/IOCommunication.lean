import CMetaCFlowCalculus.Proofs.IOCommunication

open CMetaCFlowCalculus.IO

namespace CMetaCFlowCalculus.Tests.IOCommunication

def identity : Communication.Identity :=
  { endpoint := 1, request := 1, generation := 1 }

def endpointIdentity : Communication.EndpointIdentity :=
  { endpoint := 1, generation := 1 }

def closeCommand : Communication.ControlCommand :=
  { kind := .close, endpoint := endpointIdentity }

example : endpointIdentity.Valid := by native_decide
example : closeCommand.Valid := by native_decide

def readiness : ReadinessDriver.State := ReadinessDriver.empty 2 2
def completion : CompletionDriver.State := CompletionDriver.empty 2 2
def blocking : BlockingDriver.State := BlockingDriver.empty 2 2

def terminalEvent : Communication.Event :=
  { identity := identity, result := .completed 64 }

def eofEvent : Communication.Event :=
  { identity := identity, result := .eof }

example : eofEvent.Valid := by native_decide

theorem readinessValid : readiness.Valid := by
  simp [ReadinessDriver.State.Valid, ReadinessDriver.State.project,
    readiness, ReadinessDriver.empty, Communication.empty,
    Communication.Contract.Valid, BoundedMpsc.State.Valid]

theorem completionValid : completion.Valid := by
  simp [CompletionDriver.State.Valid, CompletionDriver.State.project,
    completion, CompletionDriver.empty, Communication.empty,
    Communication.Contract.Valid, BoundedMpsc.State.Valid]

theorem blockingValid : blocking.Valid := by
  simp [BlockingDriver.State.Valid, BlockingDriver.State.project,
    blocking, BlockingDriver.empty, Communication.empty,
    Communication.Contract.Valid, BoundedMpsc.State.Valid]

example : readiness.project.pending = [] := rfl
example : completion.project.pending = [] := rfl
example : blocking.project.pending = [] := rfl

example :
    (ReadinessDriver.tryWatch readiness identity 1).state.project =
      (Communication.tryAdmit readiness.project identity).state :=
  ReadinessDriver.tryWatch_refines readiness identity 1 (by decide)

example :
    (CompletionDriver.trySubmit completion identity 11).state.project =
      (Communication.tryAdmit completion.project identity).state :=
  CompletionDriver.trySubmit_refines completion identity 11 (by decide)

example :
    (BlockingDriver.tryExecute blocking identity 17).state.project =
      (Communication.tryAdmit blocking.project identity).state :=
  BlockingDriver.tryExecute_refines blocking identity 17 (by decide)

def watched := ReadinessDriver.tryWatch readiness identity 1
def submitted := CompletionDriver.trySubmit completion identity 11
def queued := BlockingDriver.tryExecute blocking identity 17

example : watched.status = .accepted := by native_decide
example : submitted.status = .accepted := by native_decide
example : queued.status = .accepted := by native_decide

example : watched.state.Valid :=
  ReadinessDriver.tryWatch_preserves_valid readiness identity 1
    (by decide) readinessValid
example : submitted.state.Valid :=
  CompletionDriver.trySubmit_preserves_valid completion identity 11
    (by decide) completionValid
example : queued.state.Valid :=
  BlockingDriver.tryExecute_preserves_valid blocking identity 17
    (by decide) blockingValid

example :
    (ReadinessDriver.signal watched.state identity).project =
      watched.state.project :=
  ReadinessDriver.signal_preserves_projection watched.state identity
example :
    (CompletionDriver.begin submitted.state identity).project =
      submitted.state.project :=
  CompletionDriver.begin_preserves_projection submitted.state identity
example :
    (BlockingDriver.start queued.state identity).project = queued.state.project :=
  BlockingDriver.start_preserves_projection queued.state identity

def readinessTerminated :=
  ReadinessDriver.tryTerminate
    (ReadinessDriver.signal watched.state identity) terminalEvent
def completionTerminated :=
  CompletionDriver.tryComplete
    (CompletionDriver.begin submitted.state identity) terminalEvent
def blockingTerminated :=
  BlockingDriver.tryComplete
    (BlockingDriver.start queued.state identity) terminalEvent

example : readinessTerminated.status = .accepted := by native_decide
example : completionTerminated.status = .accepted := by native_decide
example : blockingTerminated.status = .accepted := by native_decide
example : readinessTerminated.state.project.pending = [] := by native_decide
example : completionTerminated.state.project.pending = [] := by native_decide
example : blockingTerminated.state.project.pending = [] := by native_decide

example :
    (CompletionDriver.tryComplete completionTerminated.state terminalEvent).state =
      completionTerminated.state := by
  apply CompletionDriver.stale_terminal_unchanged
  native_decide

example : (Communication.close submitted.state.project).pending =
    submitted.state.project.pending :=
  Communication.close_preserves_pending submitted.state.project

example :
    (Communication.tryAdmit
      (Communication.close submitted.state.project) identity).status =
      .closed := by native_decide

example :
    (CompletionDriver.tryObserve completionTerminated.state).state.project =
      (Communication.tryObserve completionTerminated.state.project).state :=
  (CompletionDriver.tryObserve_refines completionTerminated.state).2

end CMetaCFlowCalculus.Tests.IOCommunication
