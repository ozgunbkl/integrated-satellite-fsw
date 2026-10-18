"""Offline evidence-parser checks: no serial, UDP or device writes."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from wifi_timeout_check import (BOOT, CONTINUE, CORE, MODE, NOMINAL, PROFILE,
                                RECEIVER, RESOURCES, STACK, TASKS, TIMEOUT, TimeoutEvidence)


def startup(include_core=True):
    evidence = TimeoutEvidence()
    for line, now in [(BOOT, 0), (RESOURCES, 0.1), (STACK, 0.15), (PROFILE, 0.2),
                      (TIMEOUT, 15.2), (CONTINUE, 15.3)]:
        assert not evidence.observe(line, now)
    for marker in TASKS:
        assert not evidence.observe(marker, 15.4)
    if include_core:
        assert not evidence.observe(CORE, 15.5)
    assert not evidence.observe(MODE, 27.5)
    return evidence


class MonitorTests(unittest.TestCase):
    def test_valid_three_cycles(self):
        evidence = startup()
        self.assertFalse(evidence.observe(NOMINAL, 30))
        self.assertFalse(evidence.observe(NOMINAL, 35))
        self.assertTrue(evidence.observe(NOMINAL, 40))

    def test_cycles_need_time_span(self):
        evidence = startup()
        for now in (30, 30.1, 30.2):
            self.assertFalse(evidence.observe(NOMINAL, now))

    def test_connected_board_rejected(self):
        with self.assertRaises(RuntimeError):
            startup().observe("MAIN: Wi-Fi connected. ESP32 IP: 192.168.1.5", 30)

    def test_reboot_rejected(self):
        with self.assertRaises(RuntimeError):
            startup().observe(BOOT, 30)

    def test_panic_watchdog_queue_failure_rejected(self):
        for marker in ("Guru Meditation", "Backtrace:", "abort()", "CRITICAL FAILURE",
                       "WARNING: Telemetry Queue Full, packet lost."):
            with self.subTest(marker=marker), self.assertRaises(RuntimeError):
                startup().observe(marker, 30)

    def test_profile_required_for_timeout(self):
        with self.assertRaises(RuntimeError):
            TimeoutEvidence().observe(TIMEOUT, 15)

    def test_premature_timeout_rejected(self):
        evidence = TimeoutEvidence()
        evidence.observe(BOOT, 0)
        evidence.observe(RESOURCES, 0.1)
        evidence.observe(STACK, 0.15)
        evidence.observe(PROFILE, 0.2)
        with self.assertRaises(RuntimeError):
            evidence.observe(TIMEOUT, 1)

    def test_core_status_must_be_offline(self):
        for line in (CORE.replace("unavailable", "connected"),
                     CORE.replace("not started", "started")):
            with self.subTest(line=line), self.assertRaises(RuntimeError):
                startup(include_core=False).observe(line, 30)

    def test_missing_task_rejected(self):
        evidence = startup()
        evidence.tasks.pop()
        with self.assertRaises(RuntimeError):
            evidence.observe(NOMINAL, 30)

    def test_empty_trace_is_not_success(self):
        self.assertFalse(TimeoutEvidence().observe("unrelated serial text", 30))

    def test_stack_required_before_profile(self):
        evidence = TimeoutEvidence()
        evidence.observe(BOOT, 0)
        evidence.observe(RESOURCES, 0.1)
        with self.assertRaises(RuntimeError):
            evidence.observe(PROFILE, 0.2)

    def test_stack_order_and_duplicates_rejected(self):
        for evidence in (TimeoutEvidence(), startup()):
            with self.subTest(boot=evidence.boot_seen), self.assertRaises(RuntimeError):
                evidence.observe(STACK, 30)

    def test_tc_listening_required_without_ip(self):
        evidence = startup()
        self.assertTrue(evidence.stack_seen)
        evidence.tasks.remove(RECEIVER)
        with self.assertRaises(RuntimeError):
            evidence.observe(NOMINAL, 30)


if __name__ == "__main__":
    unittest.main()
