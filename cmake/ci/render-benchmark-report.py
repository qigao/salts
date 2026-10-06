"""Render CI benchmark evidence; verdicts remain owned by the existing verifiers."""

import argparse
import csv
import io
import json
import math
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


def read_text(path):
    if path.is_symlink() or path.stat().st_size > MAX_INPUT_BYTES:
        raise ValueError(f"Invalid report input: {path}")
    return path.read_text(encoding="utf-8-sig")


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
    return {"jobs": jobs, "verdicts": verdicts, "metrics": metrics}


def finish(fig, path, title, subtitle):
    fig.suptitle(title, x=0.03, y=0.98, ha="left", fontsize=18, fontweight="bold")
    fig.text(0.03, 0.93, subtitle, fontsize=10, color="#526071")
    fig.savefig(path, dpi=150, facecolor="white", bbox_inches="tight")
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
    render(report, args.output)


if __name__ == "__main__":
    main()
