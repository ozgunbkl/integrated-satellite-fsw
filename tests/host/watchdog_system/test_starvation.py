import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from watchdog_system_check import (WatchdogEvidence, BOOT, RESOURCES, ARM, SOFTWARE, CORE, NOMINAL,
                                   IDLE_INJECT, INTERRUPT_INJECT, panic_diagnostic)


def begin(mode, start=0, reason=1, evidence=None, fault_core=0):
    e = evidence or WatchdogEvidence(mode, 30, fault_core=fault_core)
    for line, when in ((BOOT, start),
            (f"WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason={reason}.", start+1),
            (f"TEST: WDT profile={mode} injection={'armed' if e.boots == 0 else 'suppressed'} reset_reason={reason} core={e.fault_core}.", start+1.1),
            (RESOURCES, start+2), (ARM, start+3), (SOFTWARE, start+4), (CORE, start+5),
            ("TEST: WDT stack_free_min_bytes=1800.", start+5.1)):
        e.observe(line, when)
    return e


def inject(mode="idle_starvation", fault_core=0):
    e = begin(mode, fault_core=fault_core)
    for count in range(1, 7): e.observe(f"TEST: WDT starvation supervisor check={count} feed=ok.", 5.2+4*count)
    marker = IDLE_INJECT if mode == "idle_starvation" else INTERRUPT_INJECT
    e.observe(marker.replace("core=0", f"core={fault_core}"), 29.3)
    return e


def triggered(mode="idle_starvation", fed=True, fault_core=0):
    e = inject(mode, fault_core)
    if mode == "idle_starvation":
        if fed: e.observe("TEST: WDT starvation supervisor check=7 feed=ok.", 34.3)
        e.observe("Task watchdog got triggered", 44.3)
        e.observe(f" - IDLE{fault_core} (CPU {fault_core})", 44.31)
        e.observe("Tasks currently running:", 44.32)
        e.observe("Aborting.", 44.33)
    else:
        e.observe(f"Guru Meditation Error: Core {fault_core} panic'ed (Interrupt wdt timeout on CPU{fault_core}).", 29.6)
    return e


def recovered(mode="idle_starvation", fed=True, fault_core=0):
    e = triggered(mode, fed, fault_core)
    begin(mode, start=50, reason=6 if mode == "idle_starvation" else 5, evidence=e)
    for when in (70, 75, 80): e.observe(NOMINAL, when)
    return e


class StarvationMonitorTests(unittest.TestCase):
    def test_idle_reset_and_recovery(self):
        e = recovered(); self.assertTrue(e.complete(84)); self.assertIn("IDLE0", e.summary())

    def test_interrupt_reset_and_recovery(self):
        e = recovered("interrupt_mask"); self.assertTrue(e.complete(84)); self.assertIn("ESP_RST_INT_WDT", e.summary())

    def test_no_supervisor_feed_after_idle_injection_not_pass(self):
        self.assertFalse(recovered(fed=False).complete(84))

    def test_wrong_subscribers_are_not_idle_proof(self):
        for line in (" - WDT_MON (CPU 0/1)", " - IDLE1 (CPU 1)"):
            e = inject(); e.observe("Task watchdog got triggered", 44.3)
            with self.assertRaises(RuntimeError): e.observe(line, 44.31)

    def test_currently_running_idle_does_not_count_as_failed(self):
        e = inject(); e.observe("Task watchdog got triggered", 44.3)
        e.observe("Tasks currently running:", 44.31); e.observe("CPU 0: IDLE0", 44.32)
        with self.assertRaises(RuntimeError): e.observe("Aborting.", 44.33)

    def test_wrong_interrupt_core(self):
        e = inject("interrupt_mask")
        with self.assertRaises(RuntimeError): e.observe("Guru Meditation (Interrupt wdt timeout on CPU1)", 29.6)

    def test_wrong_watchdog_family(self):
        e = inject("interrupt_mask")
        with self.assertRaises(RuntimeError): e.observe("Task watchdog got triggered", 44.3)
        e = inject()
        with self.assertRaises(RuntimeError): e.observe("Guru Meditation (Interrupt wdt timeout on CPU0)", 29.6)

    def test_iwdt_timing_bounds(self):
        for when in (29.31, 35):
            e = inject("interrupt_mask")
            with self.assertRaises(RuntimeError): e.observe("Guru Meditation (Interrupt wdt timeout on CPU0)", when)

    def test_unrelated_panic_not_accepted(self):
        e = inject("interrupt_mask")
        with self.assertRaises(RuntimeError): e.observe("Guru Meditation (LoadProhibited)", 29.6)

    def test_wrong_reset_reason(self):
        for mode, reason in (("idle_starvation", 5), ("interrupt_mask", 6), ("interrupt_mask", 4)):
            e = triggered(mode); e.observe(BOOT, 50)
            with self.assertRaises(RuntimeError):
                e.observe(f"WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason={reason}.", 51)

    def test_second_boot_injection_must_be_suppressed(self):
        e = triggered(); e.observe(BOOT, 50)
        e.observe("WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason=6.", 51)
        with self.assertRaises(RuntimeError): e.observe("TEST: WDT profile=idle_starvation injection=armed reset_reason=6 core=0.", 51.1)

    def test_third_boot_rejected(self):
        e = recovered()
        with self.assertRaises(RuntimeError): e.observe(BOOT, 85)

    def test_reboot_without_panic_rejected(self):
        e = inject()
        with self.assertRaises(RuntimeError): e.observe(BOOT, 40)

    def test_profile_core_and_mode_must_match(self):
        for core in ("1", "2", "00", "01"):
            e = WatchdogEvidence("idle_starvation", 30); e.observe(BOOT, 0)
            e.observe("WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason=1.", 1)
            with self.assertRaises(RuntimeError): e.observe(f"TEST: WDT profile=idle_starvation injection=armed reset_reason=1 core={core}.", 2)

    def test_injection_needs_six_checks(self):
        e = begin("idle_starvation")
        with self.assertRaises(RuntimeError): e.observe(IDLE_INJECT, 29.3)

    def test_feed_sequence_cannot_skip(self):
        e = inject()
        with self.assertRaises(RuntimeError): e.observe("TEST: WDT starvation supervisor check=8 feed=ok.", 34.3)

    def test_stack_assertion_faults_always_rejected(self):
        for line in ("A stack overflow in task WDT_MON", "Stack canary watchpoint triggered", "assert failed:"):
            e = triggered("interrupt_mask")
            with self.assertRaises(RuntimeError): e.observe(line, 30)

    def test_normal_restore_rejects_fault_profiles(self):
        e = WatchdogEvidence("normal", 60); e.observe(BOOT, 0)
        e.observe("WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason=1.", 1)
        with self.assertRaises(RuntimeError): e.observe("TEST: WDT profile=idle_starvation injection=armed reset_reason=1 core=0.", 2)

    def test_private_panic_details_not_exposed(self):
        self.assertEqual(panic_diagnostic("Guru Meditation (Interrupt wdt timeout on CPU0) private"),
                         "SDK: interrupt watchdog timeout on CPU0")

    def test_duration_and_line_bounds(self):
        for duration in (True, 29, 601, 30.0):
            with self.assertRaises(ValueError): WatchdogEvidence("idle_starvation", duration)
        with self.assertRaises(RuntimeError): WatchdogEvidence("idle_starvation", 30).observe("x"*1025, 0)

    def test_cpu1_idle_reset_and_recovery(self):
        e = recovered(fault_core=1)
        self.assertTrue(e.complete(84)); self.assertIn("IDLE1", e.summary())

    def test_cpu1_interrupt_reset_and_recovery(self):
        e = recovered("interrupt_mask", fault_core=1)
        self.assertTrue(e.complete(84)); self.assertIn("CPU1 IRQ mask", e.summary())

    def test_invalid_core_selection_rejected(self):
        for core in (True, False, -1, 2, 1.0, "1", None):
            with self.assertRaises(ValueError): WatchdogEvidence("idle_starvation", 30, fault_core=core)
        for mode, duration in (("normal", 60), ("task_loss", 45), ("supervisor_stall", 30)):
            with self.assertRaises(ValueError): WatchdogEvidence(mode, duration, fault_core=1)

    def test_cpu1_rejects_wrong_idle_core_or_cpu_label(self):
        for line in (" - IDLE0 (CPU 0)", " - IDLE1 (CPU 0)", " - WDT_MON (CPU 0/1)"):
            e = inject(fault_core=1); e.observe("Task watchdog got triggered", 44.3)
            with self.assertRaises(RuntimeError): e.observe(line, 44.31)

    def test_cpu1_rejects_extra_failed_subscriber(self):
        e = inject(fault_core=1); e.observe("Task watchdog got triggered", 44.3)
        e.observe(" - IDLE1 (CPU 1)", 44.31)
        with self.assertRaises(RuntimeError): e.observe(" - IDLE0 (CPU 0)", 44.32)

    def test_cpu1_running_idle_is_not_failure_proof(self):
        e = inject(fault_core=1); e.observe("Task watchdog got triggered", 44.3)
        e.observe("Tasks currently running:", 44.31); e.observe("CPU 1: IDLE1", 44.32)
        with self.assertRaises(RuntimeError): e.observe("Aborting.", 44.33)

    def test_cpu1_rejects_cpu0_interrupt_panic(self):
        e = inject("interrupt_mask", fault_core=1)
        with self.assertRaises(RuntimeError): e.observe("Guru Meditation (Interrupt wdt timeout on CPU0)", 29.6)

    def test_cpu1_injection_marker_must_match(self):
        for mode, marker in (("idle_starvation", IDLE_INJECT), ("interrupt_mask", INTERRUPT_INJECT),
                             ("idle_starvation", "TEST: WDT idle starvation injected core=1 timeout_ms=300."),
                             ("interrupt_mask", "TEST: WDT interrupt masking injected core=2 timeout_ms=300.")):
            e = begin(mode, fault_core=1)
            for count in range(1, 7): e.observe(f"TEST: WDT starvation supervisor check={count} feed=ok.", 5.2+4*count)
            with self.assertRaises(RuntimeError): e.observe(marker, 29.3)

    def test_cpu1_recovery_cannot_change_core(self):
        e = triggered(fault_core=1); e.observe(BOOT, 50)
        e.observe("WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason=6.", 51)
        with self.assertRaises(RuntimeError): e.observe("TEST: WDT profile=idle_starvation injection=suppressed reset_reason=6 core=0.", 51.1)

    def test_cpu1_still_needs_post_injection_supervisor_feed(self):
        self.assertFalse(recovered(fed=False, fault_core=1).complete(84))

    def test_cpu0_reader_rejects_cpu1_marker(self):
        e = begin("idle_starvation")
        for count in range(1, 7): e.observe(f"TEST: WDT starvation supervisor check={count} feed=ok.", 5.2+4*count)
        with self.assertRaises(RuntimeError): e.observe(IDLE_INJECT.replace("core=0", "core=1"), 29.3)

    def test_cpu1_panic_diagnostic_remains_sanitized(self):
        self.assertEqual(panic_diagnostic("Guru Meditation (Interrupt wdt timeout on CPU1) private"),
                         "SDK: interrupt watchdog timeout on CPU1")


if __name__ == "__main__": unittest.main()
