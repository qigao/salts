#!/usr/bin/env python3
import json
import statistics
import sys
from pathlib import Path

if len(sys.argv) != 2:
    raise SystemExit("usage: verify_tls_provider_parallel_benchmark.py <jsonl>")

rows = []
for raw in Path(sys.argv[1]).read_text(encoding="utf-8").splitlines():
    line = raw.strip()
    if not line.startswith("{"):
        continue
    item = json.loads(line)
    if item.get("benchmark") == "cnet_tls_provider_parallel":
        rows.append(item)

expected_modes = {"push", "echo"}
expected_temperatures = {"fresh", "warm"}
expected_payloads = {16384, 32768, 65536}
expected_owners = {1, 2, 4, 8}

groups = {}
for row in rows:
    key = (
        row["mode"],
        row["temperature"],
        int(row["payload_bytes"]),
        int(row["owners"]),
    )
    groups.setdefault(key, []).append(row)

expected_keys = {
    (mode, temp, payload, owners)
    for mode in expected_modes
    for temp in expected_temperatures
    for payload in expected_payloads
    for owners in expected_owners
}
if set(groups) != expected_keys:
    missing = sorted(expected_keys - set(groups))
    extra = sorted(set(groups) - expected_keys)
    raise SystemExit(f"matrix mismatch missing={missing} extra={extra}")

repeat_counts = {len(points) for points in groups.values()}
if len(repeat_counts) != 1:
    raise SystemExit(f"repeat count mismatch: {sorted(repeat_counts)}")
repeat_count = next(iter(repeat_counts))
if repeat_count < 3:
    raise SystemExit(f"expected at least 3 repeats, got {repeat_count}")

def median(points, field):
    return statistics.median(float(row[field]) for row in points)

for key, points in groups.items():
    mode, temp, payload, owners = key
    operations = {int(row["operations"]) for row in points}
    if len(operations) != 1:
        raise SystemExit(f"{key}: operation count varied")
    for row in points:
        if int(row.get("errors", -1)) != 0:
            raise SystemExit(f"{key}: errors={row.get('errors')}")
        if int(row["pairs"]) != 8:
            raise SystemExit(f"{key}: pairs != 8")
        if int(row["samples"]) != int(row["operations"]):
            raise SystemExit(f"{key}: sample/operation mismatch")
        for field in (
            "ops_per_second",
            "cpu_ns_per_op",
            "p50_ns",
            "p95_ns",
            "p99_ns",
            "tls_records_per_op",
            "cipher_bytes_per_op",
        ):
            if float(row[field]) <= 0:
                raise SystemExit(f"{key}: invalid {field}={row[field]}")
        expected_records = (payload + 16383) // 16384
        if mode == "echo":
            expected_records *= 2
        actual = float(row["tls_records_per_op"])
        if abs(actual - expected_records) > 1e-6:
            raise SystemExit(
                f"{key}: TLS records/op {actual} != expected {expected_records}"
            )

print(f"repeats per point: {repeat_count}")
print("fixed TLS sessions per point: 8")
print()

for mode in ("push", "echo"):
    for temp in ("fresh", "warm"):
        print(f"### {mode} / {temp}")
        print()
        print(
            "| payload | owners | ops/s | speedup | p50 us | p95 us | p99 us | "
            "worker CPU ns/op | CPU ratio | TLS records/op | cipher bytes/op |"
        )
        print(
            "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
        )
        for payload in sorted(expected_payloads):
            baseline = groups[(mode, temp, payload, 1)]
            baseline_rate = median(baseline, "ops_per_second")
            baseline_cpu = median(baseline, "cpu_ns_per_op")
            for owners in (1, 2, 4, 8):
                points = groups[(mode, temp, payload, owners)]
                rate = median(points, "ops_per_second")
                cpu = median(points, "cpu_ns_per_op")
                print(
                    f"| {payload} | {owners} | {rate:.1f} | "
                    f"{rate / baseline_rate:.3f}x | "
                    f"{median(points, 'p50_ns') / 1000.0:.1f} | "
                    f"{median(points, 'p95_ns') / 1000.0:.1f} | "
                    f"{median(points, 'p99_ns') / 1000.0:.1f} | "
                    f"{cpu:.1f} | {cpu / baseline_cpu:.3f}x | "
                    f"{median(points, 'tls_records_per_op'):.1f} | "
                    f"{median(points, 'cipher_bytes_per_op'):.1f} |"
                )
        print()

print(
    "Provider-only gate: eight independent verified TLS sessions, fixed total work, "
    "1/2/4 worker threads, exact record counts and byte-for-byte decrypt validation."
)
print(
    "This isolates GmSSL/CNet TLS-state contention; it is not the final public "
    "CNet owner/socket acceptance for issue #804."
)
