namespace CMetaCFlowCalculus.CNet.Session

/-- The one-owner lifecycle states implemented by `cnet_session_state`. -/
inductive State where
  | free
  | reserved
  | resolving
  | transportConnecting
  | protocolHandshaking
  | openState
  | draining
  | terminal
  | retired
  deriving Repr, DecidableEq

/-- A terminal slot records exactly one public outcome. -/
inductive TerminalOutcome where
  | closed
  | failed
  deriving Repr, DecidableEq

def IsClosed : TerminalOutcome → Prop
  | .closed => True
  | .failed => False

def IsFailed : TerminalOutcome → Prop
  | .closed => False
  | .failed => True

/-- Connection lifecycle steps correspond to the C transition admission table.
    Recycle is intentionally modeled separately because it changes slot
    identity, not connection lifecycle. -/
inductive LifecycleStep : State → State → Prop where
  | reserve : LifecycleStep .free .reserved
  | beginResolve : LifecycleStep .reserved .resolving
  | beginTransportReserved : LifecycleStep .reserved .transportConnecting
  | beginTransportResolved : LifecycleStep .resolving .transportConnecting
  | beginProtocol : LifecycleStep .transportConnecting .protocolHandshaking
  | openTransport : LifecycleStep .transportConnecting .openState
  | openProtocol : LifecycleStep .protocolHandshaking .openState
  | closeReserved : LifecycleStep .reserved .draining
  | closeResolving : LifecycleStep .resolving .draining
  | closeTransport : LifecycleStep .transportConnecting .draining
  | closeProtocol : LifecycleStep .protocolHandshaking .draining
  | closeOpen : LifecycleStep .openState .draining
  | failReserved : LifecycleStep .reserved .terminal
  | failResolving : LifecycleStep .resolving .terminal
  | failTransport : LifecycleStep .transportConnecting .terminal
  | failProtocol : LifecycleStep .protocolHandshaking .terminal
  | failOpen : LifecycleStep .openState .terminal
  | failDraining : LifecycleStep .draining .terminal
  | finishClose : LifecycleStep .draining .terminal

/-- Slot reuse advances the generation or permanently retires an exhausted
    slot; it never wraps a generation. -/
inductive Recycle (limit : Nat) : State → Nat → State → Nat → Prop where
  | reuse {generation : Nat} (available : generation < limit) :
      Recycle limit .terminal generation .free (generation + 1)
  | retire : Recycle limit .terminal limit .retired limit

/-- Rejected command publication releases only an unowned reservation. It uses
    the same no-wrap generation rule as terminal recycling. -/
inductive ReleaseReservation (limit : Nat) : State → Nat → State → Nat → Prop where
  | reuse {generation : Nat} (available : generation < limit) :
      ReleaseReservation limit .reserved generation .free (generation + 1)
  | retire : ReleaseReservation limit .reserved limit .retired limit

end CMetaCFlowCalculus.CNet.Session
