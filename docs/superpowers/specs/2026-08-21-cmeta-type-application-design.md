# CMeta Type Application Architecture

Status: architecture baseline  
Date: 2026-08-21  
Branch: `leanv4`  
Parent architecture: `2026-08-21-cmeta-hexagonal-architecture-design.md`

## 1. Purpose

This specification defines finite generic type application across the CMeta hexagonal architecture.

The semantic capability belongs to **CMeta Core**. The source spelling `M<A,B>` belongs to **CMeta Extend**.

The distinction is mandatory:

```text
CMeta Core
    GenericConstructor + TypeId + finite Apply semantics

CMeta Extend
    TypeExpr parser + `M<A,B>` syntax + discovery + lowering
```

Therefore `M<A,B>` is not a macro invocation and is not itself the semantic type identity. It is an Extend source expression that resolves to one Core type application.

## 2. Existing strict-C11 model

CMeta v50 already has explicit finite instantiation:

```c
typed(Result, LoadResult, User, Error);
typed(List, UserList, User);
typed(HashMap, UsersById, int, User);
```

This remains a valid Core/module-facing C11 spelling.

The current form combines:

```text
constructor
concrete type arguments
chosen C output name
```

and emits a concrete named C type immediately.

This is sufficient for strict C11 but does not make nested type expressions compositional.

## 3. Semantic rule

The Core semantic application is:

```text
Apply(Constructor, [TypeId...]) -> TypeId
```

Example:

```text
Task<Result<User,Error>>
```

resolves to:

```text
Apply(cmeta.Task,
  [Apply(cmeta.Result,
    [Atom(app.User), Atom(app.Error)])])
```

The resulting Core TypeId, not a typedef spelling, is authoritative.

## 4. Core vs Extend ownership

### 4.1 Core owns

Core or the owning generic module MUST own:

```text
GenericConstructor identity
constructor arity
semantic argument requirements
canonical TypeId structure
type equality
reflection
trait requirements
layout/generation schema for concrete applications
strict-C11 explicit instantiation entry points
```

### 4.2 Extend owns

Extend MUST own:

```text
TypeExpr source AST
`<...>` parsing
source aliases (`type X = ...`)
source occurrence discovery
application dependency collection
source-level deduplication plan
generated symbol naming/mangling
source rewriting/lowering
diagnostics/source maps
```

Core MUST NOT depend on TypeExpr AST nodes or mangled C symbol names.

## 5. Core TypeId model

Core requires structural semantic identity.

Conceptually:

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
} cmeta_type_identity;
```

Exact storage may differ. Semantics MUST preserve these relationships.

Type identity MUST NOT depend on:

```text
descriptor address
source alias
source whitespace
generated C symbol
translation-unit-local metadata address
```

## 6. GenericConstructor

A constructor is a finite registered semantic factory, not a programmable template language.

Conceptually:

```c
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
cmeta.Pair     Type × Type -> Type
cmeta.Option   Type -> Type
cmeta.Result   Type × Type -> Type
cmeta.Task     Type -> Type       (owned by Exec)
cmeta.Channel  Type -> Type       (owned by Exec/future channel module)
cmeta.List     Type -> Type
cmeta.Map      Type × Type -> Type
```

Constructor descriptors belong to the module that owns the semantic type.

Core provides the generic application mechanism.

## 7. v1 argument policy

v1 generic parameters are TYPE-only.

Valid Extend forms:

```c
Option<User>
Result<User, Error>
Task<Result<User, Error>>
Map<String, List<User>>
```

Not v1:

```c
Array<int, 16>
SmallVec<User, 8>
BTree<Key, Value, key_compare>
```

Comparator/hash/policy callbacks SHOULD be traits/configuration rather than type arguments unless they actually define layout/type identity in a future explicit design.

Examples:

```text
Map<K,V> requires Hash<K> + Eq<K>
BTree<K,V> requires Compare<K>
```

## 8. Extend TypeExpr

Extend v1 parses only a small type-expression language:

```text
TypeExpr :=
    Name
  | Apply(Name, TypeExpr...)
  | Pointer(TypeExpr)
  | Const(TypeExpr)
```

Examples:

```c
User
User *
const User *
Result<User, Error>
Task<Result<User *, Error>>
```

TypeExpr is a frontend object and MUST NOT become a Core public ABI.

## 9. Alias semantics

Extend may support:

```c
type UserResult = Result<User, Error>;
type AnotherResult = Result<User, Error>;
```

Both aliases resolve to the same Core TypeId.

```text
TypeId(UserResult) == TypeId(AnotherResult)
```

Aliases do not create nominally distinct types in v1.

If nominal newtypes are desired later, they require a separate explicit semantic construct.

## 10. Canonical application and C representation

Extend discovers each unique concrete application and requests/emits one canonical concrete C representation.

Example source:

```c
Task<Result<User, Error>> a;
Task<Result<User, Error>> b;
```

must not create two concrete structs.

Conceptual lowering:

```text
Result<User,Error>
    -> canonical application R
Task<R>
    -> canonical application T
```

Then generated C may contain:

```c
typed(Result, cmeta_Result_<id>, User, Error);
typed(Task, cmeta_Task_<id>, cmeta_Result_<id>);

cmeta_Task_<id> a;
cmeta_Task_<id> b;
```

The actual naming scheme is an Extend/codegen implementation detail.

## 11. Symbol mangling is not TypeId

Extend needs deterministic generated C identifiers, but symbol identity is separate from semantic identity.

Requirements:

- one semantic application -> one symbol within an output unit;
- symbol length bounded;
- deterministic for reproducible builds;
- collisions detected as hard generation errors;
- symbol algorithm/version may evolve without redefining Core type equality.

Core type equality MUST never compare mangled symbols.

## 12. Application discovery and dependency ordering

Use-site implicit instantiation is an Extend responsibility.

Example:

```c
Map<String, User> users;
Task<Map<String, User>> task;
Option<Map<String, User>> cache;
```

Extend discovers:

```text
Map<String,User>
Task<Map<String,User>>
Option<Map<String,User>>
```

and orders generation by semantic dependency.

```text
String   User
   \     /
  Map<String,User>
      /        \
 Task<...>   Option<...>
```

The graph is finite because source TypeExprs and registered constructor schemas are finite.

## 13. Strict-C11 compatibility

CMeta Core MUST remain usable without Extend.

A strict-C11 user may explicitly instantiate:

```c
typed(Result, UserResult, User, Error);
typed(Task, UserTask, UserResult);
```

The architectural target is that both strict and Extend paths resolve to the same semantic constructor/type relationships, even if migration preserves legacy nominal generated structs for existing `typed(...)` code initially.

The implementation MUST NOT silently claim old separately named C structs are ABI-identical aliases until the generic backend has been made canonical.

## 14. CType descriptor integration

Current CMeta descriptors are primarily flat metadata. Core generic applications require structured identity.

The migration SHOULD append/associate an identity object without requiring descriptor pointer uniqueness.

Conceptually:

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

Legacy descriptors may have `identity == NULL` during migration.

Generic application descriptors MUST eventually carry structural identity.

## 15. Type equality authority

`cmeta_type_equal` remains Core semantic authority.

Rules:

```text
if both descriptors have structural identities:
    compare identities structurally/semantically
otherwise:
    use conservative legacy fallback during migration
```

Equality MUST NOT rely on descriptor address or generated symbol address.

Constructor equality uses stable constructor identity, not descriptor pointer equality.

## 16. Reflection

Core generic reflection SHOULD provide:

```text
is_application(type)
constructor(type)
arity(type)
argument(type,index)
```

For:

```text
Map<String,User>
```

reflection yields:

```text
constructor = cmeta.Map
arity = 2
arg0 = String
arg1 = User
```

Display strings remain diagnostics, not semantic identity.

## 17. KnownTypes vs CallableSignatures

This split is mandatory and belongs to Core.

```text
KnownTypes
    all resolved/reflected types

CallableSignatures
    exact typed-callable signatures that are admitted/generated
```

A generic application becoming a KnownType MUST NOT automatically enlarge the full callable Cartesian product.

The current full-product signature generation is acceptable for a small fixed type universe, but it does not scale to compositional generic applications.

The future direction is exact finite signature demand.

Extend MAY discover exact signatures referenced by source, but Core owns their representation, validation and invocation semantics.

## 18. Traits and generic requirements

Requirements belong to semantic constructors/modules, not Extend syntax.

Example:

```text
Map<K,V>
    requires Hash<K>
    requires Eq<K>
```

Extend may diagnose a missing trait before emission, but the owning constructor/Core validation remains authoritative.

Trait implementation choice normally does not change type identity.

## 19. Module ownership examples

```text
Option<T> / Result<T,E>   Value module
List<T> / Map<K,V>        Container module
Task<T>                   Exec module
Channel<T>                Exec/channel module
```

Core does not know their layouts. It knows how to represent constructor identity, arguments, traits and CType semantics.

Extend likewise MUST NOT hard-code module layouts.

## 20. Lowering contract

Extend lowers syntax to module/Core declarations, not directly to hand-written layouts.

Example:

```c
Task<Result<User, Error>> task;
```

should conceptually lower through:

```text
resolve Result constructor
resolve User/Error
request Result<User,Error> concrete application
resolve Task constructor
request Task<Result<User,Error>> concrete application
emit explicit Core/module generation declarations
emit ordinary C variable declaration
```

This keeps generation semantics below the frontend.

## 21. Parser requirement

The exact `<...>` spelling requires Extend parsing/source transformation.

Core generic semantics do not require a parser.

Recommended frontend path:

```text
.cm
 -> re2c-generated lexer
 -> small recursive-descent TypeExpr parser
 -> semantic resolution
 -> lowering
 -> ordinary C11
```

`re2c` is a frontend build dependency, not a Core dependency.

## 22. Finiteness

v1 remains finite by construction:

- source contains finitely many TypeExpr occurrences;
- every TypeExpr is finite;
- constructors are from a finite registered set;
- constructor generation is finite schema application;
- no arbitrary user compile-time loops/recursion are introduced.

Target formal property:

```text
finite source demand
 -> finite resolved TypeId set
 -> finite application dependency graph
 -> finite generation
```

## 23. Formal split

Core proof responsibilities:

```text
TypeId equality properties
constructor arity/identity
finite application semantics
trait requirement soundness
KnownTypes/CallableSignatures separation
```

Extend proof/conformance responsibilities:

```text
`M<A,B>` parsing/lowering resolves to expected Core application
aliases preserve identity
repeated applications deduplicate
nested applications are emitted dependency-first
source lowering preserves resolved CType
```

## 24. Rejected designs

The following are explicitly rejected:

- treating `M<A,B>` as macro text substitution;
- making parser AST the Core type representation;
- using generated typedef names as TypeId;
- automatically adding every generic application to every callable signature family;
- putting comparator/hash function names into `<...>` in v1;
- allowing constructors to execute arbitrary user compile-time code;
- making Extend know concrete Result/Task/container layouts.

## 25. Acceptance criteria

The design is correct when:

1. Core generic semantics work without `cmc`;
2. Extend `M<A,B>` resolves to the same constructor/argument semantics;
3. aliases do not change structural identity;
4. nested applications are finite and deduplicated;
5. Core type equality does not depend on generated C symbol names;
6. module owners, not Extend, define layout/generation semantics;
7. known generic types do not cause uncontrolled callable-signature expansion.

This specification is subordinate to the CMeta Hexagonal Architecture and must preserve its dependency rules.
