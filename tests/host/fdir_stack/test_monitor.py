import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from fdir_stack_check import FDIRStackEvidence, HANDLED
from watchdog_system_check import BOOT, CORE, RESOURCES, ARM, SOFTWARE, NOMINAL


def begin():
    e = FDIRStackEvidence()
    for now, line in ((0, BOOT), (1, "WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason=1."),
                      (2, RESOURCES), (3, ARM), (4, SOFTWARE), (5, CORE),
                      (10, NOMINAL), (15, NOMINAL), (20, NOMINAL),
                      (30, "FDIR: stack_free_min_bytes=2000.")):
        e.observe(line, now)
    return e


def full(e):
    for now in (170, 175, 180):
        e.observe(f"[{now}] FDIR: Fault from Source 3 (Severity: 1) Code: 0xAF01", now)
    e.observe(HANDLED, 181)
    return e


class MonitorTests(unittest.TestCase):
    def test_success(self):
        e = full(begin())
        e.observe("FDIR: stack_free_min_bytes=1600.", 210)
        e.observe("FDIR: stack_free_min_bytes=1600.", 300)
        self.assertTrue(e.complete(304))
        self.assertEqual(e.stack_min, 1600)

    def test_duration_bounds(self):
        for duration in (299, 601, 300.0, True):
            with self.subTest(duration=duration), self.assertRaises(ValueError):
                FDIRStackEvidence(duration)

    def test_missing_batch(self):
        e = begin()
        e.observe("FDIR: stack_free_min_bytes=1600.", 300)
        self.assertFalse(e.complete(304))

    def test_batch_without_warning_evidence(self):
        with self.assertRaises(RuntimeError):
            begin().observe(HANDLED, 181)

    def test_stack_bounds_and_syntax(self):
        for report in ("511", "4097", "-1", "1600x"):
            with self.subTest(report=report), self.assertRaises(RuntimeError):
                begin().observe(f"FDIR: stack_free_min_bytes={report}.", 40)

    def test_stack_cannot_increase(self):
        with self.assertRaises(RuntimeError):
            begin().observe("FDIR: stack_free_min_bytes=2001.", 40)

    def test_stack_before_startup(self):
        with self.assertRaises(RuntimeError):
            FDIRStackEvidence().observe("FDIR: stack_free_min_bytes=1600.", 0)

    def test_stack_report_must_follow_batch_and_be_fresh(self):
        e = full(begin())
        self.assertFalse(e.complete(304))
        e.observe("FDIR: stack_free_min_bytes=1600.", 210)
        self.assertFalse(e.complete(304))

    def test_minimum_observation(self):
        e = full(begin())
        e.observe("FDIR: stack_free_min_bytes=1600.", 270)
        self.assertFalse(e.complete(300))

    def test_late_batch_needs_followup(self):
        e = begin()
        for now in (270, 275, 280):
            e.observe(f"[{now}] FDIR: Fault from Source 3 (Severity: 1) Code: 0xAF01", now)
        e.observe(HANDLED, 281)
        e.observe("FDIR: stack_free_min_bytes=1600.", 300)
        self.assertFalse(e.complete(304))

    def test_panic_reboot_and_test_profiles_rejected(self):
        for line in ("***ERROR*** A stack overflow has been detected in task FDIR.",
                     "Guru Meditation Error: LoadProhibited", "Backtrace: 0x40000000:0x3ff00000", BOOT,
                     "TEST: unexpected injection"):
            with self.subTest(line=line), self.assertRaises(RuntimeError):
                begin().observe(line, 40)

    def test_critical_fault_rejected(self):
        with self.assertRaises(RuntimeError):
            begin().observe("[40] FDIR: Fault from Source 4 (Severity: 2) Code: 0xC200", 40)


if __name__ == "__main__":
    unittest.main()
