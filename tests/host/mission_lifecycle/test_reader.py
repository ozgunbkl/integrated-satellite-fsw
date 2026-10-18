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
import mission_lifecycle_check as reader
from watchdog_system_check import BOOT, SOFTWARE, CORE
from watchdog_recovery_check import ACK, NACK, CRC, ROUTE


def state(stage, mode, deployment, eps, adcs, payload, size, faults=0, revision=0, pdu=7, archive=272):
    return (f"TEST: LIFECYCLE state={stage} mode={mode} deployment={deployment} eps={eps} adcs={adcs} "
            f"payload={payload} bytes={size} faults={faults} revision={revision} pdu={pdu} archive={archive}.")


def command(number):
    return [f"TEST: LIFECYCLE request={number}.", CRC, ROUTE, ACK if number == 3 else NACK,
            f"TEST: LIFECYCLE command={number} mode={number-1}."]


def sequence():
    return [BOOT, "WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason=1.",
            SOFTWARE, CORE + " Network at startup: connected; log mirror: started.", reader.DEMO, reader.PROFILE,
            state("boot", 0, 0, 0, 1, 1, 0, archive=136), *command(1),
            state("operating", 2, 1, 0, 3, 2, 64), "FDIR: Fault from Source 1 (Severity: 2) Code: 0xE102",
            state("faulted", 1, 1, 2, 0, 4, 64, 1, 7, 4), *command(2), reader.CLEAR,
            state("cleared", 1, 1, 2, 0, 4, 64, pdu=4), *command(3),
            state("ground", 2, 1, 0, 0, 4, 64), "TEST: LIFECYCLE retained index=1 crc=123 length=64.",
            *[state("complete", 2, 1, 0, 1, 1, 128, archive=408) for _ in range(3)],
            "TEST: LIFECYCLE stack_free_min_bytes=2048.",
            "TEST: LIFECYCLE finished."]


def feed(lines):
    evidence = reader.LifecycleEvidence()
    for now, line in enumerate(lines): evidence.observe(line, now)
    return evidence


class InlineThread:
    def __init__(self, *, target, args, daemon): self.target, self.args = target, args
    def start(self): self.target(*self.args)
    def is_alive(self): return False
    def join(self, timeout): pass


class FakePort:
    def __init__(self, lines): self.lines, self.opened, self.rts, self.dtr = iter(lines), False, None, None
    def __enter__(self): return self
    def __exit__(self, *args): self.opened = False
    def open(self):
        assert self.dtr is False and self.rts is False
        self.opened = True
    def reset_input_buffer(self): pass
    def readline(self, limit):
        try: return (next(self.lines) + "\n").encode()
        except StopIteration: raise RuntimeError("No fake lines left")


class ReaderTests(unittest.TestCase):
    def test_selected_sequence_needs_three_ground_histories(self):
        evidence = feed(sequence())
        self.assertFalse(evidence.complete())
        evidence.histories = [1, 2, 3]
        self.assertTrue(evidence.complete())
        self.assertEqual(evidence.commands, [1, 2, 3])
        self.assertEqual((evidence.faults, evidence.clears), (1, 1))

    def test_each_missing_marker_rejected_or_incomplete(self):
        for index, line in enumerate(sequence()):
            with self.subTest(index=index, marker=line[:40]):
                try: evidence = feed(sequence()[:index] + sequence()[index+1:])
                except reader.EvidenceError: continue
                evidence.histories = [1, 2, 3]
                self.assertFalse(evidence.complete())

    def test_duplicate_nonrepeatable_markers(self):
        for index, line in enumerate(sequence()):
            if "state=complete" in line: continue
            with self.subTest(index=index):
                with self.assertRaises(reader.EvidenceError): feed(sequence()[:index+1] + [line] + sequence()[index+1:])

    def test_wrong_fault_owner_code_severity(self):
        original = "FDIR: Fault from Source 1 (Severity: 2) Code: 0xE102"
        for replacement in (original.replace("Source 1", "Source 5"), original.replace("E102", "C200"),
                            original.replace("Severity: 2", "Severity: 3"), original.replace("Severity: 2", "Severity: 1")):
            with self.assertRaises(reader.EvidenceError): feed([replacement if row == original else row for row in sequence()])

    def test_early_acceptance_or_late_denial(self):
        for old, new in ((NACK, ACK), (ACK, NACK)):
            with self.assertRaises(reader.EvidenceError): feed([new if row == old else row for row in sequence()])

    def test_state_mismatches_and_history_loss(self):
        changes = {"boot": ("deployment=0", "deployment=1"), "operating": ("payload=2", "payload=1"),
                   "faulted": ("adcs=0", "adcs=3"), "cleared": ("mode=1", "mode=2"),
                   "ground": ("payload=4", "payload=2"), "complete": ("bytes=128", "bytes=64")}
        for stage, (old, new) in changes.items():
            with self.subTest(stage=stage):
                with self.assertRaises(reader.EvidenceError):
                    feed([row.replace(old, new) if "state=" + stage in row else row for row in sequence()])
        for old, new in (("archive=408", "archive=0"), ("bytes=128", "bytes=65"), ("revision=7", "revision=0"),
                         ("pdu=4", "pdu=0"), ("archive=408", "archive=65535"), ("adcs=1 payload=1 bytes=128", "adcs=0 payload=1 bytes=128")):
            with self.assertRaises(reader.EvidenceError): feed([row.replace(old, new) for row in sequence()])

    def test_premature_finish_and_stale_repeated_final(self):
        with self.assertRaises(reader.EvidenceError): feed(sequence()[:-4] + [sequence()[-1]])
        evidence = reader.LifecycleEvidence()
        with self.assertRaises(reader.EvidenceError):
            for line in sequence(): evidence.observe(line, 0)

    def test_bad_retention(self):
        for bad in ("index=30 crc=123", "index=1 crc=65536", "index=1 crc=123 length=63"):
            with self.assertRaises(reader.EvidenceError):
                feed(["TEST: LIFECYCLE retained " + bad + ("." if "length=" in bad else " length=64.") if "retained index=" in row else row for row in sequence()])

    def test_sdk_test_and_resource_errors(self):
        for line in ("TEST ERROR: LIFECYCLE check=42.", "TEST: WDT profile=task_loss target=0.",
                     "Brownout detector was triggered", "Task watchdog got triggered", "abort() was called",
                     "Resource diagnostics unavailable", "Telemetry Queue Full", "command queue full, dropped", "x"*1025):
            with self.assertRaises(reader.EvidenceError): reader.LifecycleEvidence().observe(line, 0)

    def test_clock_validation(self):
        for now in (-1, True, float("nan"), float("inf")):
            with self.assertRaises(reader.EvidenceError): reader.LifecycleEvidence().observe("ignored", now)
        evidence = reader.LifecycleEvidence(); evidence.observe("ignored", 1)
        with self.assertRaises(reader.EvidenceError): evidence.observe("ignored", 0)

    def test_target_check_is_numeric_and_reset_reason_bounded(self):
        with self.assertRaisesRegex(reader.EvidenceError, "target check failed: 42"):
            reader.LifecycleEvidence().observe("TEST ERROR: LIFECYCLE check=42.", 0)
        with self.assertRaises(reader.EvidenceError):
            feed([row.replace("reset_reason=1.", "reset_reason=999.") for row in sequence()])

    def test_stack_minimum_is_bounded(self):
        for value in (0, 767, 4097):
            with self.assertRaises(reader.EvidenceError):
                feed([row.replace("stack_free_min_bytes=2048", f"stack_free_min_bytes={value}") for row in sequence()])

    def run_cli(self, output, lines, *, sender_failure=False):
        port = FakePort(lines)
        clock = iter(n/2 for n in range(1000))
        sent = []
        def send(number, stop):
            sent.append(number)
            if sender_failure: raise RuntimeError("PRIVATE HTTP TEXT")
            return "bounded-identity"
        with patch.dict(sys.modules, serial=SimpleNamespace(Serial=lambda: port)), \
             patch.object(reader, "ModeSender", return_value=SimpleNamespace(send=send)), \
             patch.object(reader.threading, "Thread", InlineThread), \
             patch.object(reader.time, "monotonic", side_effect=lambda: next(clock)), \
             patch.object(reader.time, "sleep"), redirect_stdout(io.StringIO()):
            code = reader.main(["--reset", "--send-mode-commands", "--output", str(output)])
        return code, sent, port

    def test_cli_bounded_success_saves_proof(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "proof.json"
            code, sent, port = self.run_cli(output, sequence())
            self.assertEqual(code, 0); self.assertEqual(sent, [1, 2, 3]); self.assertFalse(port.opened)
            self.assertEqual(json.loads(output.read_text())["result"], "PASS")

    def test_cli_command_failure_no_retry_sanitized(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "proof.json"
            code, sent, port = self.run_cli(output, sequence(), sender_failure=True)
            self.assertEqual(code, 1); self.assertEqual(sent, [1]); self.assertFalse(port.opened)
            self.assertNotIn("PRIVATE", output.read_text()); self.assertEqual(json.loads(output.read_text())["result"], "FAIL")

    def test_cli_midrun_fail_retains_checkpoint(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "proof.json"
            code, sent, port = self.run_cli(output, sequence()[:7] + ["TEST ERROR: failed."])
            self.assertEqual(code, 1); self.assertFalse(port.opened)
            self.assertEqual(json.loads(output.read_text())["states"][0]["stage"], "boot")

    def test_cli_refuses_existing_output_and_missing_authorization(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "proof.json"; output.write_text("preserved")
            for flags in (["--reset", "--send-mode-commands"], ["--reset"], ["--send-mode-commands"], []):
                with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit): reader.main(flags + ["--output", str(output)])
            self.assertEqual(output.read_text(), "preserved")


if __name__ == "__main__": unittest.main()
