from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, found {count}")
    p.write_text(text.replace(old, new, 1))


c_witnesses = [
    "formal/cmeta_structured_conformance_witness.c",
    "formal/cmeta_structured_policy_conformance_witness.c",
    "formal/cmeta_optimizer_conformance_witness.c",
    "formal/cmeta_optimizer_gating_conformance_witness.c",
    "formal/cmeta_optimizer_topology_conformance_witness.c",
]

for path in c_witnesses:
    replace_once(
        path,
        '    puts("import Std");\n',
        '    puts("module");\n    puts("import Std");\n',
    )

headers = {
    "formal/CMeta/StructuredConformance.lean": (
        "import CMeta.RuntimeConformance\nimport CMeta.StructuredGeneratedC\n",
        "module\nimport all CMeta.RuntimeConformance\nimport all CMeta.StructuredGeneratedC\n"
        "import all CMeta.Graph\nimport all CMeta.Lowering\n",
    ),
    "formal/CMeta/StructuredPolicyConformance.lean": (
        "import CMeta.StructuredConformance\nimport CMeta.StructuredPolicyGeneratedC\n",
        "module\nimport all CMeta.StructuredConformance\n"
        "import all CMeta.StructuredPolicyGeneratedC\n",
    ),
    "formal/CMeta/OptimizerConformance.lean": (
        "import CMeta.StructuredPolicyConformance\nimport CMeta.OptimizerGeneratedC\n",
        "module\nimport all CMeta.StructuredPolicyConformance\n"
        "import all CMeta.OptimizerGeneratedC\nimport all CMeta.Optimize\n"
        "import all CMeta.Callable\n",
    ),
    "formal/CMeta/OptimizerGatingConformance.lean": (
        "import CMeta.OptimizerConformance\nimport CMeta.OptimizerGatingGeneratedC\n",
        "module\nimport all CMeta.OptimizerConformance\n"
        "import all CMeta.OptimizerGatingGeneratedC\n",
    ),
    "formal/CMeta/OptimizerTopologyConformance.lean": (
        "import CMeta.OptimizerGatingConformance\nimport CMeta.OptimizerTopologyGeneratedC\n",
        "module\nimport all CMeta.OptimizerGatingConformance\n"
        "import all CMeta.OptimizerTopologyGeneratedC\n",
    ),
}

for path, (old, new) in headers.items():
    replace_once(path, old, new)

for path in [
    "formal/CMeta/StructuredGeneratedC.lean",
    "formal/CMeta/StructuredPolicyGeneratedC.lean",
    "formal/CMeta/OptimizerGeneratedC.lean",
    "formal/CMeta/OptimizerGatingGeneratedC.lean",
    "formal/CMeta/OptimizerTopologyGeneratedC.lean",
]:
    text = Path(path).read_text()
    if not text.startswith("module\nimport Std\n"):
        raise SystemExit(f"{path}: committed snapshot is not module-framed")

print("M6b mechanical moduleization complete")
