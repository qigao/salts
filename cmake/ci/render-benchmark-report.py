"""Render CI benchmark evidence; verdicts remain owned by the existing verifiers."""

import argparse
import csv
import io
import json
import math
import statistics
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch


BACKENDS = {
    "linux-epoll": "epoll",
    "linux-io-uring": "io_uring",
    "windows-iocp": "iocp",
    "macos-kqueue": "kqueue",
}
COLORS = {
    "PASS": "#16845b", "FAIL": "#c63845", "UNSTABLE": "#c08418",
    "DIAGNOSTIC": "#3977b8", "NO DATA": "#e0e5ec",
}
MAX_INPUT_BYTES = 2 * 1024 * 1024
MAX_ROWS = 128
IO_DRIVERS = ("libuv", "NativeIO direct", "NativeIO coroutine", "CNet")
COMPARISON_DRIVERS = ("libuv", "NativeIO direct", "CNet")
IO_PAYLOADS = {"TCP": (1024, 4096, 8192, 16384, 32768, 65536),
               "UDP": (1024, 4096, 8192)}
IO_PASSES = ("A", "diagnostic", "B")
IO_REPEATS = 5
IO_ROUND_TRIPS = 512
MAX_IO_ROWS = sum(len(p) for p in IO_PAYLOADS.values()) * len(IO_DRIVERS) * len(IO_PASSES) * IO_REPEATS
MAX_COUNTER = (1 << 64) - 1
COMPARISON_METRICS = ("p50_us", "p95_us", "rt_per_second")


def read_text(path):
    if path.is_symlink() or path.stat().st_size > MAX_INPUT_BYTES:
        raise ValueError(f"Invalid report input: {path}")
    return path.read_text(encoding="utf-8-sig")


def median_mad(values):
    median = statistics.median(values)
    return median, statistics.median(abs(value - median) for value in values)


def collect_io_comparisons(directory):
    comparisons, jobs = [], []
    required = {"backend", "protocol", "payload_bytes", "driver", "pass", "repeat",
                "round_trips", "wall_ns", "p50_ns", "p95_ns"}
    for artifact, backend in BACKENDS.items():
        path = directory / f"io-benchmark-{artifact}" / "io.runs.csv"
        if not path.is_file():
            jobs.append({"backend": backend, "status": "missing"})
            continue
        reader = csv.DictReader(io.StringIO(read_text(path)))
        fields = reader.fieldnames or []
        if not required.issubset(fields) or len(set(fields)) != len(fields):
            raise ValueError(f"Invalid I/O CSV header: {path}")
        runs = {}
        for index, row in enumerate(reader):
            if index >= MAX_IO_ROWS or None in row or any(value is None for value in row.values()):
                raise ValueError(f"Invalid I/O CSV row count/columns: {path}")
            try:
                counts = {key: int(row[key]) for key in
                          ("payload_bytes", "repeat", "round_trips", "wall_ns", "p50_ns", "p95_ns")}
            except (ValueError, TypeError) as error:
                raise ValueError(f"Invalid I/O counter: {path}") from error
            protocol, driver, phase = row["protocol"], row["driver"], row["pass"]
            if (row["backend"] != backend or protocol not in IO_PAYLOADS
                    or counts["payload_bytes"] not in IO_PAYLOADS[protocol]
                    or driver not in IO_DRIVERS or phase not in IO_PASSES
                    or not 1 <= counts["repeat"] <= IO_REPEATS
                    or counts["round_trips"] != IO_ROUND_TRIPS
                    or any(not 0 < value <= MAX_COUNTER for value in counts.values())
                    or counts["p95_ns"] < counts["p50_ns"]):
                raise ValueError(f"Invalid I/O workload/counter: {path}")
            key = (protocol, counts["payload_bytes"], driver, phase, counts["repeat"])
            if key in runs:
                raise ValueError(f"Duplicate I/O repeat: {path}")
            runs[key] = {"p50_us": counts["p50_ns"] / 1000,
                         "p95_us": counts["p95_ns"] / 1000,
                         "rt_per_second": IO_ROUND_TRIPS * 1e9 / counts["wall_ns"]}
        jobs.append({"backend": backend, "status": "complete" if len(runs) == MAX_IO_ROWS else "incomplete"})
        for protocol, payloads in IO_PAYLOADS.items():
            for payload in payloads:
                baseline = [runs.get((protocol, payload, "libuv", "A", repeat))
                            for repeat in range(1, IO_REPEATS + 1)]
                if any(row is None for row in baseline):
                    continue
                for driver in COMPARISON_DRIVERS:
                    candidate = [runs.get((protocol, payload, driver, "A", repeat))
                                 for repeat in range(1, IO_REPEATS + 1)]
                    # A partial failed run supplies no value for this cell; all
                    # plotted comparisons retain the same five-repeat contract.
                    if any(row is None for row in candidate):
                        continue
                    result = {"backend": backend, "protocol": protocol,
                              "payload_bytes": payload, "driver": driver, "repeats": IO_REPEATS}
                    control = [runs.get((protocol, payload, driver, "B", repeat))
                               for repeat in range(1, IO_REPEATS + 1)]
                    baseline_control = [runs.get((protocol, payload, "libuv", "B", repeat))
                                        for repeat in range(1, IO_REPEATS + 1)]
                    complete_control = all(row is not None for row in control + baseline_control)
                    for metric in COMPARISON_METRICS:
                        result[metric], result[f"{metric}_mad"] = median_mad(
                            [row[metric] for row in candidate])
                        delta = [100 * (cand[metric] / base[metric] - 1)
                                 for cand, base in zip(candidate, baseline)]
                        result[f"{metric}_delta_percent"], result[f"{metric}_delta_mad_pp"] = median_mad(delta)
                        control_keys = (f"{metric}_b_delta_percent", f"{metric}_b_delta_mad_pp",
                                        f"{metric}_aa_delta_percent", f"{metric}_aa_delta_mad_pp")
                        result.update(dict.fromkeys(control_keys))
                        result[f"{metric}_ab_sign_reversal"] = None
                        if complete_control:
                            b_delta = [100 * (cand[metric] / base[metric] - 1)
                                       for cand, base in zip(control, baseline_control)]
                            aa_delta = [100 * (b[metric] / a[metric] - 1)
                                        for a, b in zip(candidate, control)]
                            result[control_keys[0]], result[control_keys[1]] = median_mad(b_delta)
                            result[control_keys[2]], result[control_keys[3]] = median_mad(aa_delta)
                            result[f"{metric}_ab_sign_reversal"] = (
                                result[f"{metric}_delta_percent"] * result[control_keys[0]] < 0)
                    comparisons.append(result)
    return {"io_comparisons": comparisons, "io_comparison_jobs": jobs}


def collect(directory):
    jobs, verdicts, metrics = [], [], []
    for artifact, backend in BACKENDS.items():
        folder = directory / f"io-benchmark-{artifact}"
        verdict_file = folder / "benchmark-verdicts.csv"
        metric_file = folder / "benchmark-metrics.json"
        status = "missing"
        if metric_file.is_file():
            data = json.loads(read_text(metric_file))
            if data["schema_version"] != 1 or data["backend"] != backend:
                raise ValueError(f"Invalid metric schema/backend: {metric_file}")
            status = data["job_status"]
            if status not in {"success", "failure", "cancelled", "skipped"}:
                raise ValueError(f"Invalid job status: {status}")
            if len(data["metrics"]) > MAX_ROWS:
                raise ValueError("Too many metrics")
            for row in data["metrics"]:
                if (row["backend"] != backend or row["unit"] not in {"ratio", "percent"}
                        or row["result"] not in COLORS
                        or not all(math.isfinite(float(row[k])) for k in ("value", "mad"))
                        or float(row["mad"]) < 0):
                    raise ValueError(f"Invalid metric: {metric_file}")
                metrics.append(row)
        if verdict_file.is_file():
            rows = list(csv.DictReader(io.StringIO(read_text(verdict_file))))
            if len(rows) > MAX_ROWS:
                raise ValueError("Too many verdicts")
            seen = set()
            for row in rows:
                if (row["backend"] != backend or row["result"] not in COLORS
                        or len(row["family"]) > 80 or row["family"] in seen):
                    raise ValueError(f"Invalid verdict: {verdict_file}")
                seen.add(row["family"])
                verdicts.append(row)
        elif status == "success":
            status = "incomplete"
        jobs.append({"backend": backend, "status": status})
    if len(metrics) > MAX_ROWS or len({r["family"] for r in verdicts}) > MAX_ROWS:
        raise ValueError("Report exceeds chart row budget")
    return {"jobs": jobs, "verdicts": verdicts, "metrics": metrics,
            **collect_io_comparisons(directory)}


def finish(fig, path, title, subtitle):
    fig.suptitle(title, x=0.03, y=0.98, ha="left", fontsize=18, fontweight="bold")
    fig.text(0.03, 0.93, subtitle, fontsize=10, color="#526071")
    fig.savefig(path, dpi=150, facecolor="white", bbox_inches="tight")
    plt.close(fig)


def render_comparisons(report, output):
    plt.rcParams.update({"font.family": "DejaVu Sans", "text.parse_math": False})
    colors = {"NativeIO direct": "#2473b9", "CNet": "#d06922"}
    titles = ("P50 RTT - lower is better", "P95 RTT - lower is better",
              "Sequential RT/s - higher is better")
    status = {job["backend"]: job["status"] for job in report["io_comparison_jobs"]}
    for protocol, payloads in IO_PAYLOADS.items():
        fig, axes = plt.subplots(len(BACKENDS), len(COMPARISON_METRICS),
                                 figsize=(16, 12), sharey="col")
        for y, backend in enumerate(BACKENDS.values()):
            selected = [row for row in report["io_comparisons"]
                        if row["backend"] == backend and row["protocol"] == protocol]
            for x, metric in enumerate(COMPARISON_METRICS):
                ax = axes[y, x]
                ax.axhline(0, color="#697586", linestyle="--", linewidth=1,
                           label="libuv (0% reference)")
                for offset, driver in ((-.09, "NativeIO direct"), (.09, "CNet")):
                    rows = {row["payload_bytes"]: row for row in selected if row["driver"] == driver}
                    # NaN leaves a visible gap; missing measurements never turn
                    # into zero change or a line interpolated across absent data.
                    values = [rows[p][f"{metric}_delta_percent"] if p in rows else math.nan
                              for p in payloads]
                    errors = [rows[p][f"{metric}_delta_mad_pp"] if p in rows else math.nan
                              for p in payloads]
                    name = "CNet" if driver == "CNet" else "NativeIO"
                    ax.errorbar([i + offset for i in range(len(payloads))], values,
                                yerr=errors, color=colors[driver], fmt="o-", capsize=3,
                                linewidth=1.2, markersize=4,
                                label=f"{name} A")
                    b_values = [rows[p].get(f"{metric}_b_delta_percent") if p in rows else None
                                for p in payloads]
                    b_errors = [rows[p].get(f"{metric}_b_delta_mad_pp") if p in rows else None
                                for p in payloads]
                    ax.errorbar([i + offset for i in range(len(payloads))],
                                [v if v is not None else math.nan for v in b_values],
                                yerr=[v if v is not None else math.nan for v in b_errors],
                                color=colors[driver], fmt="s--", capsize=2,
                                linewidth=1, markersize=3, alpha=.65, label=f"{name} B control")
                if not any(row["driver"] != "libuv" for row in selected):
                    ax.text(.5, .5, "NO DATA: no complete paired samples",
                            ha="center", transform=ax.transAxes, fontsize=9)
                ax.set_xticks(range(len(payloads)), [str(p // 1024) for p in payloads])
                ax.set_xlabel("Payload (KiB)")
                ax.grid(axis="y", alpha=.2)
                ax.spines[["top", "right"]].set_visible(False)
                if x == 0:
                    label = "IOCP" if backend == "iocp" else backend
                    ax.set_ylabel(f"{label} ({status[backend]})\nPaired change (%)")
                if y == 0:
                    ax.set_title(titles[x])
        fig.suptitle(f"{protocol}: NativeIO / libuv / CNet across platforms",
                     fontsize=19, fontweight="bold", y=.98)
        fig.text(.5, .945,
                 f"{report['head'][:12]} | run {report['run']} | {report['conclusion']}\n"
                 f"DIAGNOSTIC | libuv = 0% within each pass; {IO_REPEATS} paired repeats/pass; error bars = MAD",
                 ha="center", va="top", fontsize=10)
        handles, labels = axes[0, 0].get_legend_handles_labels()
        fig.legend(handles, labels, loc="lower center", ncol=5, frameon=False,
                   bbox_to_anchor=(.5, .078))
        fig.text(.5, .060,
                 "Gray = libuv (0%); blue = NativeIO direct; orange = CNet retained/owned. Solid circles = A; dashed squares = B control.\n"
                 "Latency: negative = faster, positive = slower. RT/s: positive = faster, negative = slower.\n"
                 "Error bars = within-pass MAD, not confidence intervals; A/B disagreement indicates sensitivity to run conditions. Missing cells are gaps.\n"
                 "Different hosts: no cross-platform backend ranking. Sequential RTT, not saturation throughput.",
                 ha="center", va="top", fontsize=9, color="#526071", linespacing=1.4)
        fig.subplots_adjust(top=.885, bottom=.16, left=.10, right=.98, hspace=.48, wspace=.18)
        fig.savefig(output / f"{protocol.lower()}-comparison.png", dpi=150, facecolor="white")
        plt.close(fig)


def render(report, output):
    plt.rcParams.update({"font.family": "DejaVu Sans", "text.parse_math": False})
    subtitle = f"{report['head'][:12]}  |  run {report['run']}  |  {report['conclusion']}"
    families = sorted({row["family"] for row in report["verdicts"]})
    backends = list(BACKENDS.values())
    lookup = {(r["family"], r["backend"]): r["result"] for r in report["verdicts"]}
    fig, ax = plt.subplots(figsize=(12, max(5, 2.8 + len(families) * 0.36)))
    fig.subplots_adjust(left=0.35, right=0.97, top=0.84, bottom=0.16)
    if families:
        for y, family in enumerate(families):
            for x, backend in enumerate(backends):
                result = lookup.get((family, backend), "NO DATA")
                ax.add_patch(plt.Rectangle((x - .47, y - .43), .94, .86,
                                          color=COLORS[result]))
                ax.text(x, y, result, ha="center", va="center", fontsize=9,
                        color="#526071" if result == "NO DATA" else "white")
        ax.set(xlim=(-.5, 3.5), ylim=(len(families) - .5, -.5))
        ax.set_yticks(range(len(families)), [f.replace("_", " ") for f in families])
    else:
        ax.text(.5, .5, "No benchmark verdicts were produced", ha="center", transform=ax.transAxes)
        ax.set_yticks([])
    ax.set_xticks(range(4), [f"{j['backend']}\n{j['status']}" for j in report["jobs"]])
    ax.xaxis.tick_top()
    ax.tick_params(length=0, pad=10)
    for spine in ax.spines.values():
        spine.set_visible(False)
    fig.legend(handles=[Patch(color=c, label=s) for s, c in COLORS.items()],
               loc="lower center", ncol=5, frameon=False, bbox_to_anchor=(.5, .06))
    fig.text(.03, .02, "NO DATA = not selected, unsupported, or missing; it is not PASS. Detailed evidence remains in artifacts.",
             fontsize=9, color="#526071")
    finish(fig, output / "overview.png", "Benchmark results", subtitle)

    metrics = sorted(report["metrics"], key=lambda r: (r["family"], r["payload_bytes"], r["backend"], r["metric"]))
    groups = [(unit, [r for r in metrics if r["unit"] == unit]) for unit in ("ratio", "percent")]
    groups = [(unit, rows) for unit, rows in groups if rows]
    fig, axes = plt.subplots(max(1, len(groups)), 1,
                             figsize=(13, max(5, 3 + len(metrics) * .5)), squeeze=False,
                             gridspec_kw={"height_ratios": [max(2, len(rows)) for _, rows in groups] or [1]})
    fig.subplots_adjust(left=.40, right=.93, top=.84, bottom=.16, hspace=.45)
    if not groups:
        axes[0, 0].text(.5, .5, "No numeric performance anchors in this run", ha="center")
        axes[0, 0].axis("off")
    for ax, (unit, rows) in zip(axes[:, 0], groups):
        for y, row in enumerate(rows):
            value, mad = float(row["value"]), float(row["mad"])
            ax.errorbar(value, y, xerr=mad, fmt="o", capsize=4, color=COLORS[row["result"]])
        labels = [f"{r['backend']} | {r['family'].removeprefix('cnet_').replace('_', ' ')}\n"
                  f"{r['payload_bytes'] // 1024} KiB | {r['metric']}" for r in rows]
        ax.set_yticks(range(len(rows)), labels, fontsize=9)
        ax.invert_yaxis()
        ax.axvline(1 if unit == "ratio" else 0, color="#718096", linestyle="--", linewidth=1)
        ax.grid(axis="x", alpha=.18)
        ax.set_xlabel("Candidate / baseline (paired median ± MAD)" if unit == "ratio"
                      else "Change in % (paired median ± MAD in percentage points)")
        ax.spines[["top", "right"]].set_visible(False)
        ax.margins(y=.10)
    fig.text(.03, .04, "Ratios: higher is faster. Delta: higher throughput / lower latency is better.\n"
             "MAD shows repeat variability, not a confidence interval. Diagnostic comparisons are not merge gates.",
             fontsize=9, color="#526071")
    finish(fig, output / "performance.png", "Benchmark performance", subtitle)
    render_comparisons(report, output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--head", required=True)
    parser.add_argument("--run", required=True)
    parser.add_argument("--conclusion", required=True)
    args = parser.parse_args()
    report = collect(args.input)
    report.update(head=args.head, run=args.run, conclusion=args.conclusion)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    if report["io_comparisons"]:
        with (args.output / "io-comparisons.csv").open("w", newline="", encoding="utf-8") as file:
            writer = csv.DictWriter(file, fieldnames=list(report["io_comparisons"][0]))
            writer.writeheader()
            writer.writerows(report["io_comparisons"])
    render(report, args.output)


if __name__ == "__main__":
    main()
