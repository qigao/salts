import CMetaCFlowCalculus.CFlow.MachineSchemaHeader

open CMetaCFlowCalculus.CFlow

private def usage : String :=
  "usage: cflow-machine-schema-gen (--stdout | --write <path> | --check <path>)"

private def checkFile (path expected : String) : IO UInt32 := do
  try
    let actual ← IO.FS.readFile path
    if actual == expected then
      return 0
    IO.eprintln s!"generated CFlow Machine schema is stale: {path}"
    return 1
  catch failure =>
    IO.eprintln s!"cannot read generated CFlow Machine schema '{path}': {failure}"
    return 1

def main (args : List String) : IO UInt32 := do
  match MachineSchemaHeader.render with
  | .error failure =>
      IO.eprintln s!"invalid CFlow Machine schema: {reprStr failure}"
      return 1
  | .ok header =>
      match args with
      | ["--stdout"] =>
          IO.print header
          return 0
      | ["--write", path] =>
          IO.FS.writeFile path header
          return 0
      | ["--check", path] =>
          checkFile path header
      | _ =>
          IO.eprintln usage
          return 2
