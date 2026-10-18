import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from watchdog_system_check import (WatchdogEvidence, panic_diagnostic, BOOT, RESOURCES, ARM, SOFTWARE, CORE,
                                   NOMINAL, LOSS_PROFILE, DROP, STALL)


def begin(mode="normal", start=0, reason=1, evidence=None, core=True):
    e = evidence or WatchdogEvidence(mode, {"normal": 60, "task_loss": 60, "supervisor_stall": 30}[mode])
    e.observe(BOOT, start)
    e.observe(f"WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason={reason}.", start + 1)
    if mode == "task_loss":
        e.observe(LOSS_PROFILE, start + 1.1)
    if mode == "supervisor_stall":
        kind = "armed" if e.boots == 1 else "suppressed"
        e.observe(f"TEST: WDT profile=supervisor_stall injection={kind} reset_reason={reason}.", start + 1.1)
    e.observe(RESOURCES, start + 2)
    e.observe(ARM, start + 3)
    e.observe(SOFTWARE, start + 4)
    if core:
        e.observe(CORE + " Network at startup: unavailable; log mirror: not started.", start + 5)
    if mode != "normal":
        e.observe("TEST: WDT stack_free_min_bytes=2048.", start + 5.1)
    return e


def loss(e=None):
    e = e or begin("task_loss")
    e.observe(DROP, 6)
    e.observe("[26] FDIR: Fault from Source 4 (Severity: 2) Code: 0xC200", 26)
    e.observe("WATCHDOG: !!! CRITICAL FAILURE: Task 0 timed out !!! Cause retained; no task-triggered reset.", 26.1)
    return e


def state(e, now=27, mode=1, active=1, reports=1, uncertain=0, overflow=0):
    e.observe(f"TEST: WDT task_loss mode={mode} active={active} reports={reports} "
              f"uncertain={uncertain} overflow={overflow} feed=ok.", now)


def triggered(entries=True, panic=True):
    e = begin("supervisor_stall")
    e.observe(STALL, 29)
    e.observe("E (44000) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:", 44)
    if entries:
        e.observe("E (44000) task_wdt:  - WDT_MON (CPU 0/1)", 44.1)
    e.observe("E (44000) task_wdt: Tasks currently running:", 44.2)
    if panic:
        e.observe("E (44000) task_wdt: Aborting.", 44.3)
        e.observe("Backtrace: 0x1234:0x5678", 44.4)
    return e


class WatchdogMonitorTests(unittest.TestCase):
    def test_stack_headroom_bounds(self):
        for remaining in (0, 511, 4097):
            e = begin("task_loss")
            with self.subTest(remaining=remaining), self.assertRaises(RuntimeError):
                e.observe(f"TEST: WDT stack_free_min_bytes={remaining}.", 6)
        e = begin("task_loss")
        e.observe("TEST: WDT stack_free_min_bytes=512.", 6)
        self.assertEqual(e.stack_min, 512)

    def test_stack_in_normal_rejected(self):
        e = begin()
        with self.assertRaises(RuntimeError): e.observe("TEST: WDT stack_free_min_bytes=2048.", 6)

    def test_stack_evidence_required(self):
        e = loss()
        for t in (27, 32, 57): state(e, t)
        e.stack_seen = False
        self.assertFalse(e.complete(65))

    def test_safe_panic_diagnostic(self):
        self.assertEqual(panic_diagnostic("A stack overflow in task WDT_MON has been detected."),
                         "SDK: stack overflow detected in WDT_MON")
        self.assertEqual(panic_diagnostic("Guru Meditation Error: Core 0 panic'ed (LoadProhibited)."),
                         "SDK: panic class LoadProhibited")
        self.assertEqual(panic_diagnostic("Stack canary watchpoint triggered (WDT_MON)"),
                         "SDK: stack canary triggered in WDT_MON")
        self.assertEqual(panic_diagnostic("Backtrace: 0x40081234:0x3ffb1234 arbitrary private text"),
                         "SDK: numeric backtrace 0x40081234:0x3ffb1234")
        self.assertIsNone(panic_diagnostic("arbitrary private log"))

    def test_normal_pass(self):
        e = begin()
        for t in (20, 25, 30): e.observe(NOMINAL, t)
        self.assertFalse(e.complete(63))
        self.assertTrue(e.complete(65))

    def test_normal_requires_three_spaced_cycles(self):
        e = begin()
        for t in (20, 21, 22): e.observe(NOMINAL, t)
        self.assertFalse(e.complete(65))

    def test_no_core_no_mission(self):
        e = begin(core=False)
        with self.assertRaises(RuntimeError): e.observe(NOMINAL, 20)

    def test_short_duration_rejected(self):
        for mode, duration in (("normal", 59), ("task_loss", 44), ("supervisor_stall", 29)):
            with self.subTest(mode=mode), self.assertRaises(ValueError): WatchdogEvidence(mode, duration)

    def test_unknown_mode_rejected(self):
        with self.assertRaises(ValueError): WatchdogEvidence("other", 90)

    def test_configuration_contract(self):
        for timeout, idle, panic in ((5000, 3, 1), (15000, 1, 1), (15000, 3, 0)):
            e = WatchdogEvidence("normal", 60); e.observe(BOOT, 0)
            with self.subTest(values=(timeout, idle, panic)), self.assertRaises(RuntimeError):
                e.observe(f"WATCHDOG: SDK recovery configured timeout={timeout} idle_mask={idle} panic={panic} reset_reason=1.", 1)

    def test_malformed_configuration(self):
        e = WatchdogEvidence("normal", 60); e.observe(BOOT, 0)
        with self.assertRaises(RuntimeError): e.observe("WATCHDOG: SDK recovery configured something", 1)

    def test_resource_before_config(self):
        e = WatchdogEvidence("normal", 60); e.observe(BOOT, 0)
        with self.assertRaises(RuntimeError): e.observe(RESOURCES, 1)

    def test_repeated_configuration(self):
        e = begin()
        with self.assertRaises(RuntimeError):
            e.observe("WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason=1.", 6)

    def test_normal_reboot_rejected(self):
        e = begin()
        with self.assertRaises(RuntimeError): e.observe(BOOT, 6)

    def test_normal_faults_rejected(self):
        for line in ("[20] FDIR: Fault from Source 4 (Severity: 2) Code: 0xC200", "Guru Meditation",
                     "Backtrace: 0x1234", "abort()", "Task watchdog got triggered", "TEST ERROR: failure"):
            e = begin()
            with self.subTest(line=line), self.assertRaises(RuntimeError): e.observe(line, 6)

    def test_wrong_profile_rejected(self):
        for line in (LOSS_PROFILE, "TEST: WDT profile=supervisor_stall injection=armed reset_reason=1.",
                     "TEST: Wi-Fi lifecycle profile active"):
            e = begin()
            with self.subTest(line=line), self.assertRaises(RuntimeError): e.observe(line, 6)

    def test_optional_network_failure_is_not_fault(self):
        e = begin()
        e.observe("Wi-Fi did not connect within timeout", 6)
        for t in (20, 25, 30): e.observe(NOMINAL, t)
        self.assertTrue(e.complete(65))

    def test_task_loss_pass(self):
        e = loss()
        for t in (27, 32, 57): state(e, t)
        self.assertTrue(e.complete(65))

    def test_task_loss_needs_retention_window(self):
        e = loss()
        for t in (27, 32, 37): state(e, t)
        self.assertFalse(e.complete(65))

    def test_task_loss_missing_timeout(self):
        e = begin("task_loss"); e.observe(DROP, 6)
        self.assertFalse(e.complete(65))

    def test_wrong_task_timeout(self):
        e = loss()
        with self.assertRaises(RuntimeError): e.observe("WATCHDOG: !!! CRITICAL FAILURE: Task 1 timed out !!!", 27)

    def test_premature_task_timeout(self):
        e = begin("task_loss"); e.observe(DROP, 6)
        e.observe("[7] FDIR: Fault from Source 4 (Severity: 2) Code: 0xC200", 7)
        with self.assertRaises(RuntimeError): e.observe("WATCHDOG: !!! CRITICAL FAILURE: Task 0 timed out !!!", 8)

    def test_repeated_critical_cause(self):
        e = loss()
        with self.assertRaises(RuntimeError): e.observe("[27] FDIR: Fault from Source 4 (Severity: 2) Code: 0xC200", 27)

    def test_other_fatal_or_critical_cause(self):
        for source, severity, code in ((1, 2, "E102"), (4, 3, "C200"), (4, 2, "C20F")):
            e = begin("task_loss"); e.observe(DROP, 6)
            with self.subTest(code=code), self.assertRaises(RuntimeError):
                e.observe(f"[26] FDIR: Fault from Source {source} (Severity: {severity}) Code: 0x{code}", 26)

    def test_snapshot_contract(self):
        for kwargs in ({"mode": 2}, {"reports": 2}, {"uncertain": 1}, {"overflow": 1}):
            e = loss()
            with self.subTest(kwargs=kwargs), self.assertRaises(RuntimeError): state(e, **kwargs)

    def test_snapshot_before_cause(self):
        e = begin("task_loss")
        with self.assertRaises(RuntimeError): state(e, now=6)

    def test_repeated_drop(self):
        e = loss()
        with self.assertRaises(RuntimeError): e.observe(DROP, 27)

    def test_task_loss_reboot_rejected(self):
        e = loss()
        with self.assertRaises(RuntimeError): e.observe(BOOT, 27)

    def test_supervisor_pass(self):
        e = triggered(); begin("supervisor_stall", start=50, reason=6, evidence=e)
        for t in (70, 75, 80): e.observe(NOMINAL, t)
        self.assertFalse(e.complete(83))
        self.assertTrue(e.complete(85))
        self.assertIn("15.00", e.summary())

    def test_wrong_reset_reason(self):
        e = triggered(); e.observe(BOOT, 50)
        with self.assertRaises(RuntimeError):
            e.observe("WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason=4.", 51)

    def test_reinjection_not_suppressed(self):
        e = triggered(); e.observe(BOOT, 50)
        e.observe("WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason=6.", 51)
        with self.assertRaises(RuntimeError): e.observe("TEST: WDT profile=supervisor_stall injection=armed reset_reason=6.", 51.1)

    def test_initial_injection_not_armed(self):
        e = WatchdogEvidence("supervisor_stall", 30); e.observe(BOOT, 0)
        e.observe("WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason=6.", 1)
        with self.assertRaises(RuntimeError): e.observe("TEST: WDT profile=supervisor_stall injection=suppressed reset_reason=6.", 1.1)

    def test_premature_stall(self):
        e = begin("supervisor_stall")
        with self.assertRaises(RuntimeError): e.observe(STALL, 10)

    def test_sdk_timeout_too_early(self):
        e = begin("supervisor_stall"); e.observe(STALL, 29)
        with self.assertRaises(RuntimeError): e.observe("Task watchdog got triggered", 30)

    def test_sdk_entry_required(self):
        e = triggered(entries=False, panic=False)
        with self.assertRaises(RuntimeError): e.observe("Aborting.", 44.3)

    def test_sdk_other_subscriber_rejected(self):
        e = begin("supervisor_stall"); e.observe(STALL, 29)
        e.observe("Task watchdog got triggered", 44)
        with self.assertRaises(RuntimeError): e.observe(" - IDLE0 (CPU 0)", 44.1)

    def test_running_task_not_failure_entry(self):
        e = triggered(entries=False, panic=False)
        e.observe("CPU 0: WDT_MON", 44.3)
        with self.assertRaises(RuntimeError): e.observe("Aborting.", 44.4)

    def test_reboot_before_panic(self):
        e = triggered(panic=False)
        with self.assertRaises(RuntimeError): e.observe(BOOT, 50)

    def test_third_boot_rejected(self):
        e = triggered(); begin("supervisor_stall", start=50, reason=6, evidence=e)
        with self.assertRaises(RuntimeError): e.observe(BOOT, 60)

    def test_recovery_needs_mission_progress(self):
        e = triggered(); begin("supervisor_stall", start=50, reason=6, evidence=e)
        self.assertFalse(e.complete(85))

    def test_recovery_panic_rejected(self):
        e = triggered(); begin("supervisor_stall", start=50, reason=6, evidence=e)
        with self.assertRaises(RuntimeError): e.observe("Backtrace: 0x1234", 60)

    def test_clock_regression_rejected(self):
        e = begin()
        with self.assertRaises(RuntimeError): e.observe("", 2)

    def test_ansi_colors_accepted(self):
        e = begin()
        for t in (20, 25, 30): e.observe("\x1b[32m" + NOMINAL + "\x1b[0m", t)
        self.assertTrue(e.complete(65))


if __name__ == "__main__":
    unittest.main()
