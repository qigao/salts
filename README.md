# CMeta + CFlow + Container typed data structures

v50 makes typed container instantiation **single-stage**. A container declaration is now the complete finite instantiation: type, traits, typed forwarding functions, descriptor, Range metadata, and the CFlow Stream bridge are all available immediately from the declaration.

There is no container `implement(...)`, no `DeclareContainers(...)`, no `ImplementContainers(...)`, and application users do not need to define a replay schema.

```text
                         compile time

                    C preprocessor kernel
                            |
                       CMeta DSL
        +-------------------+-------------------+
        |                   |                   |
   Struct / Enum          typed(...)       Containers(...)
        |                   |                   |
        |                   +----------+--------+
        |                              |
        |                    complete typed facade
        |                    traits / descriptor / Range
        |                              |
--------+------------------------------+---------------- runtime
                                       |
                              +--------+--------+
                              |                 |
                         Container algorithms    CFlow
                         ordinary C code     Stream/Graph
```

The macro layer is an implementation mechanism. The public programming model is the DSL plus normal C values/functions.

## 1. Define data models once

```c
Struct(User,
    (int, id),
    (double, score)
);

Enum(UserStatus,
    (USER_ACTIVE, "active"),
    (USER_DISABLED, "disabled")
);
```

`Struct(...)` and `Enum(...)` derive their metadata from the same declaration.

## 2. Instantiate typed value types

```c
typed(Pair, UserScore, User, double);
typed(Option, MaybeUser, User);
typed(Result, LoadUserResult, User, Error);
```

`Pair`, `Tuple`, `Option`, and `Result` are value types and are complete after this declaration.

## 3. Instantiate containers once

For one concrete container:

```c
typed(List, UserList, User);
```

For several concrete containers, use the direct batch form:

```c
Containers(
    (List,    UserList, User),
    (Vec,     UserVec,  User),
    (HashMap, UserMap,  int, User),
    (BTree,   UserTree, int, User, int_compare)
);
```

`Containers(...)` is only concise syntax for several complete `typed(...)` instantiations. It is **not** a declaration/implementation replay protocol.

Put these declarations in a normal guarded header. The same header can be included from multiple translation units. The generated forwarding layer is `static inline`; the heavy List/Hash/BTree algorithms remain compiled ordinary C in Container.

## 4. Use the concrete container as normal C

The generated concrete symbols remain available as the low-level typed ABI:

```c
UserList users;

UserList_init(&users);
UserList_push_back(&users, alice);
UserList_push_back(&users, bob);

User *first = UserList_at(&users, 0);
size_t count = UserList_size(&users);

UserList_destroy(&users);
```

No second instantiation step exists.

A higher-level kind facade such as `list_push(&users, value)` can be layered on top with C11 `_Generic`; that ergonomic facade is separate from the v50 instantiation model.

## 5. Define typed CFlow operations

```c
typed(filter, value, bool, active_user, (User u)) {
    return u.status == USER_ACTIVE;
}

typed(map, value, double, user_score, (User u)) {
    return u.score;
}
```

The same `typed(...)` spelling is routed by CMeta: registered uppercase generic kinds create finite generic values/containers; lowercase CFlow operator kinds use the CFlow callable bridge.

## 6. Stream directly from a typed container

```c
cflow_stream s;

stream(&users, &s)
    ->filter(&s, active_user)
    ->map(&s, user_score);
```

Every generated typed container begins with one CMeta container header pointing at its descriptor. Sequence/set containers expose a default Range; associative containers deliberately require an explicit view:

```c
stream_keys(&users_by_id, &s);
stream_values(&users_by_id, &s);
stream_entries(&users_by_id, &s);
```

The Range is borrowed and allocation-free. CFlow allocates only its source cursor state; the container must outlive the run.

## Single-stage container semantics

A declaration such as:

```c
typed(List, UserList, User);
```

immediately establishes:

```text
kind          = List
concrete type = UserList
element type  = User
traits        = container/range traits
descriptor    = UserList container metadata
Range         = UserList_range()
facade        = static-inline UserList_* forwarding functions
```

There is no meaningful second "implementation" phase for this metadata. The actual algorithms are already implemented by the raw Container library.

Header-local descriptor addresses are implementation details, not cross-TU type identity. Semantic type comparisons use descriptor contents/type identity rather than requiring identical static-object addresses in every translation unit.

## Schema / Replay is framework infrastructure

v49 introduced one generic tuple-row kernel:

```c
#define MySchema(M) Schema(M, (ROW_A, 1), (ROW_B, 2))
Replay(MySchema, mapper)
```

That mechanism remains useful for CMeta/CFlow/framework authors. Application users normally should not need it. In v50, `Containers(...)` is intentionally an application-facing complete-instantiation DSL rather than an alias for a named replay schema.

CFlow's operator universe can still be maintained as a named schema and replayed internally because operators genuinely have multiple generated views (enum, type rule, graph builder, stream method, runtime metadata).

## Container kinds

v50 covers:

```text
Vec        Deque      List
Stack      Queue      Heap
Set        HashSet
HashMap    Map        MultiMap
BTree      BPlusTree
```

Ordered `Heap`, `BTree`, and `BPlusTree` declarations take an explicit comparator.

## Dependency boundary

```text
       CMeta
      /     \
 Container    CFlow
```

- CMeta does not depend on Container or CFlow.
- Container raw algorithms do not depend on CMeta or CFlow; `container/typed.h` is the explicit CMeta integration layer.
- CFlow consumes CMeta metadata and Range protocols but does not own container algorithms.
- Container's allocation, growth, hashing, balancing, probing, and tree logic remain ordinary C algorithms.

## Public DSL vs implementation machinery

Application code should primarily see:

```text
Struct(...)
Enum(...)
typed(...)
Containers(...)
stream(...)
```

Framework authors may additionally use:

```text
Schema(...)
Replay(...)
Operators(...)
interface(...)
implements(...)
```

`implements(...)` belongs to CMeta's interface/protocol DSL. It is unrelated to the removed container `implement(...)` phase.

## Layout

```text
project/
├── cmeta/
│   ├── include/cmeta/
│   └── src/
├── cflow/
│   ├── include/cflow/
│   └── src/
├── container/
│   ├── include/
│   └── src/
├── tests/
├── examples/
└── Makefile
```

## Verification

The v50 gates cover the one-shot declaration model, multi-TU header inclusion/linking, descriptor/Range use across translation units, CFlow Stream integration, strict C11 compiler matrices, sanitizer runs, and existing randomized/property regressions.
