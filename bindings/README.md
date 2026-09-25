# Salts language bindings

This directory owns runtime language interoperability built on the canonical
Salts CMeta contracts.

Bindings are consumers of CMeta reflection, invocation and provider metadata.
They do not define schema semantics, native type identity, ownership models, or
format/wire layouts.

## Modules

- `lua/` — CMeta <-> Lua runtime binding.
- `quickjs/` — CMeta <-> QuickJS runtime binding.

## Dependency rule

```text
Lua / QuickJS
      |
      v
    CMeta
```

A binding must not depend on DataBind, TBE, a generated schema AST, or another
binding module. DataBind and other higher-level systems expose their native
objects through CMeta before invoking a binding.

Collection access is also a CMeta provider contract. A binding must not infer
container storage from CSTL/vec_t or from another consumer such as Jinja.

TypeScript declaration generation is compiler tooling and is intentionally not
owned by the QuickJS runtime binding.
