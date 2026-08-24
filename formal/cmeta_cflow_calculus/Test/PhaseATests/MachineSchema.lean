import CMetaCFlowCalculus.CFlow.MachineSchema
import CMetaCFlowCalculus.CFlow.MachineSchemaHeader

open CMetaCFlowCalculus.CFlow

namespace CMetaCFlowCalculus.Tests.MachineSchema

def renderedSchemaHasRequiredFacts : Bool :=
  match MachineSchemaHeader.render with
  | .error _ => false
  | .ok header =>
      (header.splitOn "CFLOW_MACHINE_SCHEMA_VERSION 1u").length = 2 &&
        (header.splitOn "CFLOW_MACHINE_STATE_KIND_COUNT 3u").length = 2 &&
        (header.splitOn "CFLOW_MACHINE_ACTION_OBSERVATION_COUNT 3u").length = 2

example : machineSchemaVersion = 1 := by decide
example : stateKindRows.length = 3 := by decide
example : actionObservationRows.length = 3 := by decide
example : stateKindRows.map (fun row => row.kind) = MachineStateKind.all := by
  decide
example : actionObservationRows.map (fun row => row.kind) =
    MachineActionObservationKind.all := by decide
example : MachineSchema.validate = .ok () := by rfl
example : renderedSchemaHasRequiredFacts = true := by native_decide

end CMetaCFlowCalculus.Tests.MachineSchema
