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


if __name__ == "__main__":
    unittest.main()
