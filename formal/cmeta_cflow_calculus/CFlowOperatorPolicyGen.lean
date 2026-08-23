import CMetaCFlowCalculus.CMeta.BuiltinSignatures
import CMetaCFlowCalculus.CFlow.BuiltinOperatorPolicy
import CMetaCFlowCalculus.CFlow.OperatorPolicyHeader

open CMetaCFlowCalculus.CMeta
open CMetaCFlowCalculus.CFlow

private def usage : String :=
  "usage: cflow-operator-policy-gen (--stdout | --write <path> | --check <path>)"

private def renderBuiltin : Except OperatorPolicyError String :=
  OperatorPolicyHeader.render builtinSignatureManifest builtinOperatorPolicy

private def checkFile (path expected : String) : IO UInt32 := do
  try
    let actual ← IO.FS.readFile path
    if actual == expected then
      return 0
    IO.eprintln s!"generated CFlow operator policy is stale: {path}"
    return 1
  catch failure =>
    IO.eprintln s!"cannot read generated CFlow operator policy '{path}': {failure}"
    return 1

def main (args : List String) : IO UInt32 := do
  match renderBuiltin with
  | .error failure =>
      IO.eprintln s!"invalid built-in CFlow operator policy: {reprStr failure}"
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
