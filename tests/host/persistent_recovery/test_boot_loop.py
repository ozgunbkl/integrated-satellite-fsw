"""Strict four-boot oracle, simulated serial/clock only; never touches NVS/USB."""
from contextlib import redirect_stderr
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from test_reader import reader, journal, sdk, boot_rows, status, finish


def evidence(history=34):
    return reader.PersistentEvidence("boot-loop", expected_history=history, initial_boot=7,
                                     allow_fault_history=bool(history & 2), allow_boot_loop_test=True)


def rows(index, history=34):
    boot, failed = 7 + index, index - 1
    reason = 3 if index == 1 else 4
    flags = history | 1 | (32 if index > 1 else 0) | (64 if index == 4 else 0)
    loop = f"TEST: RECOVERY_LOOP injection={'armed' if index < 4 else 'suppressed'} reset_reason={reason} boot={boot} failed_boots={failed} target=3."
    if index < 4:
        return [reader.BOOT, journal(boot, failed, int(bool(flags & ~1)), flags), sdk(reason), reader.ARCHIVE, loop,
                f"TEST: STARTUP profile=hk_init injection=armed reset_reason={reason}.",
                "TEST: STARTUP injected kind=hk_init return=false real_call=skipped.",
                "E (10) MAIN: Failed to initialize HK service", "abort() was called at PC 0x4001234 on core 0",
                "Backtrace: 0x40000000:0x3ff00000"]
    result = boot_rows(boot, flags=flags, failed=failed, reason=reason, profile=True)
    result.insert(4, loop)
    return result


def booted(history=34):
    value = evidence(history)
    for index in range(1, 5):
        for line in rows(index, history):
            value.observe(line, index - 1)
    return value


def ready(history=34):
    value = booted(history)
    value.observe(reader.DEPLOYED, 10)
    for now in (10, 15, 20): value.observe(reader.SAFE, now)
    value.observe(status(boot=11, flags=history | 96), 40)
    return value


class BootLoopTests(unittest.TestCase):
    def test_three_real_abort_markers_four_boots_all_baselines(self):
        for history in reader.BASE_HISTORIES:
            with self.subTest(history=history):
                value = finish(ready(history))
                self.assertTrue(value.complete(70))
                self.assertEqual([r["failed_boots"] for r in value.journals], [0, 1, 2, 3])
                self.assertEqual(value.reasons, [3, 4, 4, 4])
                self.assertEqual(value.abort_count, 3)
                self.assertEqual(value.snapshot(70, "PASS")["selected_abort_count"], 3)

    def test_separate_injection_approval_and_room_for_five_boots(self):
        for options in ({}, {"allow_boot_loop_test": True},
                        {"allow_boot_loop_test": True, "initial_boot": 0xFFFFFFFB},
                        {"allow_boot_loop_test": True, "initial_boot": True},
                        {"allow_boot_loop_test": True, "initial_boot": 7, "expected_history": 64}):
            with self.assertRaises(ValueError): reader.PersistentEvidence("boot-loop", **options)
        reader.PersistentEvidence("boot-loop", allow_boot_loop_test=True, initial_boot=0xFFFFFFFA)

    def test_no_request_during_three_failing_boots(self):
        value=evidence()
        for index in range(1, 4):
            for line in rows(index): value.observe(line, index)
            self.assertIsNone(value.next_request(index))

    def test_each_restart_requires_attributed_abort(self):
        for omitted in (5, 6, 7, 8):
            value=evidence()
            with self.assertRaises(reader.EvidenceError):
                for i, line in enumerate(rows(1)):
                    if i != omitted: value.observe(line, 0)
                value.observe(reader.BOOT, 1)

    def test_three_loop_markers_bound_to_journal_and_reason(self):
        for changed in ("boot=9", "failed_boots=1", "reset_reason=4", "injection=suppressed"):
            value=evidence()
            for line in rows(1)[:4]: value.observe(line, 0)
            original=rows(1)[4]
            key=changed.split("=")[0]
            import re
            forged=re.sub(key + r"=\w+", changed, original)
            with self.assertRaises(reader.EvidenceError): value.observe(forged, 0)

    def test_loop_marker_is_mandatory_and_unique(self):
        for duplicate in (False, True):
            value=evidence()
            with self.assertRaises(reader.EvidenceError):
                sequence=rows(1)[:5] + ([rows(1)[4]] if duplicate else []) + rows(1)[5:]
                if not duplicate: sequence.remove(rows(1)[4])
                for line in sequence: value.observe(line, 0)

    def test_counter_flags_threshold_exact_on_every_boot(self):
        for index in range(1, 5):
            for delta in ("missing_loop", "early_loop", "failed", "counter"):
                if delta == "missing_loop" and index != 4: continue
                if delta == "early_loop" and index == 4: continue
                value=evidence()
                for before in range(1, index):
                    for line in rows(before): value.observe(line, before - 1)
                value.observe(reader.BOOT, index - 1)
                flags=34 | 1 | (64 if index == 4 else 0)
                flags = flags & ~64 if delta=="missing_loop" else flags | 64 if delta=="early_loop" else flags
                with self.assertRaises(reader.EvidenceError):
                    value.observe(journal(7+index+(delta=="counter"), index-1+(delta=="failed"), 1, flags), index-1)

    def test_all_three_resets_must_have_sdk_panic_reason(self):
        for index in (2, 3, 4):
            value=evidence()
            for before in range(1, index):
                for line in rows(before): value.observe(line, before - 1)
            for line in rows(index)[:2]: value.observe(line, index - 1)
            with self.assertRaises(reader.EvidenceError): value.observe(sdk(3), index - 1)

    def test_no_injection_on_fourth_boot_or_fifth_reset(self):
        value=booted()
        with self.assertRaises(reader.EvidenceError): value.observe(reader.BOOT, 4)
        with self.assertRaises(reader.EvidenceError):
            booted().observe("TEST: STARTUP injected kind=hk_init return=false real_call=skipped.", 4)

    def test_cannot_hide_lost_boot_loop_bit_in_health_status(self):
        with self.assertRaises(reader.EvidenceError): ready().observe(status(boot=11, flags=34), 41)

    def test_sticky_loop_restore_requires_its_own_history_approval(self):
        for approved in (False, 1, None):
            with self.assertRaises(ValueError):
                reader.PersistentEvidence("restore", 11, expected_history=98, allow_fault_history=True,
                                          allow_boot_loop_history=approved)
        with self.assertRaises(ValueError): reader.PersistentEvidence("restore", 11, allow_boot_loop_history=True)

    def test_strict_normal_restore_keeps_fault_bad_reset_and_loop_history(self):
        value=reader.PersistentEvidence("restore", 11, expected_history=98, allow_fault_history=True,
                                        allow_boot_loop_history=True)
        from test_reader import ready as normal_ready
        self.assertTrue(finish(normal_ready(value)).complete(70))
        self.assertEqual(value.journals[0]["flags"], 99)

    def test_passive_loop_status_only_with_explicit_opt_in(self):
        with self.assertRaises(reader.EvidenceError): reader.BaselineEvidence().observe(status(flags=98))
        value=reader.BaselineEvidence(allow_boot_loop_history=True)
        value.observe(status(flags=98))
        self.assertEqual(value.status["history_flags"], 98)
        for flags in (102, 106, 114):
            with self.assertRaises(reader.EvidenceError): value.observe(status(flags=flags))

    def test_no_automatic_nominal_before_separate_approval(self):
        with self.assertRaises(reader.EvidenceError): booted().observe(reader.NOMINAL, 4)

    def test_cli_missing_loop_approval_refuses_before_any_http(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(reader, "JsonHttp") as api, redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                reader.main(["--phase", "boot-loop", "--initial-boot", "7", "--reset", "--allow-persistent-writes",
                             "--send-recovery-commands", "--output", str(Path(directory)/"proof.json")])
            api.assert_not_called()

    def test_full_cli_fake_usb_http_three_panics_and_sticky_restore(self):
        import json
        from test_reader import CliTests
        for phase, history in (("boot-loop", 0), ("boot-loop", 34), ("restore", 98)):
            with self.subTest(phase=phase, history=history), tempfile.TemporaryDirectory() as directory:
                result, proof, port, output = CliTests().active_run(directory, phase=phase, history=history)
                self.assertEqual(result, 0, output)
                saved=json.loads(proof.read_text())
                self.assertEqual(saved["result"], "PASS")
                self.assertEqual(saved["selected_abort_count"], 3 if phase == "boot-loop" else 0)
                self.assertEqual(saved["requests"], list(reader.KINDS))
                self.assertEqual(saved["sent_history_verified"], list(reader.KINDS))
                self.assertTrue(port.closed)


if __name__ == "__main__": unittest.main()
