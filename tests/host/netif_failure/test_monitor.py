"""Offline network-stack fault evidence checks; no board or SDK operations."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from netif_failure_check import (BOOT, CONTINUE, CORE, ERROR, MODE, NOMINAL, PROFILE,
                                 RESOURCES, SKIPPED, TASKS, NetifFailureEvidence)
from nvs_failure_check import PROFILE as NVS_PROFILE
from wifi_timeout_check import PROFILE as WIFI_PROFILE, RECEIVER, STACK, TIMEOUT


def startup():
    evidence = NetifFailureEvidence()
    for now, line in enumerate((BOOT, RESOURCES, PROFILE, ERROR, CONTINUE, SKIPPED)):
        assert not evidence.observe(line, now)
    for task in TASKS:
        assert not evidence.observe(task, 6)
    assert not evidence.observe(CORE, 7)
    assert not evidence.observe(MODE, 12)
    return evidence


class NetifMonitorTests(unittest.TestCase):
    def test_valid_three_cycles(self):
        evidence = startup()
        self.assertFalse(evidence.observe(NOMINAL, 15))
        self.assertFalse(evidence.observe(NOMINAL, 20))
        self.assertTrue(evidence.observe(NOMINAL, 25))

    def test_cycles_need_time_span(self):
        evidence = startup()
        for now in (15, 15.1, 15.2):
            self.assertFalse(evidence.observe(NOMINAL, now))

    def test_wrong_profiles_and_network_rejected(self):
        for marker in (NVS_PROFILE, WIFI_PROFILE, STACK, RECEIVER, TIMEOUT,
                       "ESP32 IP: 192.168.1.5", "UDP socket ready", "TX ok #1",
                       "TEST: Wi-Fi event loop failure injected (ESP_ERR_NO_MEM); real event loop creation not called."):
            with self.subTest(marker=marker), self.assertRaises(RuntimeError):
                startup().observe(marker, 30)

    def test_reboot_and_faults_rejected(self):
        for marker in (BOOT, "Guru Meditation", "Backtrace:", "abort()", "CRITICAL FAILURE",
                       "Telemetry Queue Full", "TEST ERROR: unexpected network stack init retry.",
                       "TEST ERROR: unsupported network stack deinit attempted.",
                       "TEST ERROR: readiness published after network stack init failure.",
                       "TEST ERROR: event loop started after network stack init failure."):
            with self.subTest(marker=marker), self.assertRaises(RuntimeError):
                startup().observe(marker, 30)

    def test_profile_and_error_order_required(self):
        for marker in (PROFILE, ERROR, CONTINUE, SKIPPED, CORE):
            with self.subTest(marker=marker), self.assertRaises(RuntimeError):
                NetifFailureEvidence().observe(marker, 0)

    def test_wrong_error_policy_rejected(self):
        evidence = NetifFailureEvidence()
        for marker in (BOOT, RESOURCES, PROFILE):
            evidence.observe(marker, 0)
        with self.assertRaises(RuntimeError):
            evidence.observe(ERROR.replace("stays disabled", "enabled"), 1)

    def test_duplicate_evidence_rejected(self):
        for marker in (RESOURCES, PROFILE, ERROR, CONTINUE, SKIPPED, CORE):
            with self.subTest(marker=marker), self.assertRaises(RuntimeError):
                startup().observe(marker, 30)

    def test_all_core_evidence_required(self):
        for field in ("core_seen", "mode_seen", "skipped_seen"):
            evidence = startup()
            setattr(evidence, field, False)
            with self.subTest(field=field), self.assertRaises(RuntimeError):
                evidence.observe(NOMINAL, 30)
        for task in TASKS:
            evidence = startup()
            evidence.tasks.remove(task)
            with self.subTest(task=task), self.assertRaises(RuntimeError):
                evidence.observe(NOMINAL, 30)

    def test_core_network_and_log_must_be_offline(self):
        for line in (CORE.replace("unavailable", "connected"), CORE.replace("not started", "started")):
            with self.subTest(line=line), self.assertRaises(RuntimeError):
                startup().observe(line, 30)

    def test_empty_trace_is_not_success(self):
        self.assertFalse(NetifFailureEvidence().observe("unrelated text", 30))


if __name__ == "__main__":
    unittest.main()
