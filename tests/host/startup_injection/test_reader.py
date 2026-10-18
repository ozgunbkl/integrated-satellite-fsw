from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import startup_required_check as reader
from startup_required_check import ARCHIVE, HK_READY, PROFILES, StartupEvidence
from watchdog_system_check import ARM, BOOT, CORE, NOMINAL, RESOURCES, SOFTWARE


def config(reason):
    return f"WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason={reason}."


def prefix(profile, boot):
    reason = 1 if boot == 1 else 4
    return [BOOT, config(reason), ARCHIVE,
            f"TEST: STARTUP profile={profile} injection={'armed' if boot == 1 else 'suppressed'} reset_reason={reason}."]


def first(profile):
    result = prefix(profile, 1)
    target = PROFILES[profile]
    if target is None:
        result += ["TEST: STARTUP injected kind=hk_init return=false real_call=skipped.", "E (9) MAIN: Failed to initialize HK service"]
    else:
        result += [HK_READY, RESOURCES, ARM, SOFTWARE,
            f"TEST: STARTUP injected kind=task_create task={target[0]} return=-1 real_call=skipped.",
            f"E (42) MAIN: Required task creation failed: {target[0]} (stack=4096, priority={target[1]}, required_created={target[2]}/12)"]
    return result + ["abort() was called at PC 0x40000042 on core 0", "Backtrace: 0x40000042:0x3fffffff"]


def second(profile):
    result = prefix(profile, 2) + [HK_READY, RESOURCES, ARM, SOFTWARE]
    if target := PROFILES[profile]:
        result += [f"TEST: STARTUP target={target[0]} real_create_return=1."]
    return result + [CORE + " Network at startup: connected; log mirror: started."]


def feed(evidence, sequence, now=0):
    for line in sequence:
        evidence.observe(line, now)


def recovered(profile):
    evidence = StartupEvidence(profile)
    feed(evidence, first(profile), 1)
    feed(evidence, second(profile), 5)
    for now in (20, 25, 30, 35, 40, 45, 50):
        evidence.observe(NOMINAL, now)
    return evidence


class ReaderTests(unittest.TestCase):
    def test_all_three_complete_sequences(self):
        for profile in PROFILES:
            evidence = recovered(profile)
            self.assertTrue(evidence.complete(50))
            proof = evidence.snapshot(50, "PASS")
            self.assertEqual(proof["sdk_reset_reasons"], [1, 4])
            self.assertEqual(proof["recovery_real_target_created"], profile != "hk_init")

    def test_duration_profile_and_clock_validation(self):
        for value in (29, 121, True, 45.0):
            with self.assertRaises(ValueError): StartupEvidence("hk_init", value)
        with self.assertRaises(ValueError): StartupEvidence("unknown")
        for now in (-1, True, float("nan"), float("inf")):
            with self.assertRaises(RuntimeError): StartupEvidence("hk_init").observe("ignored", now)
        evidence = StartupEvidence("hk_init"); evidence.observe("ignored", 1)
        with self.assertRaises(RuntimeError): evidence.observe("ignored", 0)

    def test_no_late_attach_or_unrelated_panic_shortcut(self):
        for line in (config(4), prefix("hk_init", 2)[-1]):
            with self.assertRaises(RuntimeError): feed(StartupEvidence("hk_init"), [line])
        with self.assertRaises(RuntimeError): feed(StartupEvidence("hk_init"), prefix("hk_init", 2))
        evidence = StartupEvidence("hk_init"); feed(evidence, prefix("hk_init", 1))
        with self.assertRaises(RuntimeError): evidence.observe(BOOT, 1)

    def test_wrong_and_repeated_profile(self):
        evidence = StartupEvidence("hk_init"); feed(evidence, prefix("hk_init", 1))
        with self.assertRaises(RuntimeError): evidence.observe(prefix("hk_init", 1)[-1], 1)
        with self.assertRaises(RuntimeError): feed(StartupEvidence("hk_init"), prefix("task_late", 1))

    def test_each_missing_first_boot_marker_rejected_or_incomplete(self):
        for profile in PROFILES:
            for index in range(len(first(profile)) - 1):  # backtrace is optional, abort is not
                with self.subTest(profile=profile, index=index):
                    evidence = StartupEvidence(profile)
                    sequence = first(profile)
                    if sequence[index] == SOFTWARE:
                        continue  # A created task need not run before the later failure.
                    sequence.pop(index)
                    try:
                        feed(evidence, sequence, 1); feed(evidence, second(profile), 5)
                        for now in (20, 30, 40, 50): evidence.observe(NOMINAL, now)
                    except RuntimeError:
                        continue
                    self.assertFalse(evidence.complete(50))

    def test_missing_supervisor_first_partial_boot_is_allowed(self):
        # The earlier created task is not guaranteed to run before abort.
        evidence = StartupEvidence("task_early")
        feed(evidence, [line for line in first("task_early") if line != SOFTWARE], 1)
        feed(evidence, second("task_early"), 5)
        for now in (20, 30, 40, 50): evidence.observe(NOMINAL, now)
        self.assertTrue(evidence.complete(50))

    def test_wrong_task_stack_priority_or_created_count_rejected(self):
        for old, new in (("CMD_PROC", "TC_RX"), ("stack=4096", "stack=2048"),
                         ("priority=5", "priority=4"), ("required_created=3/12", "required_created=4/12")):
            evidence = StartupEvidence("task_early")
            sequence = first("task_early")
            sequence[-3] = sequence[-3].replace(old, new)
            with self.assertRaises(RuntimeError): feed(evidence, sequence, 1)

    def test_wrong_reset_reason_and_configuration(self):
        for old, new in (("reset_reason=4", "reset_reason=6"), ("timeout=15000", "timeout=5000"), ("idle_mask=3", "idle_mask=1")):
            evidence = StartupEvidence("hk_init"); feed(evidence, first("hk_init"), 1)
            sequence = [line.replace(old, new) for line in second("hk_init")]
            with self.assertRaises(RuntimeError): feed(evidence, sequence, 5)

    def test_false_first_boot_readiness_and_hk_continuation(self):
        for line in (HK_READY, RESOURCES, ARM, CORE):
            evidence = StartupEvidence("hk_init"); feed(evidence, prefix("hk_init", 1))
            with self.assertRaises(RuntimeError): evidence.observe(line, 1)
        evidence = StartupEvidence("task_early"); feed(evidence, first("task_early")[:-2], 1)
        with self.assertRaises(RuntimeError): evidence.observe(CORE, 1)

    def test_reinjection_extra_reboot_and_failed_recovery_create(self):
        evidence = recovered("hk_init")
        with self.assertRaises(RuntimeError): evidence.observe(BOOT, 51)
        evidence = StartupEvidence("task_early"); feed(evidence, first("task_early"), 1)
        sequence = [line.replace("real_create_return=1", "real_create_return=-1") for line in second("task_early")]
        with self.assertRaises(RuntimeError): feed(evidence, sequence, 5)
        evidence = StartupEvidence("hk_init"); feed(evidence, first("hk_init"), 1)
        with self.assertRaises(RuntimeError): feed(evidence, [line.replace("injection=suppressed", "injection=armed") for line in second("hk_init")], 5)

    def test_panic_diagnostics_are_only_allowed_after_selected_abort(self):
        for line in ("Guru Meditation Error", "assert failed: anything", "stack overflow", "Backtrace: 0x12:0x34",
                     "Brownout detector", "Task watchdog got triggered", "Interrupt wdt timeout on CPU0",
                     "TEST ERROR: anything", "FDIR: Fault from Source 2 (Severity: 2) Code: 0xE102"):
            with self.assertRaises(RuntimeError): StartupEvidence("hk_init").observe(line, 1)
        evidence = StartupEvidence("hk_init"); feed(evidence, first("hk_init"), 1)
        with self.assertRaises(RuntimeError): evidence.observe("Guru Meditation Error", 2)

    def test_abort_must_follow_exact_failure_not_merely_injection(self):
        evidence = StartupEvidence("hk_init"); feed(evidence, first("hk_init")[:-3], 1)
        with self.assertRaises(RuntimeError): evidence.observe(first("hk_init")[-2], 1)
        evidence = StartupEvidence("hk_init"); feed(evidence, first("hk_init")[:-2], 1)
        with self.assertRaises(RuntimeError): evidence.observe(first("hk_init")[-2], 12)

    def test_bounded_recovery_needs_fresh_mission_cycles(self):
        evidence = recovered("hk_init")
        self.assertFalse(evidence.complete(49))
        self.assertFalse(evidence.complete(61))
        evidence.nominal_times = [50, 50, 50]
        self.assertFalse(evidence.complete(50))

    def test_raw_text_not_retained(self):
        evidence = recovered("hk_init"); evidence.observe("private raw serial secret", 50)
        self.assertNotIn("private", json.dumps(evidence.snapshot(50, "PASS")))
        with self.assertRaises(RuntimeError): evidence.observe("x" * 1025, 50)

    def test_explicit_reset_new_output_and_valid_args_required_before_serial(self):
        fake = SimpleNamespace(Serial=lambda: self.fail("A host refusal test must never open serial"))
        with tempfile.TemporaryDirectory() as folder, patch.dict(sys.modules, {"serial": fake}), redirect_stderr(io.StringIO()):
            path = Path(folder) / "exists.json"; path.write_text("keep", encoding="utf-8")
            base = ["--profile", "hk_init", "--output", str(path)]
            for arguments in (base, [*base, "--reset"], [*base, "--reset", "--duration", "29"]):
                with self.assertRaises(SystemExit) as raised: reader.main(arguments)
                self.assertEqual(raised.exception.code, 2)
            self.assertEqual(path.read_text(encoding="utf-8"), "keep")

    def test_cli_reader_with_fake_reset_and_timeout_split_panic_line(self):
        clock = SimpleNamespace(now=0)
        data = [(1, line.encode() + b"\n") for line in first("hk_init")[:-2]]
        data += [(1, b"abort() was called at PC 0x4000"), (1.1, b"0042 on core 0\n")]
        data += [(5, line.encode() + b"\n") for line in second("hk_init")]
        data += [(now, NOMINAL.encode() + b"\n") for now in range(20, 51, 5)]
        class Port:
            def __init__(self): self.rts = self.dtr = True; self.data = iter(data); self.opened = self.closed = False
            def __enter__(self): return self
            def __exit__(self, *args): self.closed = True
            def open(self):
                assert not self.rts and not self.dtr
                self.opened = True
            def reset_input_buffer(self): pass
            def readline(self, size):
                assert size == 1026
                clock.now, raw = next(self.data); return raw
        port = Port()
        with tempfile.TemporaryDirectory() as folder, patch.dict(sys.modules, {"serial": SimpleNamespace(Serial=lambda: port)}), \
                patch.object(reader.time, "monotonic", lambda: clock.now), patch.object(reader.time, "sleep"), redirect_stdout(io.StringIO()):
            path = Path(folder) / "proof.json"
            code = reader.main(["--profile", "hk_init", "--reset", "--output", str(path)])
            proof = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual((code, proof["result"], proof["boots"]), (0, "PASS", 2))
        self.assertTrue(port.opened and port.closed)

    def test_cli_setup_failure_and_interrupt_are_redacted_failed_proofs(self):
        for error, expected, code in ((OSError("private device message"), "FAIL", 1), (KeyboardInterrupt(), "INTERRUPTED", 130)):
            def factory(): raise error
            with self.subTest(expected=expected), tempfile.TemporaryDirectory() as folder, \
                    patch.dict(sys.modules, {"serial": SimpleNamespace(Serial=factory)}), redirect_stdout(io.StringIO()) as console:
                path = Path(folder) / "proof.json"
                result = reader.main(["--profile", "hk_init", "--reset", "--output", str(path)])
                proof = json.loads(path.read_text(encoding="utf-8"))
                self.assertEqual((result, proof["result"]), (code, expected))
                self.assertNotIn("private", json.dumps(proof) + console.getvalue())
                self.assertNotIn("PASS:", console.getvalue())

    def test_cli_failure_after_abort_keeps_partial_proof(self):
        clock = SimpleNamespace(now=0)
        data = iter(first("hk_init"))
        class Port:
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def open(self): pass
            def reset_input_buffer(self): pass
            def readline(self, size):
                try: return (next(data) + "\n").encode()
                except StopIteration: raise OSError("private dropped serial detail")
        with tempfile.TemporaryDirectory() as folder, patch.dict(sys.modules, {"serial": SimpleNamespace(Serial=Port)}), \
                patch.object(reader.time, "monotonic", lambda: clock.now), patch.object(reader.time, "sleep"), redirect_stdout(io.StringIO()):
            path = Path(folder) / "proof.json"
            code = reader.main(["--profile", "hk_init", "--reset", "--output", str(path)])
            proof = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual((code, proof["result"], proof["boots"]), (1, "FAIL", 1))
        self.assertIsNotNone(proof["abort_at_s"])
        self.assertNotIn("private", json.dumps(proof))


if __name__ == "__main__":
    unittest.main()
