from contextlib import redirect_stdout, redirect_stderr
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import resource_monitor_check as monitor
from resource_monitor_check import BUDGETS, ResourceEvidence
from watchdog_system_check import BOOT, NOMINAL


def lines(sequence=1, *, stack=1000, minimum=100, maximum=110, peak=1):
    return [f"DIAG: begin seq={sequence} uptime_ms={sequence * 30000} tick_ms=10.",
        "DIAG: heap free=200000 min=190000 largest=140000.",
        f"DIAG: queue name=TM used=0 peak={peak} capacity=10 sample_ms=100.",
        f"DIAG: queue name=TC used=0 peak={peak} capacity=5 sample_ms=100.",
        *[f"DIAG: stack name={name} free_min={stack} budget={budget}." for name, budget in BUDGETS.items()],
        *[f"DIAG: timing id={i} count={sequence * 30} min_ms={minimum} max_ms={maximum} age_ms=10." for i in range(7)],
        "DIAG: observer_ms=120.", f"DIAG: end seq={sequence}."]


def feed(evidence, block=None, when=30):
    result = None
    for line in lines() if block is None else block:
        result = evidence.observe(line, when)
    return result


class Clock:
    now = 0

    def __call__(self):
        return self.now


class Port:
    """Fake serial/time only. Tests never open the machine's real serial ports."""
    def __init__(self, clock, schedule):
        self.clock, self.schedule = clock, iter(schedule)
        self.dtr = self.rts = True
        self.opened = self.closed = self.flushed = False

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.closed = True

    def open(self):
        assert self.dtr is False and self.rts is False
        assert self.baudrate == 115200 and self.timeout == .2
        self.opened = True

    def reset_input_buffer(self):
        self.flushed = True

    def readline(self, size):
        assert size == 1026
        when, data = next(self.schedule)
        self.clock.now = when
        if isinstance(data, BaseException):
            raise data
        return data


def schedule(duration=300, *, mode=NOMINAL):
    for when in range(5, duration + 1, 5):
        yield when, (mode + "\n").encode()
        if when % 30 == 0:
            for line in lines(when // 30):
                yield when, (line + "\n").encode()


class MonitorTests(unittest.TestCase):
    def test_complete_baseline(self):
        evidence = ResourceEvidence(300)
        for sequence in range(1, 11): feed(evidence, lines(sequence), sequence * 30)
        self.assertTrue(evidence.complete(300))
        result = evidence.summary(300)
        self.assertEqual(result["reports"], 10)
        self.assertEqual(result["task_stack_min_bytes"]["FDIR"], 1000)
        self.assertEqual(result["observer_max_ms"], 120)

    def test_duration_bounds(self):
        for value in (299, 7201, 300.0, True):
            with self.assertRaises(ValueError): ResourceEvidence(value)
        for value in (300, 3600, 7200):
            self.assertEqual(ResourceEvidence(value).duration, value)

    def test_no_evidence_and_incomplete_summary(self):
        evidence = ResourceEvidence(300)
        self.assertFalse(evidence.complete(300))
        with self.assertRaises(RuntimeError): evidence.summary(300)

    def test_panic_fault_reset_and_test_image(self):
        for line in ("Guru Meditation", "abort()", "Brownout detector", "Task watchdog got triggered",
                     "FDIR: Fault from Source 3 (Severity: 2) Code: 0x0001", "TEST: injected",
                     "Telemetry Queue Full", "command queue full, dropped", "Resource diagnostics unavailable"):
            with self.subTest(line=line), self.assertRaises(RuntimeError): ResourceEvidence().observe(line, 0)

    def test_second_boot_and_boot_after_report(self):
        evidence = ResourceEvidence(); evidence.observe(BOOT, 0)
        with self.assertRaises(RuntimeError): evidence.observe(BOOT, 1)
        evidence = ResourceEvidence(); feed(evidence)
        with self.assertRaises(RuntimeError): evidence.observe(BOOT, 31)

    def test_initial_boot_allowed(self):
        evidence = ResourceEvidence(); evidence.observe(BOOT, 0); feed(evidence)
        self.assertEqual(evidence.boot_markers, 1)

    def test_warning_and_private_text_not_stored(self):
        evidence = ResourceEvidence()
        evidence.observe("private arbitrary line never retained", 0)
        evidence.observe("FDIR: Fault from Source 3 (Severity: 1) Code: 0xAF01", 1)
        self.assertEqual(evidence.archive_full, 1)
        evidence.observe("FDIR: Warning threshold handled for source 3; DEGRADED decision committed.", 2)
        self.assertEqual(evidence.degraded, 1)
        self.assertFalse(evidence.reports)

    def test_bad_clock_and_oversized_line(self):
        evidence = ResourceEvidence(); evidence.observe("ignored", 2)
        with self.assertRaises(RuntimeError): evidence.observe("ignored", 1)
        with self.assertRaises(RuntimeError): ResourceEvidence().observe("x" * 1025, 0)
        for now in (float("nan"), float("inf"), -1, True):
            with self.assertRaises(RuntimeError): ResourceEvidence().check_liveness(now)

    def test_orphan_and_interleaved_reports(self):
        evidence = ResourceEvidence(); evidence.observe(lines()[1], 0)
        self.assertIsNone(evidence.block)  # Initial mid-block attach is discarded.
        feed(evidence)
        with self.assertRaises(RuntimeError): evidence.observe(lines()[1], 31)
        evidence = ResourceEvidence(); evidence.observe(lines()[0], 0)
        with self.assertRaises(RuntimeError): evidence.observe(lines()[0], 0)

    def test_wrong_tick_sequence_and_uptime_gap(self):
        with self.assertRaises(RuntimeError): feed(ResourceEvidence(), [lines()[0].replace("tick_ms=10", "tick_ms=1")])
        evidence = ResourceEvidence(); feed(evidence)
        with self.assertRaises(RuntimeError): feed(evidence, lines(3), 60)
        evidence = ResourceEvidence(); feed(evidence)
        with self.assertRaises(RuntimeError): feed(evidence, [lines(2)[0].replace("60000", "70000")], 60)

    def test_insufficient_heap_and_largest_block(self):
        for replacement in ("free=20000", "min=10000", "largest=10000"):
            block = lines(); key = replacement.split("=")[0]
            old = {"free": "free=200000", "min": "min=190000", "largest": "largest=140000"}[key]
            block[1] = block[1].replace(old, replacement)
            with self.assertRaises(RuntimeError): feed(ResourceEvidence(), block)

    def test_low_stack_and_wrong_budget(self):
        with self.assertRaises(RuntimeError): feed(ResourceEvidence(), lines(stack=511))
        block = lines(); block[4] = block[4].replace("budget=4096", "budget=2048")
        with self.assertRaises(RuntimeError): feed(ResourceEvidence(), block)

    def test_missing_duplicate_and_unknown_task(self):
        for change in ("missing", "duplicate", "unknown"):
            block = lines()
            if change == "missing": block.pop(4)
            if change == "duplicate": block.insert(4, block[4])
            if change == "unknown": block[4] = block[4].replace("WDT_MON", "OTHER")
            with self.assertRaises(RuntimeError): feed(ResourceEvidence(), block)

    def test_unavailable_or_slow_observer(self):
        for line in ("DIAG: timing unavailable.", "DIAG: observer_ms=1001.", "DIAG: unknown."):
            evidence = ResourceEvidence(); evidence.observe(lines()[0], 0)
            with self.assertRaises(RuntimeError): evidence.observe(line, 0)

    def test_incomplete_or_wrong_end(self):
        block = lines(); block.pop(-2)
        with self.assertRaises(RuntimeError): feed(ResourceEvidence(), block)
        block = lines(); block[-1] = "DIAG: end seq=2."
        with self.assertRaises(RuntimeError): feed(ResourceEvidence(), block)

    def test_cumulative_stack_and_timing_and_queue_invariants(self):
        for block in (lines(2, stack=1001), lines(2, minimum=101), lines(2, maximum=109), lines(2, peak=0)):
            evidence = ResourceEvidence(); feed(evidence)
            with self.assertRaises(RuntimeError): feed(evidence, block, 60)

    def test_heartbeat_stall_and_invalid_interval(self):
        for old, new in (("max_ms=110", "max_ms=15001"), ("age_ms=10", "age_ms=15001"), ("count=30", "count=0")):
            block = [line.replace(old, new) for line in lines()]
            with self.assertRaises(RuntimeError): feed(ResourceEvidence(), block)

    def test_queue_capacity_and_bounds(self):
        for old, new in (("capacity=10", "capacity=11"), ("peak=1", "peak=11"), ("sample_ms=100", "sample_ms=1")):
            block = [line.replace(old, new) for line in lines()]
            with self.assertRaises(RuntimeError): feed(ResourceEvidence(), block)

    def test_early_and_stale_completion(self):
        evidence = ResourceEvidence(300)
        for sequence in range(1, 11): feed(evidence, lines(sequence), sequence * 30)
        self.assertFalse(evidence.complete(299))
        self.assertFalse(evidence.complete(340))

    def test_no_report_deadline_with_other_logs_still_arriving(self):
        evidence = ResourceEvidence(3600)
        evidence.observe("unrelated private log", 45)
        evidence.check_liveness(45)
        with self.assertRaisesRegex(RuntimeError, "45 seconds"):
            evidence.check_liveness(45.01)

    def test_report_deadline_includes_partial_blocks(self):
        evidence = ResourceEvidence(3600)
        feed(evidence)
        evidence.observe(lines(2)[0], 60)
        evidence.check_liveness(75)
        with self.assertRaises(RuntimeError): evidence.check_liveness(75.01)

    def test_buffered_reports_are_not_elapsed_time_proof(self):
        evidence = ResourceEvidence(); feed(evidence)
        with self.assertRaisesRegex(RuntimeError, "buffered burst"):
            feed(evidence, lines(2), 31)

    def test_nonprogressing_pet_count_rejected_except_saturated(self):
        evidence = ResourceEvidence(); feed(evidence)
        block = [line.replace("count=60", "count=30") for line in lines(2)]
        with self.assertRaisesRegex(RuntimeError, "not progressing"): feed(evidence, block, 60)
        evidence = ResourceEvidence()
        for sequence in (1, 2):
            feed(evidence, [line.replace(f"count={sequence * 30}", "count=4294967295") for line in lines(sequence)], sequence * 30)
        self.assertEqual(len(evidence.reports), 2)

    def test_mode_classification_is_samples_not_dwell_time(self):
        evidence = ResourceEvidence()
        evidence.observe(NOMINAL, 5)
        evidence.observe("TM GEN: Waiting for MODE_NOMINAL. Current mode: 1", 10)
        block = feed(evidence, when=20)
        self.assertEqual(block["sampled_mode"], "SAFE")
        result = evidence.snapshot(20, "RUNNING")
        self.assertEqual(result["mode_sample_counts"]["NOMINAL"], 1)
        self.assertEqual(result["mode_sample_counts"]["SAFE"], 1)
        self.assertEqual(result["mode_max_gap_s"], 5)
        self.assertIn("not continuous", result["limits"])

    def test_old_or_missing_mode_is_unknown_not_nominal(self):
        evidence = ResourceEvidence(); evidence.observe(NOMINAL, 5)
        self.assertIsNone(feed(evidence, when=30)["sampled_mode"])
        self.assertEqual(evidence.snapshot(30, "RUNNING")["reports_without_recent_mode"], 1)

    def test_required_mode_mismatch_and_invalid_selection(self):
        with self.assertRaises(ValueError): ResourceEvidence(require_mode="UNKNOWN")
        with self.assertRaisesRegex(RuntimeError, "required NOMINAL"):
            ResourceEvidence(require_mode="NOMINAL").observe("TM GEN: Waiting for MODE_NOMINAL. Current mode: 1", 5)
        with self.assertRaisesRegex(RuntimeError, "Contradictory"):
            ResourceEvidence().observe("TM GEN: Waiting for MODE_NOMINAL. Current mode: 2", 5)

    def test_required_mode_first_and_subsequent_deadlines(self):
        evidence = ResourceEvidence(require_mode="SAFE")
        evidence.check_liveness(15)
        with self.assertRaisesRegex(RuntimeError, "mode evidence"): evidence.check_liveness(15.01)
        evidence = ResourceEvidence(require_mode="SAFE")
        evidence.observe("TM GEN: Waiting for MODE_NOMINAL. Current mode: 1", 5)
        evidence.check_liveness(20)
        with self.assertRaises(RuntimeError): evidence.check_liveness(20.01)

    def test_timeout_split_lines_ansi_and_bounded_buffer(self):
        framer = monitor.SerialLines()
        self.assertEqual(list(framer.feed(b"\x1b[32mDIAG: heap free=")), [])
        self.assertEqual(list(framer.feed(b"200000 min=190000 largest=140000.\x1b[0m\r\nnext\n")),
                         ["DIAG: heap free=200000 min=190000 largest=140000.", "next"])
        self.assertEqual(framer.pending, b"")
        for raw in (b"x" * 1025, b"x" * 1025 + b"\n"):
            with self.assertRaises(RuntimeError): list(monitor.SerialLines().feed(raw))

    def test_one_hour_capture_is_simulated_not_a_target_soak(self):
        clock = Clock()
        port = Port(clock, schedule(3600))
        evidence = ResourceEvidence(3600, require_mode="NOMINAL")
        with redirect_stdout(io.StringIO()):
            result = monitor.observe_serial(port, evidence, 0, clock=clock)
        self.assertEqual((result["result"], result["reports"], result["observation_seconds"]), ("PASS", 120, 3600))
        self.assertEqual(result["mode_sample_counts"]["NOMINAL"], 720)
        self.assertEqual(result["reports_without_recent_mode"], 0)
        self.assertEqual(result["mode_max_gap_s"], 5)

    def test_capture_silence_fails_early_and_keeps_completed_reports(self):
        clock = Clock()
        port = Port(clock, [*schedule(30), (76, b"unrelated log\n")])
        evidence = ResourceEvidence(3600)
        with redirect_stdout(io.StringIO()), self.assertRaisesRegex(RuntimeError, "45 seconds"):
            monitor.observe_serial(port, evidence, 0, clock=clock)
        result = evidence.snapshot(clock(), "FAIL")
        self.assertEqual(result["reports"], 1)
        self.assertEqual(result["samples"][0]["sequence"], 1)

    def test_split_panic_is_not_lost_on_serial_timeout(self):
        clock = Clock()
        port = Port(clock, [(1, b"Guru Medi"), (1.2, b"tation\n")])
        with self.assertRaisesRegex(RuntimeError, "SDK panic"):
            monitor.observe_serial(port, ResourceEvidence(), 0, clock=clock)

    def run_main(self, folder, data, *, extra=()):
        clock = Clock()
        port = Port(clock, data)
        output = Path(folder) / "report.json"
        console = io.StringIO()
        with patch.dict(sys.modules, {"serial": SimpleNamespace(Serial=lambda: port)}), \
                patch.object(monitor.time, "monotonic", clock), redirect_stdout(console):
            code = monitor.main(["--duration", "300", "--output", str(output), *extra])
        self.assertTrue(port.opened and port.closed and port.flushed)
        return code, json.loads(output.read_text(encoding="utf-8")), console.getvalue()

    def test_main_success_and_new_output(self):
        with tempfile.TemporaryDirectory() as folder:
            code, report, console = self.run_main(folder, schedule(), extra=("--require-mode", "NOMINAL"))
        self.assertEqual(code, 0)
        self.assertEqual(report["result"], "PASS")
        self.assertEqual(report["reports"], 10)
        self.assertIn("PASS: bounded", console)

    def test_fail_and_interrupt_preserve_reports_and_redact_device_text(self):
        for failure, expected, code in ((OSError("private device text"), "FAIL", 1),
                                        (KeyboardInterrupt(), "INTERRUPTED", 130)):
            with self.subTest(expected=expected), tempfile.TemporaryDirectory() as folder:
                result, report, console = self.run_main(folder, [*schedule(30), (31, failure)])
                self.assertEqual((result, report["result"], report["reports"]), (code, expected, 1))
                self.assertNotIn("private device text", json.dumps(report) + console)
                self.assertNotIn("PASS:", console)

    def test_diagnostic_failure_preserves_only_complete_samples(self):
        with tempfile.TemporaryDirectory() as folder:
            code, report, console = self.run_main(folder, [*schedule(30), (60, (lines(2)[0] + "\n").encode()),
                (60, b"DIAG: heap free=1 min=1 largest=1.\n")])
        self.assertEqual(code, 1)
        self.assertEqual(report["reports"], 1)
        self.assertTrue(report["incomplete_block"])
        self.assertIn("heap evidence", report["reason"])

    def test_output_refusal_and_invalid_duration_never_open_serial(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / "existing.json"
            output.write_text("preserve", encoding="utf-8")
            fake = SimpleNamespace(Serial=lambda: self.fail("Serial must not be opened"))
            with patch.dict(sys.modules, {"serial": fake}), redirect_stderr(io.StringIO()):
                for args in (["--output", str(output)], ["--duration", "7201"], ["--require-mode", "invalid"]):
                    with self.assertRaises(SystemExit) as raised: monitor.main(args)
                    self.assertEqual(raised.exception.code, 2)
            self.assertEqual(output.read_text(encoding="utf-8"), "preserve")

    def test_running_checkpoint_exists_before_later_device_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "checkpoint.json"
            clock = Clock()
            with path.open("x+", encoding="utf-8") as stream:
                with redirect_stdout(io.StringIO()), self.assertRaises(OSError):
                    monitor.observe_serial(Port(clock, [*schedule(30), (31, OSError())]),
                                           ResourceEvidence(), 0, stream=stream, clock=clock)
            report = json.loads(path.read_text(encoding="utf-8"))
            self.assertEqual((report["result"], report["reports"]), ("RUNNING", 1))

    def test_checkpoint_write_failure_does_not_print_pass(self):
        real_save = monitor.write_snapshot
        def save(stream, report):
            if report["result"] == "PASS": raise OSError("private file detail")
            real_save(stream, report)
        with tempfile.TemporaryDirectory() as folder, patch.object(monitor, "write_snapshot", save):
            code, report, console = self.run_main(folder, schedule())
        self.assertEqual(code, 1)
        self.assertEqual(report["result"], "RUNNING")
        self.assertNotIn("PASS:", console)
        self.assertNotIn("private file detail", console)


if __name__ == "__main__":
    unittest.main()
