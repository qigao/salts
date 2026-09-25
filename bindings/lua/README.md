# Lua binding

The Lua binding is the runtime projection of canonical CMeta data/function
reflection into Lua.

The migration source currently lives in salts-utils/tools/lua. New work must
move the generic CMeta bridge here rather than extending DataBind/TBE-specific
Lua glue.

Required contract:

- scalar/enum values use canonical CMeta identities;
- struct members come from CMeta field metadata;
- string/bytes use explicit CMeta buffer providers and ownership;
- sequence/set/map require explicit CMeta collection providers;
- functions/interfaces use CMeta invocation metadata;
- no DataBind/TBE descriptors or schema-specific wrapper metadata;
- bounded recursion/conversion and fail-closed unsupported providers.
