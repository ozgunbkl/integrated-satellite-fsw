import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from watchdog_system_check import WatchdogEvidence, BOOT, RESOURCES, ARM, SOFTWARE, CORE, NOMINAL, GLOBAL_INJECT

ROWS = {"WDT_MON": " - WDT_MON (CPU 0/1)", "IDLE0": " - IDLE0 (CPU 0)", "IDLE1": " - IDLE1 (CPU 1)"}


def begin(e=None, start=0, reason=1):
    e = e or WatchdogEvidence("global_starvation", 30)
    state = "armed" if e.boots == 0 else "suppressed"
    for line, when in ((BOOT, start),
            (f"WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason={reason}.", start+1),
            (f"TEST: WDT profile=global_starvation injection={state} reset_reason={reason} cores=3.", start+1.1),
            (RESOURCES, start+2), (ARM, start+3), (SOFTWARE, start+4), (CORE, start+5),
            ("TEST: WDT stack_free_min_bytes=1800.", start+5.1)):
        e.observe(line, when)
    return e


def ready():
    e = begin()
    for count in range(1, 7): e.observe(f"TEST: WDT starvation supervisor check={count} feed=ok.", 5.2+4*count)
    return e


def injected():
    e = ready(); e.observe(GLOBAL_INJECT, 29.3); return e


def timeout():
    e = injected(); e.observe("Task watchdog got triggered", 44.3); return e


def triggered():
    e = timeout()
    for count, row in enumerate(ROWS.values()): e.observe(row, 44.31 + count*.01)
    e.observe("Tasks currently running:", 44.34)
    e.observe("CPU 0: WDT_G0", 44.35); e.observe("CPU 1: WDT_G1", 44.36)
    e.observe("Backtrace: 0x400d0000:0x3ffb0000", 44.37)
    e.observe("Aborting.", 44.38)
    return e


def recovered():
    e = begin(triggered(), start=50, reason=6)
    for when in (70, 75, 80): e.observe(NOMINAL, when)
    return e


class GlobalMonitorTests(unittest.TestCase):
    def test_full_reset_and_recovery(self):
        e = recovered(); self.assertTrue(e.complete(84)); self.assertIn("running=WDT_G0/WDT_G1", e.summary())

    def test_supervisor_subscriber_is_required(self):
        e = timeout(); e.observe(ROWS["IDLE0"], 44.31); e.observe(ROWS["IDLE1"], 44.32)
        e.observe("Tasks currently running:", 44.34)
        e.observe("CPU 0: WDT_G0", 44.35); e.observe("CPU 1: WDT_G1", 44.36)
        self.assertFalse(e.expected_panic())
        with self.assertRaises(RuntimeError): e.observe("Aborting.", 44.4)

    def test_idle_flags_may_already_be_reset_this_round(self):
        e = timeout(); e.observe(ROWS["WDT_MON"], 44.31)
        e.observe("Tasks currently running:", 44.34)
        e.observe("CPU 0: WDT_G0", 44.35); e.observe("CPU 1: WDT_G1", 44.36)
        e.observe("Backtrace:", 44.37); e.observe("Aborting.", 44.38)
        begin(e, start=50, reason=6)
        for when in (70, 75, 80): e.observe(NOMINAL, when)
        self.assertTrue(e.complete(84))

    def test_each_running_worker_is_required(self):
        for missing in (0, 1):
            e = timeout(); e.observe(ROWS["WDT_MON"], 44.31); e.observe("Tasks currently running:", 44.34)
            other = 1 - missing; e.observe(f"CPU {other}: WDT_G{other}", 44.35)
            with self.assertRaises(RuntimeError): e.observe("Backtrace:", 44.37)

    def test_wrong_running_task_or_core_rejected(self):
        for row in ("CPU 0: WDT_G1", "CPU 1: WDT_G0", "CPU 1: EPS_MGMT"):
            e = timeout(); e.observe(ROWS["WDT_MON"], 44.31); e.observe("Tasks currently running:", 44.34)
            with self.assertRaises(RuntimeError): e.observe(row, 44.35)

    def test_worker_rows_before_running_section_do_not_count(self):
        e = timeout(); e.observe(ROWS["WDT_MON"], 44.31); e.observe("CPU 0: WDT_G0", 44.32)
        e.observe("Tasks currently running:", 44.34); e.observe("CPU 1: WDT_G1", 44.35)
        with self.assertRaises(RuntimeError): e.observe("Backtrace:", 44.37)

    def test_duplicate_running_core_rejected(self):
        e = timeout(); e.observe(ROWS["WDT_MON"], 44.31); e.observe("Tasks currently running:", 44.34)
        e.observe("CPU 0: WDT_G0", 44.35)
        with self.assertRaises(RuntimeError): e.observe("CPU 0: WDT_G0", 44.36)

    def test_extra_failed_subscriber_rejected(self):
        e = timeout()
        with self.assertRaises(RuntimeError): e.observe(" - TCPIP (CPU 0/1)", 44.31)

    def test_duplicate_failed_subscriber_rejected(self):
        e = timeout(); e.observe(ROWS["IDLE0"], 44.31)
        with self.assertRaises(RuntimeError): e.observe(ROWS["IDLE0"], 44.32)

    def test_wrong_idle_affinity_rejected(self):
        for row in (" - IDLE1 (CPU 0)", " - IDLE0 (CPU 1)", " - IDLE0 (CPU 0/1)"):
            e = timeout()
            with self.assertRaises(RuntimeError): e.observe(row, 44.31)

    def test_running_tasks_are_not_failed_subscribers(self):
        e = timeout(); e.observe("Tasks currently running:", 44.31)
        e.observe("CPU 0: WDT_G0", 44.32); e.observe("CPU 1: WDT_G1", 44.33)
        with self.assertRaises(RuntimeError): e.observe("Backtrace:", 44.34)

    def test_premature_sdk_timeout_rejected(self):
        e = injected()
        with self.assertRaises(RuntimeError): e.observe("Task watchdog got triggered", 35)

    def test_release_requires_six_checks_and_complete_gate(self):
        e = begin()
        with self.assertRaises(RuntimeError): e.observe(GLOBAL_INJECT, 29.3)
        for marker in (GLOBAL_INJECT.replace("ready=3", "ready=1"), GLOBAL_INJECT.replace("cores=3", "cores=1"),
                       GLOBAL_INJECT.replace("priority=24", "priority=1"), GLOBAL_INJECT.replace("interrupts=enabled", "interrupts=disabled")):
            e = ready()
            with self.assertRaises(RuntimeError): e.observe(marker, 29.3)

    def test_supervisor_cannot_feed_after_injection(self):
        e = injected()
        with self.assertRaises(RuntimeError): e.observe("TEST: WDT starvation supervisor check=7 feed=ok.", 34.3)

    def test_interrupt_watchdog_is_wrong_family(self):
        e = injected()
        with self.assertRaises(RuntimeError): e.observe("Guru Meditation (Interrupt wdt timeout on CPU0)", 29.6)

    def test_wrong_recovery_reset_reason(self):
        e = triggered(); e.observe(BOOT, 50)
        with self.assertRaises(RuntimeError): e.observe("WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason=5.", 51)

    def test_second_boot_injection_must_be_suppressed(self):
        e = triggered(); e.observe(BOOT, 50)
        e.observe("WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason=6.", 51)
        with self.assertRaises(RuntimeError): e.observe("TEST: WDT profile=global_starvation injection=armed reset_reason=6 cores=3.", 51.1)

    def test_third_boot_rejected(self):
        e = recovered()
        with self.assertRaises(RuntimeError): e.observe(BOOT, 85)

    def test_normal_restore_rejects_global_profile(self):
        e = WatchdogEvidence("normal", 60); e.observe(BOOT, 0)
        e.observe("WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason=1.", 1)
        with self.assertRaises(RuntimeError): e.observe("TEST: WDT profile=global_starvation injection=armed reset_reason=1 cores=3.", 1.1)

    def test_global_profile_has_no_single_core_selector(self):
        with self.assertRaises(ValueError): WatchdogEvidence("global_starvation", 30, fault_core=1)

    def test_stack_and_assertion_failures_are_not_expected(self):
        for line in ("A stack overflow in task WDT_MON", "Stack canary watchpoint triggered", "assert failed:"):
            e = triggered()
            with self.assertRaises(RuntimeError): e.observe(line, 44.4)

    def test_reboot_without_complete_panic_proof_rejected(self):
        e = injected()
        with self.assertRaises(RuntimeError): e.observe(BOOT, 50)


if __name__ == "__main__": unittest.main()
