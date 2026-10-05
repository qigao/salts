# Minimal CMeta semantic lowering contract

Tracking Epic: #905

Branch: `feat/cmeta-semantic-lowering`

## Principle

CMeta remains C.

The project keeps native C control flow, native C functions, native C ABI and
ordinary status/error handling. The lowerer exists only to attach or enforce
semantic information that plain C cannot conveniently express.

```text
C
+ canonical type metadata
+ canonical lifecycle metadata
+ explicit ownership state
        |
        v
   cmeta-lower
        |
        v
 ordinary C11
```

## Public naming rule

New source-level CMeta extensions use the `cmeta_` prefix. The minimal public vocabulary is `cmeta_type(...)`, `cmeta_owned(T)`, `cmeta_move(x)`, `cmeta_value_ref`, and `cmeta_value_view`. No unprefixed compatibility aliases are kept on this branch.

## Authoring headers

Public C ABI remains ordinary `.h`, but a generated public header may have a
`.cmeta.h` authoring source. The authoring header is lowered to ordinary C and
may use the finite `cmeta_type Name = Kind(args...);` declaration tracked by
#915. Ordinary `.c` consumers never require the lowerer.

A `.cmeta.c` source may import a direct quoted `.cmeta.h`; semantic discovery
reads that finite header source and generated C rewrites the include to the
generated ordinary `.h`. This is not a general preprocessor/header parser.

## Core surface

The branch is limited to:

- `cmeta_type` — finite concrete type + Reflection/lifecycle declaration;
- `cmeta_owned(T)` — lexical ownership;
- `cmeta_move(x)` — explicit ownership transfer;
- deterministic automatic cleanup for LIVE owned values;
- compile/build-time use of TypeDesc/DataDesc/FunctionDesc and related canonical
  Reflection descriptors;
- generated ordinary C helpers where Reflection can remove boilerplate.

No other language surface is assumed.

## C stays in charge

Application code continues to use:

```c
if (...) { ... }
switch (...) { ... }
for (...) { ... }
while (...) { ... }
goto cleanup;
return status;

IntList_init(&list, 16);
IntList_add(&list, 10);
```

CMeta must not replace those constructs with another control-flow language.

## RAII rule

`cmeta_owned(T)` is accepted only when canonical lifecycle authority is known.
`cmeta_move(x)` is the only source-level ownership transfer marker in this scope.

The compiler decides **when** cleanup is required.
Canonical DataDesc/provider semantics decide **how** cleanup occurs.

No guessed destructor names, allocator policy, runtime lifecycle registry or
hidden fallback is permitted.

## Reflection rule

Reflection is descriptive semantic authority, not an object system and not a
hot-path dispatcher.

Use canonical metadata for:

- type identity and layout;
- value lifecycle/data semantics;
- function parameter/result ownership and effects;
- finite declared generic identity;
- code generation, validation and diagnostics.

Execution remains ordinary exact C functions.

## Explicit exclusions

This branch does not add:

- defer;
- try / ? / exception-like propagation;
- match/pattern syntax;
- for-in or new loop syntax;
- with/scope-success/failure syntax;
- interfaces, virtual dispatch, objects or OO syntax;
- receiver-method syntax expansion;
- C++ template spelling;
- overload/conversion systems;
- lambdas/closures;
- async/await;
- runtime generic dispatch or RTTI;
- a full C parser/compiler.

Existing receiver shorthand is not expanded and is not a dependency of the new
design.

## Merge policy

PR #906 stays Draft until the minimal `cmeta_type + cmeta_owned + cmeta_move + Reflection`
contract is complete, cross-platform qualified, documented, and produces
ordinary portable C11 without fallback paths.
