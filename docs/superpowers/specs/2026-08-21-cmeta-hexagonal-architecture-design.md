# CMeta Hexagonal Architecture

Status: architecture baseline  
Date: 2026-08-21  
Branch: `leanv4`

## 1. Purpose

CMeta is defined as a **semantic hexagonal kernel for C**, not as a replacement systems language and not as a macro collection.

The architecture separates four concerns:

1. **CMeta Core** — the stable semantic kernel;
2. **CMeta Extend** — the optional source-language frontend and lowering adapter;
3. **first-party modules** — State, Exec, Value/Containers and other domains built on Core;
4. **infrastructure adapters and consumers** — minicoro, OS pollers, CFlow and later libraries.

The defining rule is:

> Removing CMeta Extend must never remove a semantic capability. It may only remove a convenient source spelling.

Therefore every Extend feature MUST lower to an existing Core or module semantic object that can also be expressed from strict C11.

## 2. Architectural model

```text
                         source world
                             │
                    ┌────────┴────────┐
                    │                 │
               strict C11         CMeta Extend
                    │          parser / sema / lowering
                    │                 │
                    └────────┬────────┘
                             ▼
                    ┌─────────────────┐
                    │   CMeta Core    │
                    │ semantic kernel │
                    └─────────────────┘
                      ▲      ▲      ▲
                      │      │      │
                  State    Exec   Value/Containers
                      │      │
                      │      ├──────── Coroutine port
                      │      │              │
                      │      │          minicoro adapter
                      │      │
                      │      └──────── Platform ports
                      │                     │
                      │           epoll / IOCP / kqueue
                      │
                      └──────────── CFlow and other consumers
```

The hexagon is conceptual; it does not mean there are exactly six interfaces. It means semantic policy points inward and infrastructure dependencies point outward.

## 3. CMeta Core

CMeta Core MUST remain buildable and usable with an ordinary C11 compiler and MUST NOT depend on CMeta Extend, re2c, minicoro, CFlow, native event loops or OS-specific APIs.

Core owns only cross-domain semantic primitives:

```text
Schema / Replay
CType / TypeId
GenericConstructor / finite generic application semantics
Traits
Callable
Interface
Effect / Property / Contract
Reflection
finite generation relations
```

Core MAY provide strict-C11 DSL macros such as `Struct`, `Enum`, `typed`, `Schema` and `Replay`. These macros are Core spellings, not parser syntax.

Core MUST NOT own:

```text
source tokens
parser AST nodes
source spans
angle-bracket syntax
symbol mangling policy
machine syntax
async/await syntax
OS threads
pollers
coroutine stacks
CFlow graph semantics
```

## 4. CMeta Extend

CMeta Extend is an **Anti-Corruption Layer / Translator** between extended source syntax and the semantic model below it.

Extend may provide syntax such as:

```c
Result<User, Error>
Task<Result<User, Error>>
type UserResult = Result<User, Error>;
machine Connection { ... }
match (value) { ... }
async fn load(...) -> Task<Result<User, Error>> { ... }
```

Extend owns:

```text
lexer
parser
source AST
source diagnostics
source-map preservation
symbol naming/mangling
source-level aliases
semantic-demand discovery
lowering passes
compiler-driver integration
```

Extend MUST NOT be the semantic authority for type equality, traits, callable contracts, task state transitions or state-machine validity.

Extend MAY duplicate validation for better diagnostics, but authoritative validation MUST remain in Core or the relevant first-party module so strict-C11 users cannot bypass invariants.

## 5. First-party modules

State and Exec are not part of the minimal Core. They are first-party modules built on the kernel.

### 5.1 CMeta State

State owns:

```text
StateId / EventId
MachineDesc / TransitionDesc
finite transition semantics
guard/action admission rules
determinism
reachability/dead-state analysis
machine runtime dispatch
```

State uses Core `CType`, `Callable`, `Effect`, `Property` and finite graph support.

State MUST NOT know `machine`, `on`, `->` or any other Extend token.

### 5.2 CMeta Exec

Exec owns:

```text
Step
Resumable
Waitable
Waker
Task<T> runtime semantics
Executor
CancelToken
Scope
Coordination
Timer abstraction
```

Exec uses Core type/callable/contracts but MUST NOT depend on minicoro or a native poller implementation.

### 5.3 Value and Containers

`Pair`, `Tuple`, `Option`, `Result` and container generic kinds are first-party generic constructors implemented using Core generation facilities.

Their layouts and generated helpers belong to the owning module, not Extend.

## 6. Ports and adapters

Infrastructure MUST enter through narrow ports.

### 6.1 Coroutine bridge

Exec defines a backend-neutral coroutine port. A minicoro adapter implements that port.

```text
Task / Resumable
      │
      ▼
CoroutineBackend port
      │
      ├── minicoro adapter
      └── future stackless/fiber adapters
```

No public Core or Exec header may expose minicoro types.

### 6.2 Platform ports

Platform responsibilities are segregated:

```text
ThreadOps
ClockOps
PollerOps / CompletionOps
WakeupOps
```

A monolithic `PlatformOps` god-interface SHOULD NOT be introduced.

Native implementations may use:

```text
Linux       epoll + eventfd
Windows     IOCP
Darwin/BSD  kqueue
POSIX       poll fallback
```

Core and Extend MUST remain unaware of these choices.

## 7. Design-pattern mapping

The architecture intentionally uses patterns where they preserve boundaries:

| Area | Pattern | Constraint |
| --- | --- | --- |
| whole system | Hexagonal / Ports & Adapters | semantic dependencies point inward |
| Core semantics | Functional Core | deterministic/immutable where practical |
| Extend | Anti-Corruption Layer / Translator | syntax never becomes semantic authority |
| Type identity | Flyweight / Interning | one canonical identity per semantic type |
| generic kinds | Factory | finite registered generation, not user-programmable templates |
| `cmeta_callable` | Command | shared behavior object across State/CFlow/Exec |
| State | Table-Driven State Machine | no OO state-object hierarchy |
| Exec | Reactor/Proactor boundary | readiness/completion normalized behind ports |
| coroutine integration | Bridge + Adapter | backend replaceable without Task API changes |
| coordination | Composite + Policy | child composition without special-case APIs |
| external implementations | Strategy | explicit backend choice, no scattered feature macros |

Patterns are subordinate to semantic simplicity. A pattern MUST NOT be introduced solely for stylistic symmetry.

## 8. Functional Core / Imperative Shell

CMeta SHOULD maximize a deterministic semantic core and isolate effects at the shell.

Functional-core candidates:

```text
TypeId equality/canonicalization
trait resolution over finite registries
callable contract validation
state transition selection
reachability
CFlow validation/optimization/planning
coordination policy semantics
```

Imperative-shell responsibilities:

```text
source parsing
filesystem access
allocation
threads
I/O
executor wakeups
coroutine context switching
OS completion callbacks
```

Formal verification SHOULD primarily model the functional core and executable refinements at module boundaries.

## 9. Type-system boundary

Core owns **semantic type identity**. Extend owns **source type expressions**.

```text
Extend TypeExpr
    Apply("Task", Apply("Result", Name("User"), Name("Error")))
                    │
                    ▼ resolve/lower
Core TypeId
    apply:cmeta.Task(apply:cmeta.Result(atom:app.User,atom:app.Error))
```

Core MUST NOT depend on an AST representation.

Generated C symbols are an Extend/codegen concern and MUST NOT be used as the authoritative TypeId.

Aliases are source names and MUST NOT create new structural type identities unless a future explicit nominal-type construct says otherwise.

## 10. Known types vs callable signatures

Core MUST distinguish:

```text
KnownTypes
    all reflected/resolved CMeta types

CallableSignatures
    only exact signatures admitted for typed callable invocation
```

A type becoming known MUST NOT automatically expand the full callable Cartesian product.

This prevents generic applications such as `Task<Result<User,Error>>` and `Map<String,List<User>>` from causing cubic signature growth.

Extend MAY discover exact signature demand, but Core owns signature semantics and invocation behavior.

## 11. Semantic-authority rule

Each invariant has exactly one authoritative owner.

Examples:

```text
Type equality                  CMeta Core
Generic constructor arity      CMeta Core / owning generic module
Trait satisfaction             CMeta Core
Callable contract validity     CMeta Core
Machine determinism            CMeta State
Task lifecycle validity        CMeta Exec
Executor affinity              CMeta Exec
CFlow graph typing             CFlow
Source grammar                 CMeta Extend
```

Extend may report an error before lowering, but it MUST agree with the authority below it.

## 12. Dependency rules

Allowed dependencies:

```text
Extend -> Core
Extend -> State/Exec/CFlow lowering interfaces
State  -> Core
Exec   -> Core
CFlow  -> Core
CFlow  -> Exec where execution primitives are shared
minicoro adapter -> Exec
native adapters  -> Exec
```

Forbidden dependencies:

```text
Core -> Extend
Core -> State
Core -> Exec
Core -> CFlow
Core -> minicoro
Core -> OS poller
State -> Extend
Exec -> Extend
Exec -> minicoro concrete API
CFlow -> Extend
```

Dependency cycles are architecture violations.

## 13. Compilation and bootstrap model

Strict C11 path:

```text
app.c
  -> C compiler
```

Extended path:

```text
app.cm
  -> cmc
  -> generated ordinary C11
  -> GCC / Clang / MSVC
```

`re2c` may be used to build `cmc`'s lexer, but application builds MUST NOT require re2c when a built `cmc` is available.

Building CMeta Core MUST NOT require `cmc`; this prevents a bootstrap cycle.

## 14. CMeta Extend implementation shape

The recommended frontend is deliberately small:

```text
cmc/
  lex/
  parse/
  sema/
  lower/
  driver/
```

The lexer may be generated by re2c. Recursive-descent/Pratt parsing is preferred while the grammar remains small and island-oriented.

Extend SHOULD parse only what it must own. Ordinary C bodies SHOULD remain opaque/pass-through wherever possible.

## 15. State, Exec and CFlow positioning

These three domains share Core primitives but are not one universal IR.

```text
State IR    event-triggered logical state transition
Exec IR     runnable/waiting/completion execution state
CFlow IR    typed value-flow computation
```

They MUST NOT be collapsed merely because all three can be represented as finite graphs.

They may share generic graph algorithms, TypeId, Callable and policy machinery.

## 16. Formal verification architecture

Recommended formal partition:

```text
Formal/CMeta/Core
  Type
  Generic
  Traits
  Callable
  Calculus

Formal/CMeta/State
  Machine
  Transition
  Reachability

Formal/CMeta/Exec
  Task
  Waitable
  Coordination
  Executor

Formal/CMeta/Extend
  TypeLowering
  StateLowering
  ExecLowering
```

Core/module proofs establish semantic properties. Extend proofs establish lowering/refinement properties.

Examples:

```text
Extend `M<A,B>` lowering preserves Core TypeId
Extend machine syntax preserves State transition schema
Exec wake preserves legal Task state progression
CFlow compilation preserves typed endpoints
```

## 17. Anti-patterns explicitly rejected

The architecture rejects the following:

- **God Core** — putting State, Exec, parser and platform implementation in the kernel;
- **Leaky backend** — exposing minicoro, epoll or IOCP through Task/Core APIs;
- **Stringly typed reflection** — using display names as semantic identity;
- **Service Locator runtime** — repeated global string lookup instead of resolved descriptors;
- **semantic duplication** — separate Extend and Core type/trait/task rules;
- **universal graph IR** — forcing State, Exec and CFlow into one node model;
- **template-language drift** — generic constructors becoming arbitrary user compile-time programs;
- **backend feature macros in domain code** — platform choice must be isolated behind ports.

## 18. Architecture acceptance checklist

A proposed feature belongs in Core only if all are true:

1. it is cross-domain semantic infrastructure;
2. it can be expressed and used from strict C11;
3. it has no parser, OS, coroutine or domain-specific dependency;
4. removing Extend does not remove the feature;
5. it can have one stable semantic authority.

A proposed feature belongs in Extend if it primarily changes spelling, source diagnostics, source discovery or lowering.

A proposed feature belongs in a module if it introduces domain semantics such as state transitions, task scheduling or flow computation.

A proposed implementation belongs in an adapter if it connects a module port to a concrete external API/runtime.

## 19. Architectural summary

CMeta is organized around one stable idea:

> **A finite typed semantic kernel surrounded by replaceable adapters and optional modern syntax.**

The kernel stays small, deterministic and portable. First-party modules build richer systems semantics on top of it. CMeta Extend improves source ergonomics without owning runtime meaning. External runtimes and operating systems remain adapters.

This boundary is the architecture baseline for future Type Application, State, Exec, CFlow and frontend work.
