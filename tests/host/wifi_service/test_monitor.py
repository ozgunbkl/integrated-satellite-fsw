import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from wifi_timeout_check import BOOT, NOMINAL, TASKS
from wifi_system_check import WifiEvidence, ALLOC, LIFECYCLE, OFFLINE, RESTORED, LOG_READY


def prefix(mode):
    profile = ALLOC if mode == "interface" else LIFECYCLE
    return [(BOOT, 0), ("MAIN: Required queues and EPS mutex ready before Wi-Fi/log initialization.", 1),
            ("MAIN: Network stack initialized; socket access enabled.", 2),
            ("MAIN: Wi-Fi event loop created before interface/driver initialization.", 3),
            (profile, 4)]


def boot(mode):
    evidence = WifiEvidence(mode, 300)
    for line, now in prefix(mode):
        evidence.observe(line, now)
    if mode == "interface":
        evidence.observe("Wi-Fi startup failed at station interface (ESP_FAIL); core startup continues.", 5)
    else:
        evidence.observe("Wi-Fi did not connect within timeout", 15)
    for marker in TASKS:
        evidence.observe(marker, 16)
    evidence.observe(OFFLINE, 17)
    return evidence


class WifiMonitorTests(unittest.TestCase):
    def test_interface_core_continues(self):
        evidence = boot("interface")
        self.assertFalse(evidence.observe(NOMINAL, 20))
        self.assertFalse(evidence.observe(NOMINAL, 25))
        self.assertTrue(evidence.observe(NOMINAL, 30))

    def test_interface_cannot_get_ip(self):
        with self.assertRaises(RuntimeError):
            boot("interface").observe("MAIN: Wi-Fi connected. ESP32 IP: 192.168.1.5", 19)

    def test_interface_cannot_start_manager(self):
        with self.assertRaises(RuntimeError):
            boot("interface").observe("WIFI: reconnect manager running", 19)

    def test_reboot_rejected(self):
        with self.assertRaises(RuntimeError):
            boot("interface").observe(BOOT, 25)

    def test_task_evidence_required(self):
        evidence = boot("interface")
        evidence.tasks.pop()
        with self.assertRaises(RuntimeError):
            evidence.observe(NOMINAL, 20)

    def test_wrong_profile(self):
        evidence = WifiEvidence("interface")
        for line, now in prefix("lifecycle")[:-1]:
            evidence.observe(line, now)
        with self.assertRaises(RuntimeError):
            evidence.observe(LIFECYCLE, 5)

    def test_timeout_must_be_real(self):
        evidence = WifiEvidence("lifecycle")
        for line, now in prefix("lifecycle"):
            evidence.observe(line, now)
        with self.assertRaises(RuntimeError):
            evidence.observe("Wi-Fi did not connect within timeout", 8)

    def test_late_ip_requires_restore(self):
        with self.assertRaises(RuntimeError):
            boot("lifecycle").observe("MAIN: Wi-Fi connected. ESP32 IP: 192.168.1.5", 23)

    def test_bad_disconnect_order(self):
        evidence = boot("lifecycle")
        evidence.observe(RESTORED, 23)
        evidence.observe("MAIN: Wi-Fi connected. ESP32 IP: 192.168.1.5", 24)
        evidence.observe(LOG_READY, 25)
        with self.assertRaises(RuntimeError):
            evidence.observe("TEST: real station disconnect 2/3 requested.", 90)

    def test_complete_lifecycle(self):
        evidence = boot("lifecycle")
        evidence.observe(RESTORED, 23)
        evidence.observe("MAIN: Wi-Fi connected. ESP32 IP: 192.168.1.5", 24)
        evidence.observe(LOG_READY, 25)
        for index in range(1, 4):
            evidence.observe(f"TEST: real station disconnect {index}/3 requested.", 60 * index)
            evidence.observe("WIFI: link unavailable; transmissions paused.", 60 * index + 1)
            evidence.observe("MAIN: Wi-Fi connected. ESP32 IP: 192.168.1.5", 60 * index + 4)
        for now in range(20, 296, 5):
            evidence.observe(NOMINAL, now)
        evidence.observe("WIFI: health connected=1 attempts=5 heap=100000 min_heap=90000 stack_free_bytes=2048", 260)
        evidence.observe("WIFI: health connected=1 attempts=5 heap=99900 min_heap=90000 stack_free_bytes=2048", 290)
        self.assertTrue(evidence.observe(NOMINAL, 300))

    def test_incomplete_soak_rejected(self):
        with self.assertRaises(RuntimeError):
            boot("lifecycle").observe("", 300)

    def test_invalid_health_rejected(self):
        with self.assertRaises(RuntimeError):
            boot("lifecycle").observe("WIFI: health connected=1 attempts=5 heap=100000 min_heap=90000 stack_free_bytes=0", 260)

    def test_duplicate_log_start_rejected(self):
        evidence = boot("lifecycle")
        evidence.observe(RESTORED, 23)
        evidence.observe("MAIN: Wi-Fi connected. ESP32 IP: 192.168.1.5", 24)
        evidence.observe(LOG_READY, 25)
        with self.assertRaises(RuntimeError):
            evidence.observe(LOG_READY, 27)

    def test_panic_rejected(self):
        for marker in ("Guru Meditation", "TEST ERROR:", "task_wdt:", "Telemetry Queue Full"):
            with self.assertRaises(RuntimeError):
                boot("lifecycle").observe(marker, 28)

    def test_critical_mode_is_live_not_network_failure(self):
        evidence = boot("lifecycle")
        evidence.observe("TM GEN: Waiting for MODE_NOMINAL. Current mode: 5", 120)
        self.assertEqual(evidence.core_cycles, [120])
        self.assertEqual(evidence.modes, {5})
        self.assertFalse(evidence.nominals)

    def test_invalid_or_contradictory_mode_rejected(self):
        for mode in (2, 6, 99):
            with self.assertRaises(RuntimeError):
                boot("lifecycle").observe(f"TM GEN: Waiting for MODE_NOMINAL. Current mode: {mode}", 320)

    def test_long_soak_accepts_nominal_to_safe(self):
        evidence = boot("lifecycle")
        evidence.observe(RESTORED, 23)
        evidence.observe("MAIN: Wi-Fi connected. ESP32 IP: 192.168.1.5", 24)
        evidence.observe(LOG_READY, 25)
        for index in range(1, 4):
            evidence.observe(f"TEST: real station disconnect {index}/3 requested.", 60 * index)
            evidence.observe("WIFI: link unavailable; transmissions paused.", 60 * index + 1)
            evidence.observe("MAIN: Wi-Fi connected. ESP32 IP: 192.168.1.5", 60 * index + 4)
        for now in range(20, 151, 5):
            evidence.observe(NOMINAL, now)
        for now in range(155, 296, 5):
            evidence.observe("TM GEN: Waiting for MODE_NOMINAL. Current mode: 1", now)
        evidence.observe("WIFI: health connected=1 attempts=5 heap=100000 min_heap=90000 stack_free_bytes=2048", 260)
        evidence.observe("WIFI: health connected=1 attempts=5 heap=99900 min_heap=90000 stack_free_bytes=2048", 290)
        self.assertTrue(evidence.observe("TM GEN: Waiting for MODE_NOMINAL. Current mode: 1", 300))

    def test_core_heartbeat_gap_rejected(self):
        evidence = boot("lifecycle")
        evidence.observe(NOMINAL, 20)
        with self.assertRaises(RuntimeError):
            evidence.observe("TM GEN: Waiting for MODE_NOMINAL. Current mode: 5", 40)


if __name__ == "__main__":
    unittest.main()
