import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


spec = importlib.util.spec_from_file_location(
    "benchmark_report", Path(__file__).parents[1] / "render-benchmark-report.py")
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class BenchmarkReportTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.folder = self.root / "io-benchmark-linux-epoll"
        self.folder.mkdir()

    def write_metrics(self, status="success", metrics=None):
        (self.folder / "benchmark-metrics.json").write_text(json.dumps({
            "schema_version": 1, "backend": "epoll", "job_status": status,
            "metrics": metrics or [],
        }), encoding="utf-8")

    def test_missing_artifacts_are_not_passes(self):
        result = report.collect(self.root)
        self.assertEqual({j["status"] for j in result["jobs"]}, {"missing"})
        self.assertEqual(result["verdicts"], [])

    def test_success_without_verdict_file_is_incomplete(self):
        self.write_metrics()
        self.assertEqual(report.collect(self.root)["jobs"][0]["status"], "incomplete")

    def test_cancelled_job_keeps_status_and_available_diagnostic_evidence(self):
        self.write_metrics("cancelled")
        with (self.folder / "benchmark-verdicts.csv").open("w", newline="", encoding="utf-8") as file:
            writer = csv.DictWriter(file, fieldnames=["backend", "family", "result"])
            writer.writeheader()
            writer.writerow({"backend": "epoll", "family": "cnet_owner_parallel", "result": "UNSTABLE"})
        result = report.collect(self.root)
        self.assertEqual(result["jobs"][0]["status"], "cancelled")
        self.assertEqual(result["verdicts"][0]["result"], "UNSTABLE")

    def test_nonfinite_metric_is_rejected(self):
        self.write_metrics(metrics=[{"backend": "epoll", "unit": "ratio", "result": "DIAGNOSTIC",
                                     "value": float("nan"), "mad": 0}])
        with self.assertRaisesRegex(ValueError, "Invalid metric"):
            report.collect(self.root)


class IOComparisonTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.folder = self.root / "io-benchmark-linux-epoll"
        self.folder.mkdir()

    def runs(self):
        baseline = (1000, 100000, 10000, 1000000, 10000000)
        native = (10000, 200000, 30000, 4000000, 50000000)
        rows = []
        for protocol, payloads in report.IO_PAYLOADS.items():
            for payload in payloads:
                for driver in report.IO_DRIVERS:
                    for phase in report.IO_PASSES:
                        for repeat in range(1, report.IO_REPEATS + 1):
                            latency = (native if driver == "NativeIO direct" else baseline)[repeat - 1]
                            if phase != "A":
                                latency *= 10
                            rows.append({"backend": "epoll", "protocol": protocol,
                                         "payload_bytes": payload, "driver": driver,
                                         "pass": phase, "repeat": repeat, "round_trips": 512,
                                         "wall_ns": latency * 512, "p50_ns": latency,
                                         "p95_ns": latency * 2})
        return rows

    def write_runs(self, rows):
        with (self.folder / "io.runs.csv").open("w", newline="", encoding="utf-8") as file:
            writer = csv.DictWriter(file, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)

    def test_comparison_uses_paired_medians_and_only_uninstrumented_a_runs(self):
        self.write_runs(self.runs())
        result = report.collect_io_comparisons(self.root)
        self.assertEqual(result["io_comparison_jobs"][0]["status"], "complete")
        self.assertEqual(len(result["io_comparisons"]), 27)
        row = next(r for r in result["io_comparisons"] if r["protocol"] == "TCP"
                   and r["payload_bytes"] == 1024 and r["driver"] == "NativeIO direct")
        self.assertEqual(row["p50_us"], 200)
        # Median of paired ratios is 4, while ratio of medians is 2.
        self.assertEqual(row["p50_us_delta_percent"], 300)
        self.assertEqual(row["p50_us_delta_mad_pp"], 100)
        self.assertEqual(row["rt_per_second_delta_percent"], -75)
        self.assertAlmostEqual(row["rt_per_second_delta_mad_pp"], 100 / 12)

    def test_partial_run_omits_incomplete_pairs_without_inventing_zero_change(self):
        rows = [r for r in self.runs() if not (r["protocol"] == "TCP"
                and r["payload_bytes"] == 1024 and r["driver"] == "NativeIO direct"
                and r["pass"] == "A" and r["repeat"] == 5)]
        self.write_runs(rows)
        result = report.collect_io_comparisons(self.root)
        self.assertEqual(result["io_comparison_jobs"][0]["status"], "incomplete")
        self.assertEqual(len(result["io_comparisons"]), 26)
        self.assertFalse(any(r["protocol"] == "TCP" and r["payload_bytes"] == 1024
                             and r["driver"] == "NativeIO direct" for r in result["io_comparisons"]))

    def test_control_reveals_sign_reversal_even_when_each_pass_has_zero_mad(self):
        rows = self.runs()
        for row in rows:
            if row["protocol"] != "TCP" or row["payload_bytes"] != 65536:
                continue
            latency = 100000
            if row["driver"] == "NativeIO direct":
                latency = 150000 if row["pass"] == "A" else 80000
            row.update(p50_ns=latency, p95_ns=latency * 2, wall_ns=latency * 512)
        self.write_runs(rows)
        result = report.collect_io_comparisons(self.root)
        row = next(r for r in result["io_comparisons"] if r["protocol"] == "TCP"
                   and r["payload_bytes"] == 65536 and r["driver"] == "NativeIO direct")
        self.assertEqual(row["p50_us_delta_percent"], 50)
        self.assertAlmostEqual(row["p50_us_b_delta_percent"], -20)
        self.assertEqual(row["p50_us_delta_mad_pp"], 0)
        self.assertEqual(row["p50_us_b_delta_mad_pp"], 0)
        self.assertAlmostEqual(row["p50_us_aa_delta_percent"], -100 * 7 / 15)
        self.assertTrue(row["p50_us_ab_sign_reversal"])

    def test_missing_control_is_a_gap_and_never_a_stability_claim(self):
        self.write_runs([r for r in self.runs() if not (r["driver"] == "NativeIO direct"
                        and r["pass"] == "B" and r["repeat"] == 5)])
        result = report.collect_io_comparisons(self.root)
        row = next(r for r in result["io_comparisons"] if r["driver"] == "NativeIO direct")
        self.assertIsNone(row["p50_us_b_delta_percent"])
        self.assertIsNone(row["p50_us_aa_delta_percent"])
        self.assertIsNone(row["p50_us_ab_sign_reversal"])

    def test_missing_platforms_retain_missing_status(self):
        result = report.collect_io_comparisons(self.root)
        self.assertEqual(result["io_comparisons"], [])
        self.assertEqual([j["status"] for j in result["io_comparison_jobs"]], ["missing"] * 4)

    def test_duplicate_repeat_is_rejected(self):
        rows = self.runs()
        rows[1] = rows[0].copy()
        self.write_runs(rows)
        with self.assertRaisesRegex(ValueError, "Duplicate I/O repeat"):
            report.collect_io_comparisons(self.root)

    def test_wrong_backend_and_invalid_counters_are_rejected(self):
        for field, value in (("backend", "iocp"), ("wall_ns", 0),
                             ("p50_ns", "nan"), ("p95_ns", 1),
                             ("wall_ns", 1 << 64)):
            with self.subTest(field=field, value=value):
                rows = self.runs()
                rows[0][field] = value
                self.write_runs(rows)
                with self.assertRaisesRegex(ValueError, "Invalid I/O"):
                    report.collect_io_comparisons(self.root)

    def test_renderer_produces_both_comparison_images_with_missing_platforms(self):
        self.write_runs(self.runs())
        result = report.collect_io_comparisons(self.root)
        result.update(head="abc", run="123", conclusion="failure")
        output = self.root / "output"
        output.mkdir()
        report.render_comparisons(result, output)
        for protocol in ("tcp", "udp"):
            content = (output / f"{protocol}-comparison.png").read_bytes()
            self.assertEqual(content[:8], b"\x89PNG\r\n\x1a\n")
            self.assertGreater(len(content), 1000)


if __name__ == "__main__":
    unittest.main()
