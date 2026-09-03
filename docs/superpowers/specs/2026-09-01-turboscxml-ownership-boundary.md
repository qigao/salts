# TurboSCXML Repository Ownership Boundary

## Background

TurboSCXML has been extracted into `qigao/turbo-scxml`. That repository now
owns the public `<scxml/scxml.h>` API, the `TurboSCXML::SCXML` CMake target,
the XML-to-StateChart compiler, the SCXML session runtime, its conformance
corpus, and its package-consumer test. Salts still contains and exports
the older `cflow-scxml` copy as the optional `Salts::CFlowScxml` target.
Publishing both copies creates two writable facts for one interpreter.

Salts remains the owner of CFlow StateChart construction and execution,
including the exact V4 host-transaction protocol consumed by TurboSCXML.
That lower-level protocol is format-neutral and must not depend on XML,
SCXML, CSerde, or an SCXML data model.

## Decision

Remove the complete in-tree `cflow-scxml` module and every build, install, and
consumer switch that publishes it from Salts. TurboSCXML becomes the sole
SCXML implementation and package. Salts documentation retains a short
ownership pointer but no duplicate SCXML API or behavior documentation.

The following Salts interfaces are removed together:

- the `CFLOW_ENABLE_SCXML` CMake option;
- the `cflow-scxml/` source, public header, tests, fixtures, and corpus;
- the build-tree and installed `Salts::CFlowScxml` target;
- `EXPECT_CFLOW_SCXML`, `SALTS_EXPECT_CFLOW_SCXML`, and the installed
  consumer branch for `<cflow/scxml.h>`.

No compatibility alias or fallback target is provided. A consumer that still
requests any removed interface must fail during configure or compile rather
than silently bind another implementation.

## Alternatives

### Keep both implementations

Rejected. Each repository could change parser behavior, ABI, test corpus, and
dependency policy independently. Passing one repository's tests would no
longer establish which implementation an application receives.

### Keep a forwarding `Salts::CFlowScxml` target

Rejected. Salts cannot export a reliable alias to a package that is not a
Salts dependency, and making TurboSCXML a Salts dependency would
reverse the intended dependency direction.

### Deprecate before removal

Rejected for this extraction because there are no supported downstream users
to preserve and the project has explicitly chosen the standalone repository as
the new package boundary. Retaining the old target would preserve the dual
fact source that this change is intended to eliminate.

## Architectural Effects

- Dependency direction is `TurboSCXML -> Salts::CFlow`; CFlow never
  depends on TurboSCXML.
- Statechart configuration, extended state, queues, and host effect tickets
  remain owned by one `cflow_statechart_instance`.
- XML parsing and SCXML-specific state live only in TurboSCXML.
- Salts package size and its optional configuration matrix shrink; SCXML
  validation moves to the standalone repository.

## Compatibility and Migration

This is an intentional source and CMake package break for consumers of
`CFLOW_ENABLE_SCXML`, `Salts::CFlowScxml`, or `<cflow/scxml.h>`.
Consumers migrate by installing TurboSCXML, including `<scxml/scxml.h>`, and
linking `TurboSCXML::SCXML`. CFlow and StateChart users require no migration.

## Verification

1. A clean Salts configure must not accept or export the removed target.
2. Salts build, all `cflow_*` tests, and installed-package consumers must
   pass without an SCXML feature switch.
3. A repository-wide production/build scan must find no removed target,
   header, or consumer macro. The old option name may appear only in the
   configure-time migration guard that rejects it explicitly.
4. TurboSCXML must configure against the newly installed Salts package,
   then build and pass its focused tests and installed consumer.

## Rollback

Rollback is a Git revert of this removal commit. No user data or persisted
format changes are involved. Reintroducing the module by copying newer
TurboSCXML sources back into Salts is not an acceptable rollback because
it would recreate dual ownership.
