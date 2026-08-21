from pathlib import Path

STATUS = "Plan A implemented and exact-head verified; Plan B pending"


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, found {count}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    "docs/superpowers/specs/2026-08-21-cmeta-lean-module-system-migration-design.md",
    "**Status:** Design approved in chat; awaiting spec review before implementation planning  ",
    f"**Status:** {STATUS}  ",
)

replace_once(
    "docs/superpowers/specs/2026-08-21-cmeta-lean-module-system-migration-plan-a-amendment.md",
    "**Status:** Approved-design execution clarification  ",
    f"**Status:** {STATUS}  ",
)

errata_path = Path("docs/superpowers/specs/2026-08-21-cmeta-lean-module-system-plan-a-errata.md")
errata = errata_path.read_text()
if "**Status:**" not in errata.split("## 1.", 1)[0]:
    errata = errata.replace(
        "# CMeta Lean Module-System Plan A Execution Errata\n\n",
        f"# CMeta Lean Module-System Plan A Execution Errata\n\n**Status:** {STATUS}  \n",
        1,
    )
section = """
## 5. Permanent `CType.denote` exposure

Actual Lean 4.30 module execution showed that executable conformance models must reduce `CType.denote` across module boundaries in order to elaborate host-language value types such as `Int`, `Bool`, and `Float`.

Therefore the supported semantic declaration remains intentionally:

```lean
@[expose] public def CType.denote : CType → Type
```

This is part of the public semantic vocabulary, not a `TEMP-MODULE-BRIDGE`. The M6 cleanup rule “remove migration-only `@[expose]` declarations” does **not** apply to `CType.denote`.

The final static audit for Plan A is therefore:

- no `TEMP-MODULE-BRIDGE` marker remains in production formal sources;
- no migration-only `@[expose]` remains;
- `CType.denote` is the explicit permanent `@[expose]` exception required by the supported executable semantics.
"""
if "## 5. Permanent `CType.denote` exposure" not in errata:
    errata = errata.rstrip() + "\n\n" + section.strip() + "\n"
errata_path.write_text(errata)

readme_path = Path("formal/README.md")
readme = readme_path.read_text()
readme_section = """
## Lean module-system migration status

Plan A (M1–M6) of the Lean 4.30 module-system migration is implemented and exact-head verified. The CFlow semantic spine now uses explicit module visibility, `CMeta.PublicProof` exposes the curated semantic vocabulary plus six stable end-to-end wrapper theorems, and representative Graph, Lowering, Optimize, Execution, and EndToEnd proof plumbing is not visible to a downstream client importing only `CMeta.PublicProof`.

The real-C generated Lean snapshots for the direct, structured, and optimizer conformance paths are module-framed by their authoritative C witnesses and remain protected by byte-for-byte CI regeneration checks. `CType.denote` is intentionally `@[expose] public` because executable conformance models must reduce the logical CType universe to host value types across module boundaries.

Plan B is still pending. It will migrate the independent Producer / Replay / Registry / LanguageSpec tree, create the final internal build aggregator, and convert the root `CMeta` module without reducing kernel-check coverage.
"""
if "## Lean module-system migration status" not in readme:
    readme = readme.rstrip() + "\n\n" + readme_section.strip() + "\n"
readme_path.write_text(readme)

print("Plan A docs finalization complete")
