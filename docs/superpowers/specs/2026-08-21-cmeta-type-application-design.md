# CMeta Type Application Architecture

Status: draft for user review  
Date: 2026-08-21  
Branch: `leanv4`

## 1. Purpose

This specification defines `M<A,B>` as a real finite generic type application in CMeta Syntax.

It is not a prettier spelling for `typed(M, Name, A, B)`. The design separates:

1. the generic constructor (`M`);
2. the type arguments (`A`, `B`);
3. the canonical semantic type identity of `M<A,B>`;
4. the generated C symbol used to represent that type;
5. optional user aliases.

The goal is to make types compositional:

```text
Task<Result<User, Error>>
Map<String, List<User>>
Option<const User *>
```

while preserving CMeta's finite, terminating, schema-driven metaprogramming model and ordinary C ABI.

## 2. Existing model and migration constraint

CMeta v50 currently uses explicit named instantiation:

```c
typed(Result, LoadResult, User, Error);
typed(List, UserList, User);
typed(HashMap, UsersById, int, User);
```

The current call simultaneously means:

```text
constructor = Result
arguments   = [User, Error]
C type name = LoadResult
```

The implementation routes a finite registered generic kind to `CMETA_TYPED_<Kind>` and expands a complete named C type/facade immediately.

This remains valid strict-C11 infrastructure. However, it is not a compositional type-expression system: nested type applications require manual intermediate names.

The new syntax layer therefore introduces a canonical type-expression model above this mechanism. Existing `typed(...)` remains available as a low-level explicit instantiation spelling during migration.

## 3. Core design rule

`M<A,B>` means:

```text
Apply(
    constructor = M,
    arguments   = [A, B]
)
```

It does not mean "expand macro M here".

The compiler/frontend first builds and canonicalizes a finite type-expression graph, then lowers each unique concrete application exactly once.

Example:

```c
Task<Result<User, Error>>
```

becomes:

```text
Apply(Task,
  [
    Apply(Result,
      [
        Atom(User),
        Atom(Error)
      ])
  ])
```

That tree, not a generated typedef spelling, is the semantic type identity.

## 4. TypeExpr

The v1 type-expression language is deliberately small.

```text
TypeExpr :=
    Atom(name)
  | Apply(constructor, TypeExpr...)
  | Pointer(TypeExpr)
  | Const(TypeExpr)
```

Surface examples:

```c
User
Result<User, Error>
Task<Result<User, Error>>
User *
const User *
Option<const User *>
```

v1 excludes:

- value/non-type generic arguments;
- arbitrary compile-time expressions;
- dependent types;
- generic function bodies/templates authored by users;
- lifetime/ownership types;
- arbitrary type-level recursion.

The generic argument list inside `<...>` contains `TypeExpr` only.

## 5. GenericConstructor

A generic constructor is a finite registered kind known to CMeta.

Conceptual semantic descriptor:

```c
typedef enum cmeta_generic_category {
    CMETA_GENERIC_VALUE,
    CMETA_GENERIC_CONTAINER,
    CMETA_GENERIC_HANDLE
} cmeta_generic_category;

typedef struct cmeta_generic_desc {
    const char *stable_id;
    const char *display_name;
    unsigned min_arity;
    unsigned max_arity;
    cmeta_generic_category category;
} cmeta_generic_desc;
```

Examples:

```text
cmeta.Pair     arity 2
cmeta.Tuple    arity 2..16
cmeta.Option   arity 1
cmeta.Result   arity 2
cmeta.Task     arity 1
cmeta.Channel  arity 1
cmeta.List     arity 1
cmeta.Map      arity 2
```

`stable_id` is semantic and namespace-qualified. `display_name` is the user-facing spelling.

Two unrelated libraries must not obtain the same constructor identity merely because both use the spelling `Map`.

## 6. Constructor parameters

v1 constructor parameters are TYPE-only.

Valid:

```c
Map<int, User>
Result<User, Error>
Task<User>
```

Not v1:

```c
SmallVec<int, 16>
BTree<int, User, int_compare>
Array<float, 4>
```

Comparator/hash/policy callables are not generic type arguments in v1.

For example:

```c
Map<String, User>
```

should obtain requirements such as:

```text
Hash<String>
Eq<String>
```

through traits/configuration rather than by making the functions part of the type identity.

This prevents CMeta from drifting toward unrestricted C++-style template arguments.

## 7. Canonical type identity

CMeta must distinguish type identity from C spelling.

Conceptual identity form:

```c
typedef enum cmeta_type_form {
    CMETA_TYPE_ATOM,
    CMETA_TYPE_POINTER,
    CMETA_TYPE_CONST,
    CMETA_TYPE_APPLY
} cmeta_type_form;

typedef struct cmeta_type_identity {
    cmeta_type_form form;
    const char *stable_atom_id;
    const cmeta_generic_desc *constructor;
    const struct cmeta_type_identity *base;
    const struct cmeta_type_identity *const *args;
    size_t arity;
    uint64_t stable_hash;
} cmeta_type_identity;
```

Interpretation:

```text
ATOM     stable_atom_id is used
POINTER  base is used
CONST    base is used
APPLY    constructor + args[] are used
```

The exact memory representation may change, but these semantic fields are required.

## 8. Alias is not identity

Surface syntax may provide aliases:

```c
type UserMap = Map<int, User>;
type AnotherUserMap = Map<int, User>;
```

Both aliases refer to the same canonical type application:

```text
Apply(Map, [int, User])
```

Therefore:

```text
TypeId(UserMap) == TypeId(AnotherUserMap)
```

An alias is a source-level name, not a distinct nominal type.

Generated C may contain:

```c
typedef cmeta_t_31a8ef... UserMap;
typedef cmeta_t_31a8ef... AnotherUserMap;
```

rather than generating two unrelated structs.

## 9. Existing `typed(...)` compatibility

Current handwritten strict-C11 declarations such as:

```c
typed(Result, LoadResult, User, Error);
```

remain supported during migration as explicit named instantiations.

However, existing macros currently generate a C type directly from the supplied `name`, so two different explicit names may produce two distinct C types even when their logical arguments match.

The syntax frontend must not rely on that legacy nominal behavior for canonical applications.

Frontend lowering therefore follows this model:

```text
Result<User,Error>
   ↓ canonicalize
internal canonical symbol = cmeta_t_<stable-id>
   ↓ instantiate once
existing/updated generic backend emits canonical concrete type
   ↓
source aliases become typedefs to that canonical type
```

This permits old code to continue using `typed(...)` while new compositional syntax has structural/canonical identity.

A later migration may redefine or supplement the strict-C11 API with a canonical-instantiation form, but that is outside this v1 syntax specification.

## 10. Canonicalization

Canonicalization is deterministic and syntax-independent.

Examples:

```text
Task < Result < User , Error > >
Task<Result<User,Error>>
```

produce the same canonical representation.

Canonical identity ignores:

- whitespace;
- aliases;
- generated C symbol names;
- descriptor addresses;
- translation-unit-local metadata addresses.

Canonical identity includes:

- atom stable ID;
- pointer/const structure;
- constructor stable ID;
- argument order;
- recursively canonicalized argument identities.

## 11. Stable hash and generated C symbol

Every canonical TypeExpr obtains a stable hash from its canonical semantic serialization.

Canonical serialization examples:

```text
atom:cmeta.int
atom:app.User
ptr(atom:app.User)
apply:cmeta.Result(atom:app.User,atom:app.Error)
apply:cmeta.Task(apply:cmeta.Result(atom:app.User,atom:app.Error))
```

Generated C symbols use a bounded readable prefix plus the stable hash, for example:

```text
cmeta_Result_7c912e4a...
cmeta_Task_423da813...
```

Requirements:

- same semantic TypeExpr -> same generated symbol across translation units;
- different aliases -> same generated symbol;
- symbol length is bounded;
- hash algorithm/version is fixed for one CMeta ABI generation;
- a detected collision within one build is a hard generation error, never silently merged.

The human-readable descriptor name remains the canonical source spelling such as `Task<Result<User,Error>>`, not only the hashed C symbol.

## 12. Instantiation graph

The frontend collects all referenced concrete applications before code generation.

Example source:

```c
Map<int, User> users;
Task<Map<int, User>> task;
Option<Map<int, User>> cached;
```

Unique application nodes:

```text
Map<int,User>
Task<Map<int,User>>
Option<Map<int,User>>
```

Dependency graph:

```text
int
User
  \ /
Map<int,User>
  ├── Task<Map<int,User>>
  └── Option<Map<int,User>>
```

Generation rules:

1. parse TypeExpr;
2. resolve atoms and constructors;
3. validate constructor arity;
4. canonicalize recursively;
5. intern/deduplicate TypeId;
6. collect required constructor/trait constraints;
7. build finite dependency graph;
8. emit concrete applications in dependency order;
9. emit user aliases after the canonical concrete type exists.

Repeated source occurrences never imply repeated instantiation.

## 13. Finiteness and termination

The source program contains a finite set of type-expression occurrences.

Every TypeExpr is a finite syntax tree.

Constructor application does not execute arbitrary compile-time user code. It applies one finite registered generation schema to already finite arguments.

Therefore the v1 generic type layer remains finite by construction.

CMeta does not add:

```text
while-at-type-level
recursive template execution
arbitrary comptime programs
self-generating new constructor programs
```

The implementation should make the following property explicit in the formal model:

```text
finite source TypeExpr set
-> finite canonical TypeId set
-> finite instantiation graph
-> terminating generation
```

## 14. CType descriptor integration

Current `cmeta_type_desc` is primarily flat metadata with optional pointer pointee information.

Generic applications require structural semantic identity.

The migration should append an optional identity pointer to the existing descriptor shape:

```c
typedef struct cmeta_type_desc {
    const char *name;
    size_t size;
    size_t align;
    cmeta_type_kind kind;
    const struct cmeta_type_desc *pointee;
    const cmeta_type_identity *identity;
} cmeta_type_desc;
```

Appending the field preserves existing five-field aggregate initializers because the trailing field is zero-initialized by C.

Rules:

- legacy descriptors may use `identity == NULL`;
- generated generic descriptors use structured identity;
- descriptor pointer equality is never type identity;
- cross-TU equality structurally compares identity when present;
- aliases do not create new identities;
- `name` is display/debug metadata, not the authoritative equality key.

## 15. Type equality

`cmeta_type_equal(a,b)` evolves to:

```text
if both have structural identity:
    compare canonical identity recursively
else:
    use the existing legacy semantic fallback
```

It must never require:

```text
a == b
identity_a == identity_b
constructor_descriptor_pointer_a == constructor_descriptor_pointer_b
```

because descriptors remain allowed to be TU-local.

Constructor equality uses stable constructor ID, not address.

## 16. Reflection

Generic type reflection must support:

```text
is_application(type)
constructor(type)
arity(type)
argument(type, index)
```

For:

```c
Map<String, User>
```

reflection reports:

```text
constructor = cmeta.Map
arity       = 2
arg[0]      = String
arg[1]      = User
```

This structured information is required for later:

- serde/schema generation;
- Task/Channel reflection;
- container capabilities;
- RPC/FFI schemas;
- diagnostics;
- formal conformance.

The string `"Map<String,User>"` alone is not sufficient semantic metadata.

## 17. Known type universe vs callable signature universe

This distinction is mandatory.

Current callable generation expands finite unary/binary/generator signature families from `CMETA_TYPE_LIST`, and the binary family can grow cubically with the number of registered types.

Therefore:

> Being a valid CMeta TypeExpr does not automatically make a type part of the callable signature universe.

Two sets exist conceptually:

```text
KnownTypes
    every concrete type known to CType/reflection/generic generation

CallableTypes / CallableSignatures
    only types/signatures explicitly required for typed callable invocation
```

For example:

```text
Map<String,User>
Task<Result<User,Error>>
```

may be perfectly valid reflected CMeta types without automatically participating in every `N^2/N^3` callable product.

The long-term callable system should become demand-driven by exact referenced signatures or an explicit finite callable schema, rather than automatically taking the full product of all known generic applications.

This callable refactor is a dependent subproject, not part of the first TypeExpr parser implementation.

## 18. Traits and constructor requirements

Generic constructors may declare finite semantic requirements on type arguments.

Examples:

```text
Map<K,V>
    requires Hash<K>
    requires Eq<K>

BTree<K,V>
    requires Compare<K>
```

Requirements affect whether an application is valid, but do not normally become part of type identity.

Therefore these are normally the same type:

```text
Map<String,User> using hash_impl_A
Map<String,User> using hash_impl_B
```

The algorithm/strategy may differ per object/configuration, but the element/key/value type identity is unchanged unless a future constructor explicitly declares a policy argument to be layout/identity-bearing.

v1 has no such value/policy generic parameters.

## 19. Containers

Current container kinds already behave semantically like unary or binary constructors:

```text
List<T>
Vec<T>
HashMap<K,V>
Map<K,V>
BTree<K,V>
```

The new type model removes the need for application code to invent facade names for every nested usage.

Example:

```c
List<User>
Map<String, List<User>>
```

The raw algorithm implementation remains ordinary C. The constructor schema generates the typed facade and metadata once for each canonical application.

Comparator/hash requirements move toward trait resolution rather than appearing as extra `<...>` arguments.

## 20. Value types

Existing value kinds map naturally:

```text
Pair<T,U>
Tuple<T...>
Option<T>
Result<T,E>
```

Examples:

```c
Option<User>
Result<User, Error>
Pair<String, User>
Tuple<int, double, User>
```

Their layouts remain ordinary generated C structs/unions.

The new semantic difference is that `Result<User,Error>` is one canonical application, not "whatever named struct a particular call to `typed(Result, Name, User, Error)` happened to generate".

## 21. Task and concurrency integration

`Task<T>` becomes a normal unary type constructor:

```text
Task : Type -> Type
```

Examples:

```c
Task<User>
Task<Result<User, Error>>
Task<Map<String, User>>
```

The public Task handle may have a layout independent of `T`, while its runtime implementation/result storage remains typed by the application metadata.

The TypeExpr system therefore provides the canonical identity used by CMeta Exec to represent task result types.

Similarly:

```text
Channel<T>
Option<Task<T>>
Result<Task<T>, Error>
```

become ordinary compositions rather than special concurrency syntax.

## 22. Struct/Enum integration

A reflected struct field may use any resolved TypeExpr:

```c
struct User {
    int id;
    Option<String> nickname;
    List<Role> roles;
}
```

The generated field descriptor should point to the exact canonical CType descriptor for each field TypeExpr.

This enables recursive schema traversal for serde/RPC/debugging.

C completeness/layout rules still apply. A by-value infinitely recursive C layout is invalid; pointer indirection may break layout recursion normally.

## 23. Parser boundary

`<` and `>` are parser-level type syntax, not lexer semantics.

The lexer returns punctuation/tokens. The parser decides whether `<` begins type arguments based on `TypeExpr` context.

Therefore:

```c
List<int>
```

in type context is application, while:

```c
if (a < b)
```

in expression context is comparison.

Nested closing brackets:

```c
Task<Result<User,Error>>
```

must parse as two type-closing `>` tokens in TypeExpr context, not require a special generic `>>` token.

This is compatible with the proposed re2c lexer + small parser frontend architecture.

## 24. Constructor registration and frontend discovery

The semantic constructor registry must have one finite source of truth for:

- stable ID;
- display name;
- arity;
- category;
- trait requirements;
- lowering/generator binding.

The current `CMETA_GENERIC_KIND_<Kind>` macro markers only answer whether a C-preprocessor route exists; they are insufficient as a frontend semantic registry.

Implementation should introduce a declarative finite constructor schema from which both can be derived:

```text
constructor schema
    ├── C preprocessor generic markers/routes
    ├── frontend constructor table/manifest
    └── formal snapshot
```

The exact file/macro representation is an implementation-plan decision, but duplicated hand-maintained constructor tables are not permitted.

## 25. Formal model

Lean should model the semantic layer independently of C symbol mangling.

Conceptual model:

```lean
inductive TypeExpr where
  | atom  : AtomId -> TypeExpr
  | ptr   : TypeExpr -> TypeExpr
  | const : TypeExpr -> TypeExpr
  | apply : ConstructorId -> List TypeExpr -> TypeExpr
```

Required properties:

```text
wellFormed
    constructor exists
    arity is valid
    all children are well formed

canonical
    aliases erased
    stable constructor/atom identity used

finite
    every TypeExpr has finite size
    finite source list produces finite unique application set

deterministic
    canonicalization is deterministic
    equality is decidable
```

Conformance should eventually connect:

```text
CMeta Syntax TypeExpr
        ↓
generated canonical C descriptor
        ↓
C runtime cmeta_type_equal
        ↓
Lean TypeExpr equality
```

## 26. v1 acceptance examples

These must resolve and canonicalize:

```c
Option<User>
Result<User, Error>
Task<Result<User, Error>>
Map<String, User>
Map<String, List<User>>
Option<const User *>
```

Repeated use:

```c
Map<String,User> a;
Map<String,User> b;
```

must instantiate one canonical application.

Aliases:

```c
type Users = Map<String,User>;
type Users2 = Map<String,User>;
```

must share identity.

Invalid arity:

```c
Option<User,Error>
```

must fail at CMeta semantic analysis.

Unknown constructor:

```c
Unknown<User>
```

must fail before generated C compilation.

Value parameter attempt:

```c
SmallVec<int,16>
```

must fail under v1 rules unless `SmallVec` is represented through a non-TypeExpr configuration mechanism.

## 27. Non-goals

This specification does not implement:

- parser/lexer code;
- arbitrary user-defined template bodies;
- non-type generic parameters;
- partial specialization;
- SFINAE;
- arbitrary compile-time evaluation;
- ownership/lifetime types;
- automatic inclusion of every generic application in the callable Cartesian product;
- a new C ABI;
- removal of existing `typed(...)` APIs.

## 28. Implementation decomposition

This architecture should be implemented as separate reviewable subprojects:

1. **Type identity core** — structured `cmeta_type_identity`, equality, reflection, compatibility with legacy descriptors.
2. **Generic constructor schema** — single finite constructor source of truth for existing Pair/Tuple/Option/Result and container kinds.
3. **Canonical instantiation model** — TypeExpr interning, stable naming/hash rules and one-instantiation-per-application codegen substrate.
4. **Syntax frontend TypeExpr parser** — `M<A,B>`, pointer/const, aliases; lower to canonical C generation.
5. **Callable-universe separation** — stop equating every known CType with every generated callable combination; move toward exact finite signature generation.
6. **Formal/conformance layer** — Lean TypeExpr model and C-generated snapshots.

The first implementation plan should cover only subproject 1 or subprojects 1+2 if their interface cannot be meaningfully tested independently.

## 29. Architectural conclusion

The public semantic rule is:

> `M<A,B>` is a finite generic type application whose identity is the constructor plus canonical argument TypeIds.

It is not a macro invocation and not a generated typedef name.

This turns CMeta from a system of manually named generic instantiations into a compositional finite type system while retaining:

- finite generation;
- explicit registered constructors;
- ordinary C layout and ABI;
- strict-C11 backend compatibility;
- existing low-level `typed(...)` migration path;
- formal decidability and termination.
