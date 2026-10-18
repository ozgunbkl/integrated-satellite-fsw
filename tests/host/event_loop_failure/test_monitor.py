"""Offline event-loop fault evidence tests; no board/SDK/network access."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from event_loop_failure_check import (BOOT, CONTINUE, CORE, ERROR, EVENT_READY, MODE,
                                      NOMINAL, PROFILE, RECEIVER, RESOURCES, STACK, TASKS,
                                      EventLoopFailureEvidence)
from netif_failure_check import PROFILE as NETIF_PROFILE
from nvs_failure_check import PROFILE as NVS_PROFILE, SKIPPED
from wifi_timeout_check import PROFILE as WIFI_PROFILE, TIMEOUT


def startup(include_core=True):
    evidence = EventLoopFailureEvidence()
    for now, line in enumerate((BOOT, RESOURCES, STACK, PROFILE, ERROR, CONTINUE)):
        assert not evidence.observe(line, now)
    for task in TASKS:
        assert not evidence.observe(task, 6)
    if include_core:
        assert not evidence.observe(CORE, 7)
    assert not evidence.observe(MODE, 12)
    return evidence


class EventLoopMonitorTests(unittest.TestCase):
    def test_valid_three_cycles_with_ready_stack_and_tc(self):
        evidence = startup()
        self.assertTrue(evidence.stack_seen)
        self.assertIn(RECEIVER, evidence.tasks)
        self.assertFalse(evidence.observe(NOMINAL, 15))
        self.assertFalse(evidence.observe(NOMINAL, 20))
        self.assertTrue(evidence.observe(NOMINAL, 25))

    def test_cycles_need_time_span(self):
        evidence = startup()
        for now in (15, 15.1, 15.2):
            self.assertFalse(evidence.observe(NOMINAL, now))

    def test_stack_required_before_profile(self):
        evidence = EventLoopFailureEvidence()
        evidence.observe(BOOT, 0)
        evidence.observe(RESOURCES, 1)
        with self.assertRaises(RuntimeError):
            evidence.observe(PROFILE, 2)

    def test_stack_order_and_duplicate_rejected(self):
        for evidence in (EventLoopFailureEvidence(), startup()):
            with self.subTest(boot=evidence.boot_seen), self.assertRaises(RuntimeError):
                evidence.observe(STACK, 30)

    def test_tc_skip_rejected(self):
        with self.assertRaises(RuntimeError):
            startup().observe(SKIPPED, 30)

    def test_wrong_profiles_ip_or_event_success_rejected(self):
        for marker in (NVS_PROFILE, NETIF_PROFILE, WIFI_PROFILE, TIMEOUT, EVENT_READY,
                       "ESP32 IP: 192.168.1.5", "MAIN: Wi-Fi connected.", "TX ok #1"):
            with self.subTest(marker=marker), self.assertRaises(RuntimeError):
                startup().observe(marker, 30)

    def test_reboot_panic_queue_and_traps_rejected(self):
        for marker in (BOOT, "Guru Meditation", "Backtrace:", "abort()", "CRITICAL FAILURE",
                       "Telemetry Queue Full", "TEST ERROR: unexpected event loop creation retry.",
                       "TEST ERROR: event loop deleted after failed creation.",
                       "TEST ERROR: Wi-Fi interface created after event loop failure.",
                       "TEST ERROR: Wi-Fi driver started after event loop failure."):
            with self.subTest(marker=marker), self.assertRaises(RuntimeError):
                startup().observe(marker, 30)

    def test_profile_error_continue_order_and_duplicates(self):
        for marker in (PROFILE, ERROR, CONTINUE, CORE):
            with self.subTest(marker=marker):
                with self.assertRaises(RuntimeError):
                    EventLoopFailureEvidence().observe(marker, 0)
                with self.assertRaises(RuntimeError):
                    startup().observe(marker, 30)

    def test_wrong_error_or_policy_rejected(self):
        for line in (ERROR.replace("ESP_ERR_NO_MEM", "ESP_FAIL"),
                     ERROR.replace("stays ready", "disabled")):
            evidence = EventLoopFailureEvidence()
            for marker in (BOOT, RESOURCES, STACK, PROFILE):
                evidence.observe(marker, 0)
            with self.subTest(line=line), self.assertRaises(RuntimeError):
                evidence.observe(line, 1)

    def test_all_tasks_and_mode_evidence_required(self):
        for task in TASKS:
            evidence = startup()
            evidence.tasks.remove(task)
            with self.subTest(task=task), self.assertRaises(RuntimeError):
                evidence.observe(NOMINAL, 30)
        evidence = startup()
        evidence.mode_seen = False
        with self.assertRaises(RuntimeError):
            evidence.observe(NOMINAL, 30)

    def test_core_network_and_log_must_be_offline(self):
        for line in (CORE.replace("unavailable", "connected"), CORE.replace("not started", "started")):
            with self.subTest(line=line), self.assertRaises(RuntimeError):
                startup(include_core=False).observe(line, 30)

    def test_empty_trace_is_not_success(self):
        self.assertFalse(EventLoopFailureEvidence().observe("unrelated text", 30))


if __name__ == "__main__":
    unittest.main()
