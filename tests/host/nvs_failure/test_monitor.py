"""Offline NVS-failure evidence checks: no serial, networking or storage writes."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from nvs_failure_check import (BOOT, CONTINUE, CORE, ERROR, MODE, NOMINAL, PROFILE,
                               RECEIVER, RESOURCES, SKIPPED, STACK, TASKS, TIMEOUT,
                               WIFI_PROFILE, NvsFailureEvidence)


def startup(include_core=True):
    evidence = NvsFailureEvidence()
    for now, line in enumerate((BOOT, RESOURCES, PROFILE, ERROR, CONTINUE, SKIPPED)):
        assert not evidence.observe(line, now)
    for marker in TASKS:
        assert not evidence.observe(marker, 6)
    if include_core:
        assert not evidence.observe(CORE, 7)
    assert not evidence.observe(MODE, 12)
    return evidence


class NvsMonitorTests(unittest.TestCase):
    def test_valid_three_cycles(self):
        evidence = startup()
        self.assertFalse(evidence.observe(NOMINAL, 15))
        self.assertFalse(evidence.observe(NOMINAL, 20))
        self.assertTrue(evidence.observe(NOMINAL, 25))

    def test_cycles_need_time_span(self):
        evidence = startup()
        for now in (15, 15.1, 15.2):
            self.assertFalse(evidence.observe(NOMINAL, now))

    def test_reboot_rejected(self):
        with self.assertRaises(RuntimeError):
            startup().observe(BOOT, 30)

    def test_panic_watchdog_queue_and_injection_failure_rejected(self):
        for marker in ("Guru Meditation", "Backtrace:", "abort()", "CRITICAL FAILURE",
                       "Telemetry Queue Full", "TEST ERROR: forbidden NVS partition erase attempted.",
                       "TEST ERROR: unexpected NVS init retry."):
            with self.subTest(marker=marker), self.assertRaises(RuntimeError):
                startup().observe(marker, 30)

    def test_network_and_wrong_profile_rejected(self):
        for marker in (STACK, RECEIVER, "ESP32 IP: 192.168.1.5", "MAIN: Wi-Fi connected.",
                       WIFI_PROFILE, TIMEOUT, "UDP socket ready", "TX ok #1",
                       "TEST: Network stack init failure injected (ESP_FAIL); real esp_netif_init not called.",
                       "TEST: Wi-Fi event loop failure injected (ESP_ERR_NO_MEM); real event loop creation not called."):
            with self.subTest(marker=marker), self.assertRaises(RuntimeError):
                startup().observe(marker, 30)

    def test_resources_required_before_profile(self):
        evidence = NvsFailureEvidence()
        evidence.observe(BOOT, 0)
        with self.assertRaises(RuntimeError):
            evidence.observe(PROFILE, 1)

    def test_duplicate_and_wrong_injection_rejected(self):
        for line in (PROFILE, PROFILE.replace("NO_FREE_PAGES", "NEW_VERSION_FOUND")):
            with self.subTest(line=line), self.assertRaises(RuntimeError):
                startup().observe(line, 30)

    def test_missing_policy_or_wrong_error_rejected(self):
        for line in (ERROR.replace("automatic NVS erase disabled", "NVS erase enabled"),
                     ERROR.replace("NO_FREE_PAGES", "NEW_VERSION_FOUND")):
            evidence = NvsFailureEvidence()
            for marker in (BOOT, RESOURCES, PROFILE):
                evidence.observe(marker, 0)
            with self.subTest(line=line), self.assertRaises(RuntimeError):
                evidence.observe(line, 1)

    def test_continuation_requires_error_evidence(self):
        with self.assertRaises(RuntimeError):
            NvsFailureEvidence().observe(CONTINUE, 1)

    def test_core_status_must_be_offline(self):
        for line in (CORE.replace("unavailable", "connected"),
                     CORE.replace("not started", "started")):
            with self.subTest(line=line), self.assertRaises(RuntimeError):
                startup(include_core=False).observe(line, 30)

    def test_tc_skip_required(self):
        evidence = startup()
        evidence.skipped_seen = False
        with self.assertRaises(RuntimeError):
            evidence.observe(NOMINAL, 30)

    def test_all_core_tasks_required(self):
        for task in TASKS:
            evidence = startup()
            evidence.tasks.remove(task)
            with self.subTest(task=task), self.assertRaises(RuntimeError):
                evidence.observe(NOMINAL, 30)

    def test_mode_transition_required(self):
        evidence = startup()
        evidence.mode_seen = False
        with self.assertRaises(RuntimeError):
            evidence.observe(NOMINAL, 30)

    def test_empty_trace_is_not_success(self):
        self.assertFalse(NvsFailureEvidence().observe("unrelated text", 30))


if __name__ == "__main__":
    unittest.main()
