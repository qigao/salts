import CMetaCFlowCalculus.CMeta.BuiltinSignatures
import CMetaCFlowCalculus.CMeta.SignatureHeader

open CMetaCFlowCalculus.CMeta

private def usage : String :=
  "usage: cmeta-signature-gen (--stdout | --write <path> | --check <path>)"

private def renderBuiltin : Except ManifestError String :=
  SignatureHeader.render builtinSignatureManifest

private def checkFile (path expected : String) : IO UInt32 := do
  try
    let actual ← IO.FS.readFile path
    if actual == expected then
      return 0
    IO.eprintln s!"generated signature manifest is stale: {path}"
    return 1
  catch failure =>
    IO.eprintln s!"cannot read generated signature manifest '{path}': {failure}"
    return 1

def main (args : List String) : IO UInt32 := do
  match renderBuiltin with
  | .error failure =>
      IO.eprintln s!"invalid built-in signature manifest: {reprStr failure}"
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
