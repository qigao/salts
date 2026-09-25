# QuickJS binding

QuickJS starts directly as a CMeta-driven runtime binding.

Do not copy the historical generated Lua/TBE binding model. JavaScript object
properties, functions and ownership must be projections of canonical CMeta
metadata.

TypeScript declaration generation belongs to the DataBind/compiler projection
layer and is not part of this runtime module.

## Runtime projection

- strings become JavaScript strings and bytes become owned `ArrayBuffer` copies;
- structs become plain objects whose property names come from CMeta fields;
- sequences and sets become arrays;
- maps become arrays of `{key, value}` entries, preserving non-string and
  repeated keys without inventing a JavaScript-object key policy;
- native reads use CMeta temporary storage and publish only after full success;
- reflected functions and interface methods invoke through `cmeta_invokable`.

The binding never retains native object pointers. Values returned to QuickJS
own their JavaScript storage, while native descriptors and invokables remain
borrowed for the duration of each call.
