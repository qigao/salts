import Std

namespace CMetaCFlowCalculus.CFlow

inductive MachineStateKind where
  | active
  | done
  | error
  deriving Repr, DecidableEq, BEq

namespace MachineStateKind

def all : List MachineStateKind := [.active, .done, .error]

end MachineStateKind

inductive MachineActionObservationKind where
  | none
  | value
  | event
  deriving Repr, DecidableEq, BEq

namespace MachineActionObservationKind

def all : List MachineActionObservationKind := [.none, .value, .event]

end MachineActionObservationKind

/-- One stable C enum row owned by the proof-facing Machine schema. -/
structure MachineEnumRow (α : Type) where
  kind : α
  cName : String
  value : Nat
  deriving Repr, DecidableEq, BEq

inductive MachineSchemaError where
  | emptyStateKinds
  | emptyActionObservations
  | incompleteStateKinds
  | incompleteActionObservations
  | duplicateKind
  | duplicateCName
  | duplicateValue
  deriving Repr, DecidableEq, BEq

def machineSchemaVersion : Nat := 1

def stateKindRows : List (MachineEnumRow MachineStateKind) := [
  { kind := .active, cName := "ACTIVE", value := 0 },
  { kind := .done, cName := "DONE", value := 1 },
  { kind := .error, cName := "ERROR", value := 2 }
]

def actionObservationRows :
    List (MachineEnumRow MachineActionObservationKind) := [
  { kind := .none, cName := "NONE", value := 0 },
  { kind := .value, cName := "VALUE", value := 1 },
  { kind := .event, cName := "EVENT", value := 2 }
]

namespace MachineSchema

private def unique [BEq α] : List α → Bool
  | [] => true
  | value :: rest => !rest.contains value && unique rest

private def rowsValid [BEq α]
    (rows : List (MachineEnumRow α)) : Except MachineSchemaError Unit :=
  if !unique (rows.map (·.kind)) then .error .duplicateKind
  else if !unique (rows.map (·.cName)) then .error .duplicateCName
  else if !unique (rows.map (·.value)) then .error .duplicateValue
  else .ok ()

def validate : Except MachineSchemaError Unit :=
  if stateKindRows.isEmpty then .error .emptyStateKinds
  else if actionObservationRows.isEmpty then .error .emptyActionObservations
  else if stateKindRows.map (·.kind) != MachineStateKind.all then
    .error .incompleteStateKinds
  else if actionObservationRows.map (·.kind) !=
      MachineActionObservationKind.all then
    .error .incompleteActionObservations
  else
    match rowsValid stateKindRows with
    | .error failure => .error failure
    | .ok () => rowsValid actionObservationRows

def WellFormed : Prop := validate = .ok ()

theorem wellFormed : WellFormed := by rfl

end MachineSchema

end CMetaCFlowCalculus.CFlow
