# Parser Ownership Migration Design

## Background

The low-level parser implementations previously lived under
`turbo-parser/parser`. They depend on TurboUtils primitives (`Core`, `STL`,
`CSerde`, memory pools, files, and TinyTest), while the higher-level
`TurboParser::Parser` facade, Mustache, Cron, TBE schema, DataBind, and the TBE
compiler compose those implementations.

The `parser` directory has been moved to `turbo-utils/parser`. The physical
move alone leaves TurboParser unable to configure and leaves TurboUtils unable
to build or install the moved code. This migration closes that build and
package boundary.

## Decision

TurboUtils owns and exports the low-level parser and query targets under the
`TurboUtils::` namespace. TurboParser remains the owner of the unified parser
facade, XML vendor integration, Mustache, Cron, TBE schema, TBE CBind,
DataBind, and schema generation.

The JSON-to-CSerde reader is part of `TurboUtils::JsonParser`. `TurboUtils::CBind`
continues to consume the format-neutral `cserde_reader` interface. TBE CBind is
not moved because its schema model and generated sidecars depend on the TBE
schema/compiler fact source.

## Target Mapping

| Build target | Installed target |
|---|---|
| `query_vm` | `TurboUtils::QueryVM` |
| `json_parser` | `TurboUtils::JsonParser` |
| `csv_parser` | `TurboUtils::CsvParser` |
| `ini_parser` | `TurboUtils::IniParser` |
| `uri_parser` | `TurboUtils::UriParser` |
| `tlv_parser` | `TurboUtils::TLVParser` |
| `ltv_parser` | `TurboUtils::LtvParser` |
| `modbus_parser` | `TurboUtils::ModbusParser` |
| `soa_parser` | `TurboUtils::SoaParser` |
| `dotenv_parser` | `TurboUtils::DotEnvParser` |
| `cmd_parser` | `TurboUtils::CmdParser` |
| `toon` | `TurboUtils::Toon` |
| `toml_parser` | `TurboUtils::TomlParser` |
| `datetime_parser` | `TurboUtils::DateTimeParser` |
| `cyaml` | `TurboUtils::CYaml` |
| `cyaml_json_adapter` | `TurboUtils::CYamlJsonAdapter` |
| `turbo_selector` | `TurboUtils::Selector` |

## Dependency Direction

```text
application -> TurboParser facade -> TurboUtils parser targets
                                  -> TurboUtils Core/CSerde/CBind

application -> TurboUtils parser targets -> TurboUtils Core/CSerde/STL
```

TurboUtils never finds or links TurboParser. TurboParser finds one installed
TurboUtils package and uses its exported parser targets. Private vendor targets
such as `cxml` remain in TurboParser and do not escape its installed interface.

## Public API And Compatibility

The C headers, parser input/output semantics, ownership rules, diagnostics, and
error codes remain unchanged. The CMake ownership changes from build-tree
`TurboParser::*` aliases to installed `TurboUtils::*` targets. This is an
intentional package API change authorized by the repository refactor.

The TurboParser facade continues to export `TurboParser::Parser`; its public C
and C++ headers remain stable. Low-level consumers must link the corresponding
`TurboUtils::*` target.

## State And Failure Semantics

This refactor moves build ownership only; it does not add runtime state or
change parser state machines. Configuration fails immediately when the
installed TurboUtils package lacks a required parser target. There is no source
tree fallback and no duplicate parser implementation.

## Alternatives

1. Keep parser ownership in TurboParser. This preserves the old package layout
   but contradicts the requested repository move.
2. Copy parser sources into both repositories. This creates two fact sources
   and makes fixes, generated grammar behavior, and security patches diverge.
3. Export only one aggregate parser library. This hides useful component
   boundaries but still requires exporting every static dependency or changing
   the facade into that aggregate library.

The selected component-target approach preserves the existing module layout
and makes every cross-repository dependency explicit.

## Migration And Rollback

Migration order is: add failing installed-consumer coverage, export parser
targets from TurboUtils, install headers, switch TurboParser links, install
TurboUtils, then configure/build/test TurboParser against that installation.

Rollback restores `parser/` to TurboParser from Git, reverts the two root CMake
files and target namespaces, and removes the TurboUtils parser consumer tests.
Because source history remains in the original Git repository until commits are
finalized, rollback does not require reconstructing file contents.

## Verification

- Configure TurboUtils with its versioned Windows Release preset.
- Build and run QueryVM, JSON parser, JSON-to-CSerde, and CBind integration tests.
- Run the TurboUtils test suite and installation target.
- Configure and run the external TurboUtils install consumer using
  `TurboUtils::JsonParser` and `TurboUtils::CBind` together.
- Configure, build, and test TurboParser against the installed TurboUtils SDK.
- Inspect both exported target files for source-tree paths and obsolete
  low-level `TurboParser::*` dependencies.
