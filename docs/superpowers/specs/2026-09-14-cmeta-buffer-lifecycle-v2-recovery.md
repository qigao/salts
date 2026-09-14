# CMeta Buffer Lifecycle V2 Recovery

## Scope

This branch reconstructs the lost post-#264 Core work required by salts-utils #47 after the original local-only commits became unavailable to GitHub.

The recovery is intentionally strict:

- `cmeta_data_buffer_ops` requires the complete v2 lifecycle (`init_zero`, assign/read where applicable, `move`, and `restore_zero`).
- Old/incomplete buffer providers are rejected; there is no compatibility or fallback path.
- `tstr` publishes a canonical Core-owned unique-owned buffer provider.
- CSTL publishes `stl_byte_buffer` plus its canonical owning byte-buffer provider.
- UUID keeps its exact fixed-value authority while its buffer provider is upgraded to the same lifecycle admission contract.
- salts-utils remains the DataBind owner and will consume these providers; Core does not absorb DataBind behavior.

## Recovery provenance

The branch is rebuilt on the merged Core head rather than replaying unavailable local Git objects, so recovered commits have new SHAs. The semantic boundary is preserved while old local SHA identity is intentionally not claimed.
