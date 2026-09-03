# Parser Ownership Migration Design

## Background

The low-level parser implementations previously lived under
`turbo-parser/parser`. They depend on Salts primitives (`Core`, `STL`,
memory pools, files, and TinyTest), while the higher-level
`TurboParser::Parser` facade, Mustache, Cron, TBE schema, DataBind, and the TBE
compiler compose those implementations.

The `parser` directory has been moved to `salts/parser`. The physical
move alone leaves TurboParser unable to configure and leaves Salts unable
to build or install the moved code. This migration closes that build and
package boundary.

## Decision

Salts owns and exports the low-level parser and query targets under the
`Salts::` namespace. TurboParser remains the owner of the unified parser
facade, XML vendor integration, Mustache, Cron, TBE schema, DataBind, and
schema generation.

The JSON parser has no CSerde dependency. The optional JSON-to-CSerde bridge is
exported separately as `Salts::JsonCSerdeAdapter`; consumers opt into that
composition explicitly. TurboParser does not consume this adapter, CSerde, or
CBind.

## Target Mapping

| Build target | Installed target |
|---|---|
| `query_vm` | `Salts::QueryVM` |
| `json_parser` | `Salts::JsonParser` |
| `json_cserde_adapter` | `Salts::JsonCSerdeAdapter` |
| `csv_parser` | `Salts::CsvParser` |
| `ini_parser` | `Salts::IniParser` |
| `uri_parser` | `Salts::UriParser` |
| `tlv_parser` | `Salts::TLVParser` |
| `ltv_parser` | `Salts::LtvParser` |
| `modbus_parser` | `Salts::ModbusParser` |
| `soa_parser` | `Salts::SoaParser` |
| `dotenv_parser` | `Salts::DotEnvParser` |
| `cmd_parser` | `Salts::CmdParser` |
| `toon` | `Salts::Toon` |
| `toml_parser` | `Salts::TomlParser` |
| `datetime_parser` | `Salts::DateTimeParser` |
| `cyaml` | `Salts::CYaml` |
| `cyaml_json_adapter` | `Salts::CYamlJsonAdapter` |
| `salts_selector` | `Salts::Selector` |

## Dependency Direction

```text
application -> TurboParser facade -> Salts parser targets
                                  -> Salts Core

application -> Salts parser targets -> Salts Core/STL

adapter consumer -> Salts::JsonCSerdeAdapter
                 -> Salts::JsonParser + Salts::CSerde
```

Salts never finds or links TurboParser. TurboParser finds one installed
Salts package and uses its exported parser targets. Private vendor targets
such as `cxml` remain in TurboParser and do not escape its installed interface.

## Public API And Compatibility

The C headers, parser input/output semantics, ownership rules, diagnostics, and
error codes remain unchanged. The CMake ownership changes from build-tree
`TurboParser::*` aliases to installed `Salts::*` targets. This is an
intentional package API change authorized by the repository refactor.

The TurboParser facade continues to export `TurboParser::Parser`; its public C
and C++ headers remain stable. Low-level consumers must link the corresponding
`Salts::*` target.

## State And Failure Semantics

This refactor moves build ownership only; it does not add runtime state or
change parser state machines. Configuration fails immediately when the
installed Salts package lacks a required parser target. There is no source
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
targets from Salts, install headers, switch TurboParser links, install
Salts, then configure/build/test TurboParser against that installation.

Rollback restores `parser/` to TurboParser from Git, reverts the two root CMake
files and target namespaces, and removes the Salts parser consumer tests.
Because source history remains in the original Git repository until commits are
finalized, rollback does not require reconstructing file contents.

## Verification

- Configure Salts with its versioned Windows Release preset.
- Build and run QueryVM, JSON parser, JSON-to-CSerde, and CBind integration tests.
- Run the Salts test suite and installation target.
- Configure and run the external Salts install consumer using the explicit
  `Salts::JsonCSerdeAdapter` composition target.
- Configure, build, and test TurboParser against the installed Salts SDK.
- Inspect both exported target files for source-tree paths and obsolete
  low-level `TurboParser::*` dependencies.
