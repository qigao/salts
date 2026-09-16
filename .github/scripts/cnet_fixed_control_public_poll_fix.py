from pathlib import Path

path = Path("cnet/benchmarks/cnet_io_benchmark.c")
text = path.read_text()


def replace_once(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"expected exactly one match, got {count}: {old!r}")
    text = text.replace(old, new, 1)


replace_once(
    "  owner = &result->cnet_profile.owner;\n  sample = (cnet_benchmark_fixed_control_sample){\n",
    "  owner = &result->cnet_profile.owner;\n"
    "  if (result->cnet_poll_ns < result->cnet_profile.client_poll_ns) return SALTS_ERANGE;\n"
    "  sample = (cnet_benchmark_fixed_control_sample){\n",
)
replace_once(
    "      .client_poll_ns = result->cnet_profile.client_poll_ns,\n",
    "      .client_poll_ns = result->cnet_poll_ns,\n",
)
replace_once(
    '         "payload copy + benchmark work + closure residual = profiled send + client poll.\\n");\n',
    '         "payload copy + benchmark work + closure residual = profiled send + public client poll.\\n");\n',
)

path.write_text(text)
