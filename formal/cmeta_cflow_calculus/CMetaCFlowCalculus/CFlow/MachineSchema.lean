import Std

namespace CMetaCFlowCalculus.CFlow

/-- One stable C enum row owned by the proof-facing Machine schema. -/
structure MachineEnumRow where
  leanName : String
  cName : String
  value : Nat
  deriving Repr, DecidableEq, BEq

inductive MachineSchemaError where
  | emptyStateKinds
  | emptyActionObservations
  | duplicateLeanName
  | duplicateCName
  | duplicateValue
  deriving Repr, DecidableEq, BEq

def machineSchemaVersion : Nat := 1

def stateKindRows : List MachineEnumRow := [
  { leanName := "active", cName := "ACTIVE", value := 0 },
  { leanName := "done", cName := "DONE", value := 1 },
  { leanName := "error", cName := "ERROR", value := 2 }
]

def actionObservationRows : List MachineEnumRow := [
  { leanName := "none", cName := "NONE", value := 0 },
  { leanName := "value", cName := "VALUE", value := 1 },
  { leanName := "event", cName := "EVENT", value := 2 }
]

namespace MachineSchema

private def unique [BEq α] : List α → Bool
  | [] => true
  | value :: rest => !rest.contains value && unique rest

private def rowsValid (rows : List MachineEnumRow) : Except MachineSchemaError Unit :=
  if !unique (rows.map (·.leanName)) then .error .duplicateLeanName
  else if !unique (rows.map (·.cName)) then .error .duplicateCName
  else if !unique (rows.map (·.value)) then .error .duplicateValue
  else .ok ()

def validate : Except MachineSchemaError Unit :=
  if stateKindRows.isEmpty then .error .emptyStateKinds
  else if actionObservationRows.isEmpty then .error .emptyActionObservations
  else
    match rowsValid stateKindRows with
    | .error failure => .error failure
    | .ok () => rowsValid actionObservationRows

def WellFormed : Prop := validate = .ok ()

theorem wellFormed : WellFormed := by rfl

end MachineSchema

end CMetaCFlowCalculus.CFlow
