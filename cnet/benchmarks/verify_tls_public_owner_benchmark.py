#!/usr/bin/env python3
import json
import statistics
import sys
from pathlib import Path

if len(sys.argv) != 2:
    raise SystemExit("usage: verify_tls_public_owner_benchmark.py <jsonl>")

rows = []
for raw in Path(sys.argv[1]).read_text(encoding="utf-8").splitlines():
    line = raw.strip()
    if not line.startswith("{"):
        continue
    item = json.loads(line)
    if item.get("benchmark") == "cnet_tls_public_loopback_parallel":
        rows.append(item)

expected = {
    (nodelay, mode, temp, payload, owners)
    for nodelay in (0, 1)
    for mode in ("push", "echo")
    for temp in ("fresh", "warm")
    for payload in (16384, 32768, 65536)
    for owners in (1, 2, 4)
}
groups = {}
for row in rows:
    key = (
        int(row.get("nodelay", -1)),
        row["mode"],
        row["temperature"],
        int(row["payload_bytes"]),
        int(row["owners"]),
    )
    groups.setdefault(key, []).append(row)

if set(groups) != expected:
    raise SystemExit(
        f"matrix mismatch missing={sorted(expected-set(groups))} "
        f"extra={sorted(set(groups)-expected)}"
    )

repeat_counts = {len(points) for points in groups.values()}
if len(repeat_counts) != 1:
    raise SystemExit(f"repeat count mismatch: {sorted(repeat_counts)}")
repeat_count = next(iter(repeat_counts))
if repeat_count < 3:
    raise SystemExit(f"expected at least 3 repeats, got {repeat_count}")

def median(points, field):
    return statistics.median(float(row[field]) for row in points)

for key, points in groups.items():
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
            "owner_cpu_ns_per_op",
            "p50_ns",
            "p95_ns",
            "p99_ns",
        ):
            if float(row[field]) <= 0:
                raise SystemExit(f"{key}: invalid {field}={row[field]}")

print(f"repeats per point: {repeat_count}")
print("fixed public TLS loopback pairs per point: 8")
print("TLS version: 1.3 required on both public CNet endpoints")
print()

for nodelay in (0, 1):
    for mode in ("push", "echo"):
        for temp in ("fresh", "warm"):
            print(f"### public CNet {mode} / {temp} / nodelay={nodelay}")
        print()
        print(
            "| payload | owners | ops/s | speedup | p50 us | p95 us | p99 us | "
            "owner CPU ns/op | CPU ratio |"
        )
        print(
            "| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
        )
            for payload in (16384, 32768, 65536):
                base = groups[(nodelay, mode, temp, payload, 1)]
                base_rate = median(base, "ops_per_second")
                base_cpu = median(base, "owner_cpu_ns_per_op")
                for owners in (1, 2, 4):
                    points = groups[(nodelay, mode, temp, payload, owners)]
                rate = median(points, "ops_per_second")
                cpu = median(points, "owner_cpu_ns_per_op")
                print(
                    f"| {payload} | {owners} | {rate:.1f} | "
                    f"{rate/base_rate:.3f}x | "
                    f"{median(points, 'p50_ns')/1000.0:.1f} | "
                    f"{median(points, 'p95_ns')/1000.0:.1f} | "
                    f"{median(points, 'p99_ns')/1000.0:.1f} | "
                    f"{cpu:.1f} | {cpu/base_cpu:.3f}x |"
                )
        print()

print(
    "Public-path gate: real CNet listener/connect/TLS/NativeIO/send/receive APIs, "
    "eight fixed loopback TLS connections, byte-for-byte payload validation, "
    "1/2/4 final worker threads and equal total work."
)
print(
    "Each worker owns both endpoints of its assigned independent pairs. This is "
    "a CNet socket/owner attribution layer, not a CHTTP listener/admission benchmark."
)
