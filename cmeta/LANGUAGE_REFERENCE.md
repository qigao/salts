# CMeta Language Reference

CMeta is a pragmatic ordinary-C metaprogramming and Reflection layer built on
strict C11. It combines finite declaration macros, compile-time schema replay,
metadata descriptors, lifecycle helpers, and ordinary C runtime protocols.

CMeta is deliberately **not** a replacement for C++, Rust, or a separate source
language. Public application forms must remain valid ordinary C macros,
declarations, inline helpers, or functions.

This document is the authoritative language vocabulary. The public surface is
split into four categories so application syntax does not get mixed with
framework generation or runtime APIs.

---

## 1. Application DSL

These declarations are intended for normal application code.

### `cmeta_struct(...)` / `cmeta_field(...)`

Declares a C struct and reflection metadata in one statement.

```c
cmeta_struct(User,
    cmeta_field(int, id)
    cmeta_field(double, score)
    cmeta_field(const char *, name)
);
```

Fields use `cmeta_field(type, name)` without separating commas. The same
declaration generates the ordinary C layout and static Reflection metadata.
Tuple rows remain internal Schema/Replay input, not canonical application syntax.
The declaration provides the concrete C type plus field metadata helpers such
as:

```c
StructMeta(User);
FieldCount(User);
FieldMeta(User, 0);
FieldFind(User, "score");
```

Use `cmeta_struct(...)` when a type benefits from structural metadata. Ordinary C
`struct` remains valid and should be preferred when metadata is unnecessary.

### `cmeta_enum(...)`

Declares an enum and immutable reflection metadata.

Auto-valued rows:

```c
cmeta_enum(State,
    (READY,   "ready"),
    (RUNNING, "running"),
    (DONE,    "done")
);
```

Explicit stable values:

```c
cmeta_enum(HttpStatus,
    (HTTP_OK,        200, "ok"),
    (HTTP_NOT_FOUND, 404, "not_found")
);
```

Rows are either `(symbol, text)` or `(symbol, value, text)`. There is no
`Item(...)` wrapper.

Generated helpers include typed string/symbol conversion and parsing:

```c
State_to_string(value);
State_to_symbol(value);
State_from_string(text, &out);

EnumMeta(State);
EnumString(State, value);
EnumSymbol(State, value);
EnumParse(State, text, &out);
```

### `cmeta_traits(...)`

Declares callable type capabilities from tagged rows.

```c
cmeta_traits(User,
    (equal, user_equal),
    (hash, user_hash),
    (compare, user_compare),
    (copy, user_copy),
    (move, user_move),
    (destroy, user_destroy)
);
```

Supported callable tags are:

```text
equal
hash
compare
copy
move
destroy
```

CMeta derives both the capability flags and the matching function slots from the
same rows. Duplicate, unknown, or malformed rows are compile-time errors.

`TRIVIAL_COPY` and `TRIVIAL_DESTROY` are descriptor properties, not inferred
callable traits.

### Compile-time capabilities, tagged variants and flags

`cmeta_trait(Capability, callback)` names the existing tagged trait row.
`cmeta_has_trait(Name, Capability)` and `cmeta_require_trait(Name, Capability)`
query/check the declaration at compile time; `cmeta_require_field(Owner, member,
Type)` checks exact native field type without evaluating an object.

`cmeta_variant(Name, "stable.Name", cases...)` takes consecutive
`cmeta_case(Case, nonzero_int_tag, PayloadType, &payload_data)` rows and generates
checked typed operations over one inline tag/union and canonical DataDesc.
`cmeta_match(pointer)` is ordinary switch with `case Name_Case:` labels.
`cmeta_flags(Name, "stable.Name", rows...)` takes consecutive
`cmeta_flag(Symbol, uint64_bits, "text")` rows and derives distinct typed values
and one canonical unsigned64 enum domain. Both finite declarations accept 1–16
rows; traits still use comma-separated rows.

See [capability contracts and executable examples](CAPABILITIES.md) for callback
signatures, provider admission, ownership, failure cleanup and compatibility.

### Structured scope

`cmeta_scope(name, status, autos, body)` owns 1 through 16 explicit
`cmeta_auto(Type, local_name)` rows inside `cmeta_autos(...)`. The wrapper
keeps the leading-comma row stream in one preprocessor argument and has no
runtime representation. Each type exposes `Type_cmeta_data()` with
canonical concrete `construct_ops`. The body is one ISO C expression returning
`cmeta_status`, usually a typed function call borrowing the local values:

```c
#include <cstl/typed.h>

cmeta_type(List, ScopeList, int);

static cmeta_status fill_values(ScopeList *values) {
    if (ScopeList_init(values, 2u) != STL_OK) return CMETA_CALLBACK_ERROR;
    if (ScopeList_add(values, 11) != STL_OK) return CMETA_CALLBACK_ERROR;
    return CMETA_OK;
}

int main(void) {
    cmeta_status status;
    cmeta_scope(request, status,
        cmeta_autos(cmeta_auto(ScopeList, values)),
        cmeta_body(fill_values(&values)));
    return status == CMETA_OK ? 0 : 1;
}
```

Link this container example with `Salts::CSTL`. Body functions can take typed
context parameters as well as resource pointers; no closure or erased callback
registry is needed. Resources belong to the generated scope, and borrowing ends
when the body returns. They must not escape through a saved pointer or suspended
operation. Move ownership into caller-owned storage explicitly with `cmeta_move`
when a value must outlive the scope; moved-from locals still receive cleanup.

The ordinary-C reference expansion has this control flow:

```text
declare zero storage and live flags
bind each resource's canonical concrete construct ops
initialize resources in declaration order
call body and store its returned status
cleanup:
    restore live resources to semantic zero in reverse order
continue with status
```

An initialization error restores the failed partial value once, skips the body,
then cleans earlier initialized resources. A body error follows the same cleanup
path. The returned body status replaces `status`; cleanup does not overwrite it.
The same cached canonical ops supply initialization and destruction. The binding
checks descriptor/ops ABI, callback presence and native size/alignment without
field-name lookup, recursive Reflection validation or another lifecycle table.
Descriptors lacking concrete construct ops return `CMETA_TRAIT_MISSING` before
construction; malformed ABI/callbacks return `CMETA_INVALID_ARGUMENT`, and an
incompatible native layout returns `CMETA_TYPE_MISMATCH`.

Scope setup/cleanup is O(N) time and O(N) bounded automatic storage for N resource
rows, excluding provider-owned payloads. No heap allocation, source lowerer,
cleanup attribute, SEH or assembly backend is required. DataDesc layout and the
generic checked runtime lifecycle APIs retain their existing contracts.

**Migration and design decision (#920/#929):** the earlier statement-block
`cmeta_body(...)` could let native exits bypass cleanup. Pure C macros cannot
intercept arbitrary `return/goto`, and compiler cleanup attributes do not provide
an equivalent MSVC implementation. Move the block into a typed status-returning
function and replace `cmeta_leave(...)` or the earlier `cmeta_scope_exit(...)`
with `return status` in that function. Both old exit macros now fail compilation.
Function boundaries prevent jumps to an outer scope's cleanup label. Nested body functions return the inner scope's
status only after inner cleanup, so outer cleanup follows automatically. Only
code using the earlier scope body/exit syntax needs migration; field layout and
container APIs are unaffected. Providers formerly using generic-only lifecycle
dispatch must expose canonical concrete construct ops before joining this
static scope backend; there is no automatic runtime dispatch fallback.

The tradeoff is an explicit function and explicit borrowed parameters in place
of implicit captures. This is an intentional source-compatibility change to make
ordinary-C exits safe on every supported compiler. Qualification covers native
body returns, goto/break inside body functions, partial initialization, moves,
nested error propagation and rejection of escaping block syntax. A rollback
requires reverting the scope header and its migrated callers together; restoring
only the former block expander would restore the resource-leak paths.

### `cmeta_type(...)`

The ordinary-C finite generic type declaration entry point.

```c
#include <cmeta/meta.h>

typedef int Key;
typedef int Value;
typedef struct User {
    int id;
} User;

cmeta_type(Option, MaybeUser, User);
cmeta_type(Pair, Entry, Key, Value);
cmeta_type(Tuple, Point3, double, double, double);
```

General form:

```text
cmeta_type(kind, generated_name, type_arguments...)
```

CMeta implements **finite generic routing**, not unrestricted templates.
`cmeta_type(...)` accepts only registered generic kinds and fails compilation
for an unregistered kind.

Algorithmic container kinds such as `List`, `Vec`, and `HashMap` belong to
CSTL:

```c
#include <cstl/typed.h>

cmeta_type(List, IntList, int);
cmeta_type(Vec, IntVec, int);
cmeta_type(HashMap, IntValuesById, int, int);
```

The declared type exposes ordinary concrete C operations:

```c
IntList values = {0};

IntList_init(&values, 100u);
IntList_push_back(&values, 7);
IntList_destroy(&values);
```

One declaration may generate the wrapper type, static-inline typed forwarding
functions, canonical metadata, Range factories, and relevant traits. Allocation
and container algorithms remain ordinary compiled C in the provider library.

Callable/function DSLs use separate explicit entry points such as
`cmeta_function(...)`; `cmeta_type(...)` never routes into callable syntax.

### `typed_any(...)`

Declares a typed callable together with semantic contract metadata.

```c
typed_any(value, int, increment, (int value)) {
    return value + 1;
}

typed_any(associative, long, add, (long left, long right)) {
    return left + right;
}
```

Form:

```text
typed_any(contract, return_type, name, parameters)
```

Current contract vocabulary:

```text
unknown
value
pure
idempotent
associative
fallible
io
async
stateful
```

Contracts map to the existing CMeta effect/property bitsets. They express
programmer intent and optimization/runtime constraints; they do not introduce a
new execution model.

### `FunctionDecl(...)`

Declares an ordinary C function prototype together with immutable descriptive
metadata. It does not replace `typed_any(...)` and does not create an erased
runtime invocation mechanism.

声明还生成 `name_function_type` 精确函数指针 typedef；static call 复用同一组
参数行，不另写原生签名。既有 descriptor 布局及相等语义不变。

```c
FunctionDecl(io, int, send_packet,
    (int, fd, CMETA_PARAM_IN),
    (size_t *, written, CMETA_PARAM_OUT, &cmeta_type_size_ptr));
```

General parameter rows are:

```text
(type, name, flags)
(type, name, flags, explicit_descriptor)
(type, name, flags, explicit_descriptor, abi_carrier)
```

The three-field form requires a registered scalar type, resolves `type` with
`CMETA_TYPEOF(type)`, and records a scalar ABI carrier. Non-scalar and unregistered
types are rejected at declaration time; use the five-field form for pointer,
aggregate, or callback parameters. Likewise, inferred returns require a registered
scalar or literal `void`; a typedef of `void` requires an explicit return descriptor
and carrier. Use `FunctionDeclAsAbi` / `Function0DeclAsAbi` for other return types.
The four-field form remains the compatibility form for a
provider-owned semantic descriptor and leaves ABI carrier unspecified. Consumers
that generate exact C call boundaries (TinyMock, FFI, plugin bridges) should use
the five-field form to state an explicit ABI carrier such as
`CMETA_ABI_OBJECT_POINTER`, `CMETA_ABI_AGGREGATE`,
`CMETA_ABI_FUNCTION_POINTER`, or `CMETA_ABI_ENUM`.

For an application-defined return type, `FunctionDeclAs` preserves an
unspecified ABI carrier. Use `FunctionDeclAsAbi` when the return carrier is
part of the execution contract. `FunctionAbi(name)` returns the immutable
`cmeta_function_abi_desc` sidecar paired with `FunctionMeta(name)`.

Use `FunctionDeclAs(...)` with its explicit return descriptor.
`Function0Decl(...)` and `Function0DeclAs(...)` are the zero-parameter
forms.

Direction flags are:

```text
CMETA_PARAM_UNKNOWN
CMETA_PARAM_IN
CMETA_PARAM_OUT
CMETA_PARAM_INOUT
```

`CMETA_PARAM_UNKNOWN` is the zero value and is a valid explicit statement
that direction is not authoritative. Consumers may use
`cmeta_param_direction_known()` before applying IN/OUT-specific behavior and
must not infer direction from pointer spelling or parameter names.

Pointer parameters may additionally declare `CMETA_PARAM_NULLABLE`,
`CMETA_PARAM_BORROWED`, or `CMETA_PARAM_OWNED`. Borrowed and owned are
mutually exclusive. These pointer semantics are independent of direction:
nullable/borrowed/owned may be known while direction remains UNKNOWN.
OUT/nullability/ownership metadata still requires a pointer descriptor where
the respective semantic demands it.

CMeta publishes canonical pointer-boundary descriptors for common reflected
contracts: `cmeta_type_void_ptr`, `cmeta_type_char_ptr`, `cmeta_type_char_ptr_ptr`, and
`cmeta_type_descriptor_ptr` (plus their pointee descriptors where applicable).
They provide type identity and exact pointer shape only. A source declaration
such as `const char *` may use `cmeta_type_char_ptr`; `const` does not imply
BORROWED, and ownership/lifetime still comes exclusively from explicit
parameter/result flags.

Function results have an independent semantic flag set:

```text
CMETA_RESULT_UNKNOWN
CMETA_RESULT_VALUE
CMETA_RESULT_BORROWED
CMETA_RESULT_SHARED
CMETA_RESULT_OWNED
CMETA_RESULT_NULLABLE
```

VALUE/BORROWED/SHARED/OWNED are mutually exclusive. NULLABLE is orthogonal and
is valid only for pointer-shaped return types. A void return admits only
UNKNOWN. Existing `FunctionDecl*` forms deliberately publish UNKNOWN rather
than inferring ownership from return spelling, type kind, names, or ABI carrier.

Use the explicit result-aware forms when the result contract is authoritative:

```c
Function0DeclResult(value, int, CMETA_RESULT_VALUE, current_count);

Function0DeclAsAbiResult(
    value, Buffer *, &buffer_ptr_type, CMETA_ABI_OBJECT_POINTER,
    CMETA_RESULT_OWNED | CMETA_RESULT_NULLABLE, make_buffer);
```

Result semantics remain descriptive. They tell compilers/planners whether a
returned resource is a value, borrow, shared reference, or ownership transfer;
they do not themselves execute retain/release/destroy or insert lexical cleanup.

The generated `FunctionMeta(name)` view is TU-local immutable metadata.
Consumers compare the referenced CMeta types semantically rather than relying
on descriptor address identity across translation units.

Function reflection answers "what is this C function?". Execution remains a
separate concern: an exact-ABI generated adapter, `cmeta_callable`, CFlow, or
another consumer-specific mechanism must perform the actual call. CMeta does not
parse arbitrary C prototypes at runtime and does not provide libffi-style
universal invocation.

### Reflection across native modules

Function, parameter, interface and type descriptors are borrowed views. Their
names, arrays, nested descriptors, traits, callbacks and generated wrappers remain
owned by the provider. Copying a descriptor, interface, `cmeta_callable`, or CFlow
projection does not retain the module containing its data or executable code.
The host must hold a module reference until all consumers finish, including copied
Graph/Plan callables, active runs and values requiring provider trait callbacks.
Unload order is: stop new admissions, drain calls/runs, destroy dependent values
and consumers while their callbacks are live, then release the final module
reference. Validators require live storage and cannot detect an unloaded pointer.

`CMETA_REFLECTION_ABI_VERSION` is the reflection layout epoch. Epoch 2 introduced result-semantic FunctionDesc layout; epoch 3 replaces receiver-method owner strings with optional canonical `cmeta_generic_desc` identity. Ordinary receiver methods use a null generic owner, while generic operation sets publish their constructor descriptor. A provider bootstrap
must accept a fixed-width requested epoch and reject a mismatch **before publishing
descriptor pointers**. The provider compares against its own header constant;
`cmeta_reflection_abi_version()` returns the linked CMeta library's epoch and checks
header/library agreement, but a host-resolved query cannot identify a plugin's
compile-time ABI. Providers predating this handshake require an explicit legacy
adapter or rejection; a matching `sizeof` is not proof of compatibility.

Within an epoch, existing descriptor layouts, field meanings, enum values and
array element strides are frozen, including reachable type/identity/trait layouts.
Add metadata through separate sidecars; incompatible changes require a new epoch.
The `size` fields are validation guards, not permission to append fields to array
elements or to reinterpret another epoch. Host and provider must also agree on
native architecture, calling convention, packing and enum representation.

This epoch covers reflection only. Salts Plugin exports embed borrowed pointers
to these descriptors, so the Plugin ABI is also advanced when an incompatible
Reflection layout becomes part of the exported manifest contract; hosts must
reject the older Plugin ABI before consuming reflected exports.

Application interface/vtable versions and the
finite callable type/signature configuration require separate agreement before
dispatch; equal reflection epochs do not authorize exchanging arbitrary
`cmeta_callable` builds. CMeta owns neither the platform loader nor its references.

Dynamic module publication, loading, lease and unload verification belong to the
Salts Plugin module and its tests.

### `interface(...)`

Declares a small protocol/vtable interface.

```c
#include <cmeta/meta.h>

#define SOURCE_METHODS(X, I) \
    X(I, R1, bool, next, int *, out) \
    X(I, V0, void, close, _)

interface(Source, SOURCE_METHODS);
```

Method row kinds are split into two levels:

```text
R0..R4   non-void ABI-only rows
V0..V4   void ABI-only rows
D0       owning destructor ABI-only row

F0..F4   fully reflected non-void rows; result semantics remain UNKNOWN
FR0..FR4 fully reflected non-void rows with explicit canonical result semantics
FV0..FV4 fully reflected void rows
FD0      fully reflected owning destructor
```

Legacy `R/V/D` rows preserve the exact vtable/wrapper ABI but intentionally do
not synthesize semantic function metadata from C spelling. Their
`cmeta_interface_method_desc.function` and `.abi` are `NULL`; use
`cmeta_interface_method_arity()` for dispatch arity.

Fully reflected `F/FV/FD` rows additionally state the semantic contract,
return descriptor, return ABI carrier, and each parameter as the exact
five-field FunctionDecl row. `F` rows intentionally preserve
`CMETA_RESULT_UNKNOWN`. Result-aware `FR0..FR4` rows add one explicit
`result_flags` field after the return ABI carrier and otherwise preserve the
same vtable/wrapper ABI:

```text
(type, name, flags, descriptor, abi_carrier)
```

For all reflected rows, `cmeta_interface_method_function()` and
`cmeta_interface_method_abi()` expose the same canonical
`cmeta_function_desc` / `cmeta_function_abi_desc` model used by ordinary
`FunctionDecl`. CMeta never infers OUT/INOUT, ownership, effects, or custom
type identity from pointer spelling or names.

Example:

```c
#define CLOCK_METHODS(X, I) \
    X(I, F0, cflow_instant, now, stateful,
      &cflow_type_instant, CMETA_ABI_AGGREGATE) \
    X(I, F1, bool, advance, stateful,
      &cmeta_type_bool, CMETA_ABI_SCALAR,
      (cflow_duration, delta, CMETA_PARAM_IN,
       &cflow_type_duration, CMETA_ABI_AGGREGATE)) \
    X(I, FR0, cflow_buffer *, create_buffer, stateful,
      &cflow_buffer_ptr_type, CMETA_ABI_OBJECT_POINTER,
      CMETA_RESULT_OWNED | CMETA_RESULT_NULLABLE)
```

An interface value is conceptually `{ self, vtable }` plus implementation and
capability metadata. This is a protocol mechanism, not a class hierarchy.

### `implements(...)`

Binds an ordinary C implementation to an interface.

```c
implements(Source,
           file_source,
           SOURCE_CAN_SEEK,
           .next = file_source_next,
           .close = file_source_close);
```

`implements(...)` only means "implements this interface/protocol". It is not a
container generation phase.

The natural names are conditional aliases. If
`CMETA_NO_NATURAL_INTERFACE_NAMES` is defined, or a host header has already
claimed `interface` or `implements`, use the collision-safe spellings:

```c
CMETA_INTERFACE(Source, SOURCE_METHODS);

CMETA_IMPLEMENTS(Source,
                 file_source,
                 SOURCE_CAN_SEEK,
                 .next = file_source_next,
                 .close = file_source_close);
```

Framework and public headers should prefer `CMETA_INTERFACE(...)` and
`CMETA_IMPLEMENTS(...)`; application-local code may use the natural aliases when
the host environment leaves them available.

---

### Static key / typed static call

`<cmeta/fastpath.h>` 的 C11 `cmeta_static_key(name, initial)` 定义原子 bool，
`cmeta_static_branch(&name)` acquire 读取；enable/disable 在控制面 release 发布。
`cmeta_static_call(slot, default_function)` 从 FunctionDecl 生成精确类型原子槽，
`cmeta_static_update(slot, function)` 检查签名与完整 ABI 契约后替换。
`cmeta_static_invoke(slot, args...)` 直接调用该次读取的目标，零参数用
`cmeta_static_invoke0(slot)`。
call 声明要求文件作用域、每槽一个 TU；热路径无 Reflection 查询。

默认使用可移植 C 原子；`CMETA_NATIVE_FASTPATH=ON` 才暴露显式 native 读取/调用。
C++17 借用 C-owned opaque key。更新失败不改变目标；更新不 retain 或 drain 旧
提供者，所有旧代码/metadata 必须活到在途调用结束。完整契约、边界、benchmark
和可编译测试示例见 [FASTPATH.md](FASTPATH.md)。

## 2. Framework DSL

These forms are primarily for libraries and CMeta/CFlow internals.

### `Schema(...)`

Defines or expands parenthesized row data through a mapper.

```c
#define MyRows(M) \
    Schema(M,
        (ROW_A, 1),
        (ROW_B, 2))
```

`Schema(...)` owns row unpacking. It is a finite C-preprocessor code-generation
kernel, not a runtime data structure. The tuple-list kernel accepts at most 16
rows per invocation. `cmeta_struct(...)`, `cmeta_enum(...)`, `cmeta_traits(...)`, `Schema(...)`,
and `Operators(...)` share that row-count limit; `Tuple` accepts 2 through 16
type arguments. Split larger declarations into separate stable concepts instead
of depending on an implementation-specific macro expansion failure.

### `Replay(...)`

Applies a named schema/producer to a consumer mapper.

```c
#define DECLARE(name, value) int name = value;
Replay(MyRows, DECLARE)
```

Conceptually:

```text
Schema = source rows / compile-time representation
Replay = consumer application
```

One schema can therefore feed declaration, metadata, validation, counting, or
other framework mappers without creating another user-facing language layer.

### Finite compile-time functions

Finite functions declare explicit mappings and evaluate them through generated
C declarations. They do not add C++ template syntax or a runtime evaluator.

```c
TypeFunction(StorageType,
    (small, SmallStorage),
    (wide, WideStorage));

TypeFunction(CommonType,
    (small, small, SmallStorage),
    (small, wide, WideStorage),
    (wide, small, WideStorage),
    (wide, wide, WideStorage));

typedef TypeEval(StorageType, small) storage_type;
typedef TypeEval(CommonType, small, wide) common_type;
```

`TypeFunction` infers the input arity from its first row, while `TypeEval`
infers it from the number of input keys. The corresponding integer-constant
forms are `ValueFunction` and `ValueEval`:

```c
ValueFunction(TypeRank,
    (small, 1),
    (wide, 2));

enum { wide_rank = ValueEval(TypeRank, wide) };
```

`Predicate` is a unary boolean value function. `Satisfies` evaluates it and
`Require` rejects a false row with a C/C++ static assertion:

```c
Predicate(Hashable, (small, 1), (opaque, 0));
Require(Hashable, small);
```

The declaration contract is deliberately finite and fail-fast:

- A function name and every input key must each be one stable preprocessor
  identifier.
- A declaration contains 1 through 16 rows. More rows can be added by repeating
  the same function name in separate bounded declarations.
- The first row determines input arity; every later row in that declaration
  must have the same shape or compilation fails.
- A value result is an integer constant expression. Complex type results should
  first receive a stable typedef name.
- Evaluating an absent mapping produces an unknown generated identifier, while
  declaring conflicting rows produces a C declaration conflict. Neither case
  has a fallback.

Arity is part of each generated identifier, so unary, binary, and ternary
functions with the same public name remain distinct. Declaration work is linear
in the number of rows; an evaluation is fixed token lookup rather than a scan
or a generated binary/ternary Cartesian product. Only the unified entry points
are public; numbered implementation families remain internal.

### Finite DFA inference

`InferenceRules` projects explicit integer-symbol rows into an ordinary C
relation and infers arity from the first row. A row contains one through three
input symbols followed by one result.
Each declaration accepts the same bounded 1–16 row list as the underlying
`CMETA_PP_FOR_EACH_A` projection.
The row source may be shared with a matching `ValueFunction` so compile-time and
admission-time evaluation cannot drift:

```c
#include <cmeta/meta.h>

enum {
    OP_ADD = 1,
    TYPE_SMALL = 10,
    TYPE_WIDE = 11
};

#define CommonRows \
    (TYPE_SMALL, TYPE_SMALL, TYPE_SMALL), \
    (TYPE_SMALL, TYPE_WIDE, TYPE_WIDE), \
    (TYPE_WIDE, TYPE_SMALL, TYPE_WIDE)

ValueFunction(CommonType, CommonRows);
InferenceRules(common_type_relation, CommonRows);

int main(void) {
    cmeta_infer_state states[
        CMETA_INFER_STATE_BOUND(
            InferenceRuleCount(common_type_relation),
            InferenceRuleArity(common_type_relation))];
    cmeta_infer_transition transitions[
        CMETA_INFER_TRANSITION_BOUND(
            InferenceRuleCount(common_type_relation),
            InferenceRuleArity(common_type_relation))];
    cmeta_infer_dfa dfa;
    cmeta_infer_value result = 0u;
    const cmeta_infer_symbol input[] = {TYPE_SMALL, TYPE_WIDE};

    cmeta_infer_dfa_init(
        &dfa, states, sizeof(states) / sizeof(states[0]), transitions,
        sizeof(transitions) / sizeof(transitions[0]));
    if (cmeta_infer_dfa_build(&dfa, &common_type_relation) !=
        CMETA_INFER_OK)
        return 1;
    if (cmeta_infer_dfa_eval(&dfa, input, 2u, &result) != CMETA_INFER_OK)
        return 2;
    return result == TYPE_WIDE ? 0 : 3;
}
```

The relation borrows its macro-projected static row array. The DFA owns no
memory: it borrows both arrays passed to `cmeta_infer_dfa_init`. The maximum
required capacities are `1 + rule_count * arity` states and
`rule_count * arity` transitions. These macros are intended for bounded
compile-time counts; code deriving capacities from untrusted runtime values
must perform checked arithmetic first.

`cmeta_infer_dfa_build` rejects duplicate input rows, conflicting results, and
insufficient workspaces. `cmeta_infer_dfa_eval` rejects wrong arity and reports
missing rules. Failed evaluation does not modify the output result. There is no
fallback mapping.

Building or rebuilding a DFA is a control-plane operation that requires no
concurrent readers. After a successful build, read-only evaluation is safe as
long as the relation and caller workspaces remain alive and unchanged. A
compiled executor should retain the inferred result or handler rather than
querying the DFA per value.

### Schema constant folds

`SchemaCount`, `SchemaAll`, and `SchemaAny` turn an existing schema into an
integer constant expression:

```c
#define Checks(M) Schema(M, (1), (1), (0))

_Static_assert(SchemaCount(Checks) == 3u, "row count");
_Static_assert(!SchemaAll(Checks), "not every row is true");
_Static_assert(SchemaAny(Checks), "at least one row is true");
```

`SchemaCount` accepts arbitrary non-empty row shapes because it ignores row
contents. `SchemaAll` and `SchemaAny` require every row to contain exactly one
integer constant expression. These folds retain the existing 16-row limit of a
single `Schema(...)` invocation.

### `Operators(...)`

CFlow's specialized operator-schema normalizer.

Structured source rows group call, function, flow, semantic, and effect
information, then normalize to the established flat consumer ABI before replay.
This keeps source syntax readable without inventing a second operator semantics.

`Operators(...)` is framework vocabulary. Application code normally consumes the
higher-level CFlow API instead of authoring raw operator schemas.

---

## 3. Runtime Protocol

The following are normal C APIs/protocols, not additional language keywords.

### Type metadata and identity

Core concepts include:

```text
cmeta_type_desc
cmeta_type_traits
cmeta_type_identity
CMETA_TYPEOF(T)
```

Descriptors carry semantic type information such as name, size, alignment,
kind, pointee, traits, and identity. Header-generated descriptor addresses are
not required to be process-global type IDs; consumers compare semantic type
identity/content.

`CMETA_TYPEOF(T)` is a finite `_Generic` lookup, not reflection over arbitrary C
types. `T` must be compatible with exactly one C type named by the active
`CMETA_TYPE_LIST`; by default that aliases `CMETA_CALLABLE_TYPE_LIST`, which in
turn defaults to `CMETA_KNOWN_TYPE_LIST`. A `typedef` alias reuses its underlying
compatible type's descriptor. Do not register a second row for such an alias,
because two compatible `_Generic` associations are a compile-time error.

Applications normally extend the defaults through `CMETA_USER_TYPE_LIST` in a
shared configuration header included before the first CMeta header in every
affected translation unit. Overrides of `CMETA_KNOWN_TYPE_LIST` and
`CMETA_CALLABLE_TYPE_LIST` follow the same rule. Changing the callable list
changes the ABI of `cmeta_sig` and `cmeta_callable`, so the list must match in
every translation unit and in the linked CMeta library build. Every custom row
must name descriptors and traits that the program defines with the ownership
operations required by its consumers.

The built-in type rows and finite callable relation graph are declared in
`formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CMeta/BuiltinSignatures.lean`.
Validation rejects empty categories, duplicates, and relations that reference
unknown built-in type tokens before rendering. The checked-in
`cmeta/generated/builtin_signature_manifest.h` preserves the public macro and
signature order, is consumed by normal C/C++ compilation without a Lean
dependency, and must not be edited manually. From the formal package, use:

```text
lake exe cmeta-signature-gen --write ../../cmeta/include/cmeta/generated/builtin_signature_manifest.h
lake exe cmeta-signature-gen --check ../../cmeta/include/cmeta/generated/builtin_signature_manifest.h
```

This generation boundary owns only built-ins. Application rows and
`CMETA_USER_UNARY_RELATION_LIST`, `CMETA_USER_BINARY_RELATION_LIST`, and
`CMETA_USER_GENERATOR_RELATION_LIST` remain shared compile-time configuration;
all translation units must still see the same callable ABI.

Mechanical consumers can select `CMETA_VALUE_SIGNATURES(U, B)` or
`CMETA_GENERATOR_SIGNATURES(G)` when their protocol is already known.
`CMETA_ALL_SIGNATURES(U, B, G)` remains the complete ABI universe and preserves
unary, binary, then generator ordering. Protocol grouping changes only which
adapters or switch cases are emitted at an already-validated boundary;
`cmeta_fn_invoke` and `cmeta_fn_generate` retain their documented rejection
results for the other protocol.

`cmeta_struct(T, ...)` and `cmeta_traits(T, ...)` generate structural and callable metadata,
but do not add `T` to either finite type universe. `CMETA_TYPEOF(T)` returns
`NULL` when no compatible registered type exists. APIs that require a descriptor
then fail according to their own contract: a container range lookup can return
`false`, range traversal can return `CMETA_GEN_ERROR`, and initialization or
collector boundaries can return invalid-argument or type-mismatch errors. Check
the exact API result; there is no implicit fallback.

### Callable protocol

Core runtime values include:

```text
cmeta_fn
cmeta_callable
cmeta_sig_desc
```

They provide typed signature metadata, effect/property contracts, erased invoke
or generator entry points, and optional inline captures.

### Range protocol

`cmeta_range` is an allocation-free borrowed traversal protocol. Range capability
flags include:

```text
SIZED
ORDERED
SORTED
UNIQUE
CONTIGUOUS
RANDOM_ACCESS
REUSABLE
```

Typed containers may expose default, key, value, or entry ranges through their
descriptors.

A Range borrows both its source object and its element descriptor.
The source handle storage must outlive the Range and every cursor used with it.
Creating a Range does not allocate, retain, or extend either lifetime. Unless a
concrete container promises otherwise, append, erase, resize, reset, destroy,
slot reuse, and undocumented cross-thread access invalidate the traversal.

When a Range supplies version tracking, `cmeta_range_next(...)` reports
`CMETA_GEN_MUTATED` before changing the cursor or output, but only while the
source handle storage remains alive. Version tracking cannot make expired or
freed storage safe, and a Range without a version callback cannot detect
mutation generically.

### Collector protocol

`cmeta_collector` is a bounded transactional protocol for constructing a
caller-owned output from typed borrowed values.

Stable states:

```text
ZERO
BEGUN
ACCEPTING
COMMITTED
ABORTED
```

The facade performs validation and exactly-once abort behavior but does not own a
general allocator, scheduler, retry system, or synchronization policy.

### Container descriptor protocol

`cmeta_container_desc` describes the runtime capabilities of a typed container,
including type metadata, Range factories, and an optional collector factory.
The raw algorithms remain ordinary C implementation code.

Declaration-side construction uses two distinct lifecycle operations:

```c
cmeta_status cmeta_container_bind_types(
    void *object, const cmeta_declared_type *declared);
cmeta_status cmeta_container_restore_zero(
    void *object, const cmeta_declared_type *declared);
```

`bind_types` accepts a canonical zero handle and installs its concrete
descriptor plus T/K/V metadata without allocating. `restore_zero` accepts a
zero, bound, active, or committed handle for the same declared provider,
releases provider-owned storage, and restores the complete handle to canonical
all-bits-zero. It returns `CMETA_TYPE_MISMATCH` when a nonzero handle belongs to
a different provider and `CMETA_INVALID_ARGUMENT` for an invalid declaration,
missing lifecycle callback, or invalid pointer.

Complete C usage is compiled in
`cstl/tests/cstl_construction_binding_test.c`.

The restore operation is not a synonym for `cmeta_collector_abort()`.
Collector abort owns only a begun/accepting collection transaction and remains
a no-op after commit; restore-to-zero is the declaration/provider lifecycle
boundary used by larger object transactions.

### Interface runtime values

`interface(...)` generates an ordinary C `{ self, vtable }` protocol value,
inline forwarding functions, capability metadata, and reflection metadata.

This value is an exact typed protocol capability. It is not the canonical
runtime-selected identity/lifetime representation for an arbitrary native
object; that role belongs to `cmeta_object_ref`. The two compose through an
explicit provider relation:

```text
one native provider
    +-- cmeta_object_ref     identity / field+receiver reflection / lifetime
    +-- Interface A         typed protocol capability
    +-- Interface B         typed protocol capability
```

An ObjectRef-to-Interface view is normally borrowed. Its validity is bounded by
the object/provider lifetime and, for Plugin-backed code, by the outer Plugin
lease. CMeta never derives this relation from pointer equality, struct offsets,
vtable layout, descriptor names, or semantic descriptor equality.

A method marked `CMETA_INTERFACE_METHOD_OWNS_SELF` is different: dispatch
consumes the Interface self owner and invalidates that handle. Such an Interface
cannot be exposed as a borrowed view of a still-live ObjectRef without creating
two competing ownership authorities. Runtime projection must therefore reject
that shape until an explicit ownership transfer is performed. The canonical
query is `cmeta_interface_desc_has_owning_method()`.

This does not affect static dispatch. Compile-time-known Interface calls remain
ordinary typed calls through their generated wrappers/vtable; ObjectRef is not a
mandatory hop on the hot path.

---

## 4. Reserved future syntax

The following names describe useful directions but are **not current language
features and are not compatibility promises**:

```text
Lambda
Bind
Variant
Match
Array
SmallVec
RingBuffer
```

Related ideas may already exist in Lean models, CFlow internals, experiments, or
ordinary C implementations. They become CMeta syntax only after a concrete,
useful, maintainable C11 implementation exists.

The preferred evolution rule is:

1. solve the concrete use case with ordinary C and existing CMeta patterns;
2. identify repeated boilerplate or a stable semantic pattern;
3. add the smallest composable CMeta abstraction;
4. validate it on GCC, Clang, and MSVC portability lanes;
5. add formal proof only when the new feature needs a property not already
   covered by existing evidence.

CMeta does not need to "complete" a universal language design before it is
useful.

---

## Removed syntax

### `Containers(...)`

Removed. Do not keep or reintroduce it as an alias.

Old style:

```c
/* removed */
Containers(
    (List, IntList, int),
    (Vec, IntVec, int)
);
```

Current style:

```c
#include <cstl/typed.h>

cmeta_type(List, IntList, int);
cmeta_type(Vec, IntVec, int);
```

The explicit form keeps one generic entry point and avoids a second batch DSL
that adds no semantic capability.

### Container `implement(...)`

The earlier declaration/implementation split is also removed. Typed container
instantiation is single-stage.

---

## Design rule of thumb

Use the smallest layer that solves the problem:

```text
ordinary C
    ↓ when repetitive metadata/code generation appears
Application DSL
    ↓ when a library needs reusable compile-time row generation
Framework DSL
    ↓ for execution/storage/interoperation
Runtime Protocol
```

Do not add a new keyword when an existing declaration, schema mapper, descriptor,
interface, or ordinary C function composes cleanly enough.
