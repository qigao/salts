# CMeta shared-owner repair plan

Goal: repair salts#252 without suppressing ASan, changing descriptor layouts, or weakening schema checks.
Spec: salts#252 and the exact-head salts-utils run 34585495003 artifact 10193521853.

## RED: shared-library regression

- Build the real cmeta/CMakeLists.txt target and existing CMeta tests from tests/cmeta_shared_owner.
- Link two shared consumers, each directly referencing cmeta_type_double_ptr and using cmeta_type_find; an executable verifies direct, registry and pointee identity across both boundaries.
- Run ASan+UBSan with detect_odr_violation=2, upload exact source and test logs. Test the same consumers against installed exported targets. Run a Windows compiler to check DLL data imports.
- Observe the runtime duplicate global failure before changing production code.

## GREEN: canonical owner

- Make Salts::CMeta a shared target so Core and bridges dynamically reference a single descriptor and registry owner instead of embedding a transitive static archive.
- Preserve the target name, header names, layouts and API signatures. Handle Windows exported data explicitly; exported functions must remain linkable.
- Keep the existing package install/export rules and verify installed consumers. Review consumer build flags, C++ declarations and all public external data symbols.
- Re-run all CMeta unit and shared-owner tests, then the original downstream failures and the whole TBE suite with both sanitizers.

## Downstream and review

- Only pin salts-utils to the exact tested Core revision; make its broad TBE suite blocking once green.
- Correct the previous PR report: raw evidence says 22/24 with DataBind CMeta and CFlow failing, not 74/75 or a Lua bridge failure.
- Review ABI ownership and test sensitivity separately. Record only observed CI results and leave unverified platforms explicit. Do not merge automatically.
