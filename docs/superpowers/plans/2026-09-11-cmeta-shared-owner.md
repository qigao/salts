# CMeta DSO ownership repair plan and rationale

Goal: repair salts#252 without suppressing ASan or changing public layouts, type equality, package targets or deployment.
Evidence: salts-utils run 34585495003 artifact 10193521853 has 22/24 targets passing; DataBind CMeta and CFlow fail when Core and the bridge expose copies of cmeta_type_double_ptr.

## Regression

Build the production cmeta/CMakeLists.txt target and all existing CMeta tests. Two shared consumers each reference a descriptor from a static initializer and call the type registry. Verify semantic type equality across both DSOs and consistency within each image. Verify that static CMeta descriptors remain present locally but are absent from each DSO's exported symbols. Run ASan+UBSan with detect_odr_violation=2 and the installed exported package target. Windows covers C static initialization and DLL consumer linkage.

The initial dual-DSO regression reproduced the original ODR failure in CI run 34587494843 and locally. Its address-equality assumption was corrected to the documented semantic-identity contract; that corrected test was also observed RED before the production fix.

## Minimal production fix

For STATIC_LIBRARY only, set C_VISIBILITY_PRESET=hidden on salts_cmeta. This prevents ELF preemption of immutable descriptors and helper functions copied from the static archive. Do not change the shared-CMeta target's exports or propagate hidden visibility to callers.

A mandatory shared CMeta DLL was rejected: public macros use descriptor addresses in C static initializers, for which imported DLL data is not a constant expression. CMeta's README explicitly uses semantic identity, not descriptor addresses, as the type contract. Preserve that contract rather than inventing process-global pointer IDs.

## Verification and integration

Run static and explicitly shared Linux configurations with ASan+UBSan, Windows static linkage, and installed consumers. Revert only the visibility fix in a disposable copy to prove the runtime and symbol regressions fail. Review symbol scope and ABI compatibility separately from test sensitivity.

Pin salts-utils only to the verified exact Core revision, then run the original failing targets and complete TBE suite. Make the downstream broad suite blocking and correct earlier inaccurate PR evidence. Do not merge automatically.
