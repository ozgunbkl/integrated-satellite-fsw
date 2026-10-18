from contextlib import redirect_stdout, redirect_stderr
import base64
import io
from pathlib import Path
import struct
import sys
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import persistent_recovery_check as reader


def journal(boot, failed=0, held=1, flags=33):
    return f"RECOVERY: boot={boot} failed_boots={failed} held={held} flags={flags}; journal committed before core init."


def status(boot=12, held=1, ready=1, storage=1, stable=1, flags=32, stack=2048):
    return (f"RECOVERY: boot={boot} held={held} ready={ready} storage_ok={storage} "
            f"stable={stable} flags={flags} stack_free_bytes={stack}.")


def sdk(reason):
    return f"WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason={reason}."


def boot_rows(boot, held=1, flags=33, failed=0, reason=3, profile=False):
    rows = [reader.BOOT, journal(boot, failed, held, flags), sdk(reason), reader.ARCHIVE]
    if profile:
        rows.append(f"TEST: STARTUP profile=hk_init injection=suppressed reset_reason={reason}.")
    rows += [reader.HK_READY, reader.RESOURCES, reader.ARM, reader.SOFTWARE, reader.CORE + " Network at startup: connected; log mirror: started."]
    for marker, route in reader.DEMO:
        rows += [marker, crc("blocked"), route]
    return rows


def ready(evidence=None):
    evidence = evidence or reader.PersistentEvidence("restore", 11)
    history = evidence.expected_history | 32
    for line in boot_rows(12, flags=history | 1):
        evidence.observe(line, 0)
    evidence.observe(reader.DEPLOYED, 10)
    for now in (10, 15, 20):
        evidence.observe(reader.SAFE, now)
    evidence.observe(status(flags=history), 40)
    return evidence


def crc(kind):
    size = 18 if kind in ("blocked", "nominal") else 22
    return f"TC PROC: [CRC OK] Packet Length: {size} bytes. Handing to Router..."


def transaction(evidence, kind, now):
    evidence.begin_request(kind, now)
    for line in (crc(kind), reader.ROUTE):
        evidence.observe(line, now)
    if kind == "approve":
        evidence.observe(reader.QUEUED, now)
        evidence.observe(reader.COMPLETE, now)
    else:
        evidence.observe({"blocked": reader.NACK, "stale": reader.REJECTED, "nominal": reader.ACK}[kind], now)
    evidence.sent(kind)


def finish(evidence):
    for kind, now in (("blocked", 41), ("stale", 42), ("approve", 43)):
        transaction(evidence, kind, now)
    evidence.observe(reader.SAFE, 45)
    transaction(evidence, "nominal", 46)
    for now in (50, 55, 60):
        evidence.observe(reader.NOMINAL, now)
    evidence.observe(status(boot=evidence.boot, held=0, flags=evidence.journals[-1]["flags"] & ~1), 70)
    evidence.observe(reader.NOMINAL, 70)
    return evidence


class EvidenceTests(unittest.TestCase):
    def test_full_restored_normal_with_sticky_history(self):
        evidence = finish(ready())
        self.assertTrue(evidence.complete(70))
        self.assertEqual(evidence.requests, list(reader.KINDS))
        self.assertEqual(evidence.stack_min, 2048)

    def test_real_selected_abort_then_second_boot_is_required(self):
        evidence = reader.PersistentEvidence("panic")
        for line in [reader.BOOT, journal(11, held=0, flags=1), sdk(3), reader.ARCHIVE,
                     "TEST: STARTUP profile=hk_init injection=armed reset_reason=3.",
                     "TEST: STARTUP injected kind=hk_init return=false real_call=skipped.",
                     "E (10) MAIN: Failed to initialize HK service", "abort() was called at PC 0x4001234 on core 0",
                     "Backtrace: 0x40000000:0x3ff00000"]:
            evidence.observe(line, 0)
        for line in boot_rows(12, failed=1, reason=4, profile=True):
            evidence.observe(line, 1)
        evidence.observe(reader.DEPLOYED, 10)
        for now in (10, 15, 20):
            evidence.observe(reader.SAFE, now)
        evidence.observe(status(), 40)
        self.assertTrue(finish(evidence).complete(70))
        self.assertEqual(evidence.reasons, [3, 4])

    def test_panic_initial_bad_reason_not_counted_as_selected_injection(self):
        evidence = reader.PersistentEvidence("panic", expected_history=32)
        for line in (reader.BOOT, journal(1)):
            evidence.observe(line, 0)
        with self.assertRaises(reader.EvidenceError):
            evidence.observe(sdk(4), 0)

    def test_missing_or_duplicate_startup_markers(self):
        rows = boot_rows(12)
        for index in range(len(rows)):
            for changed in (rows[:index] + rows[index+1:], rows[:index+1] + rows[index:]):
                with self.subTest(index=index, duplicate=len(changed) > len(rows)):
                    try:
                        evidence = reader.PersistentEvidence("restore", 11)
                        for line in changed:
                            evidence.observe(line, 0)
                        evidence.observe(reader.DEPLOYED, 10)
                        for now in (10, 15, 20):
                            evidence.observe(reader.SAFE, now)
                        evidence.observe(status(), 40)
                        evidence = finish(evidence)
                        self.assertFalse(evidence.complete(70))
                    except reader.EvidenceError:
                        pass

    def test_normal_restore_rejects_counter_loss(self):
        for boot in (1, 11, 13, 0x100000000):
            evidence = reader.PersistentEvidence("restore", 11)
            evidence.observe(reader.BOOT, 0)
            with self.assertRaises(reader.EvidenceError):
                evidence.observe(journal(boot), 0)

    def test_historical_faults_are_not_automatically_approved(self):
        for flags in (1, 3, 5, 9, 17, 35, 37, 41, 49, 65, 97, 161):
            evidence = reader.PersistentEvidence("restore", 11)
            evidence.observe(reader.BOOT, 0)
            with self.assertRaises(reader.EvidenceError):
                evidence.observe(journal(12, flags=flags), 0)

    def test_status_invalid_health_storage_history_stack(self):
        for changes in (dict(boot=13), dict(held=0), dict(storage=0), dict(stable=0), dict(ready=0), dict(flags=33),
                        dict(flags=0), dict(flags=36), dict(stack=511), dict(stack=6145)):
            with self.subTest(changes=changes), self.assertRaises(reader.EvidenceError):
                ready().observe(status(**changes), 41)

    def test_premature_readiness_or_missing_safe_observation(self):
        for now, safe_samples in ((29, (10, 15, 20)), (40, (10, 11, 12)), (40, ())):
            evidence = reader.PersistentEvidence("restore", 11)
            for line in boot_rows(12):
                evidence.observe(line, 0)
            evidence.observe(reader.DEPLOYED, 9)
            for sample in safe_samples:
                evidence.observe(reader.SAFE, sample)
            with self.assertRaises(reader.EvidenceError):
                evidence.observe(status(), now)

    def test_no_command_before_ready(self):
        evidence = reader.PersistentEvidence("restore", 11)
        self.assertIsNone(evidence.next_request(0))
        with self.assertRaises(reader.EvidenceError):
            evidence.begin_request("approve", 0)

    def test_stable_marker_counter_must_survive_normal_restore(self):
        evidence = reader.PersistentEvidence("restore", 11)
        evidence.observe(reader.BOOT, 0)
        with self.assertRaises(reader.EvidenceError):
            evidence.observe(journal(12, failed=1), 0)

    def test_wrong_command_order_and_repeated_results(self):
        with self.assertRaises(reader.EvidenceError):
            ready().begin_request("approve", 41)
        evidence = ready()
        transaction(evidence, "blocked", 41)
        with self.assertRaises(reader.EvidenceError):
            evidence.observe(reader.NACK, 41)

    def test_unattributed_or_wrong_length_responses(self):
        for line in (reader.ACK, reader.NACK, reader.QUEUED, reader.REJECTED, reader.COMPLETE, reader.ROUTE, crc("approve")):
            with self.subTest(line=line), self.assertRaises(reader.EvidenceError):
                ready().observe(line, 41)

    def test_pending_is_not_completion(self):
        evidence = ready()
        transaction(evidence, "blocked", 41)
        transaction(evidence, "stale", 42)
        evidence.begin_request("approve", 43)
        for line in (crc("approve"), reader.ROUTE, reader.QUEUED):
            evidence.observe(line, 43)
        evidence.sent("approve")
        self.assertIsNone(evidence.next_request(44))
        self.assertIsNone(evidence.rearmed_at)
        self.assertFalse(evidence.complete(44))

    def test_sent_history_not_board_result_and_reverse(self):
        evidence = ready()
        evidence.begin_request("blocked", 41)
        evidence.sent("blocked")
        self.assertIsNone(evidence.next_request(42))
        for line in (crc("blocked"), reader.ROUTE, reader.NACK):
            evidence.observe(line, 42)
        self.assertEqual(evidence.next_request(42), "stale")
        evidence = ready()
        evidence.begin_request("blocked", 41)
        for line in (crc("blocked"), reader.ROUTE, reader.NACK):
            evidence.observe(line, 41)
        self.assertIsNone(evidence.next_request(42))

    def test_approval_needs_safe_dwell_before_separate_nominal(self):
        evidence = ready()
        for kind, now in (("blocked", 41), ("stale", 42), ("approve", 43)):
            transaction(evidence, kind, now)
        self.assertIsNone(evidence.next_request(45))
        evidence.observe(reader.SAFE, 45)
        self.assertEqual(evidence.next_request(45), "nominal")

    def test_no_automatic_nominal_even_after_valid_approval(self):
        for after_approval in (False, True):
            evidence = ready()
            if after_approval:
                for kind, now in (("blocked", 41), ("stale", 42), ("approve", 43)):
                    transaction(evidence, kind, now)
            for line in (reader.NOMINAL, "SYSTEM MODE: SAFE -> NOMINAL."):
                with self.assertRaises(reader.EvidenceError):
                    evidence.observe(line, 44)

    def test_no_pass_without_released_status(self):
        evidence = finish(ready())
        evidence.released_seen = False
        self.assertFalse(evidence.complete(70))

    def test_no_pass_with_missing_or_stale_nominal_samples(self):
        evidence = finish(ready())
        self.assertFalse(evidence.complete(81))
        evidence.nominal_times = [69, 70]
        self.assertFalse(evidence.complete(81))

    def test_bad_clock_or_oversized_line(self):
        for now in (-1, float("nan"), float("inf"), True):
            with self.assertRaises(reader.EvidenceError):
                reader.PersistentEvidence("panic").observe("", now)
        with self.assertRaises(reader.EvidenceError):
            ready().observe("", 39)
        with self.assertRaises(reader.EvidenceError):
            ready().observe("x" * 1025, 41)

    def test_faults_reboots_or_unknown_recovery_never_pass(self):
        for line in (reader.BOOT, "FDIR: Fault from Source 1 (Severity: 2) Code: 0xE102",
                     "RECOVERY: Journal write/read-back FAILED; operating modes inhibited for this boot.",
                     "RECOVERY: Storage/init unavailable; operating modes inhibited; no automatic erase.",
                     "TEST: WDT other profile", "Task watchdog got triggered", "Guru Meditation", "Backtrace: 0x40000000:0x3ff00000"):
            with self.subTest(line=line), self.assertRaises(reader.EvidenceError):
                ready().observe(line, 41)

    def test_invalid_phase_or_previous_counter(self):
        for phase, previous in (("unknown", None), ("restore", None), ("restore", 0), ("restore", True), ("restore", 0xFFFFFFFF)):
            with self.assertRaises(ValueError):
                reader.PersistentEvidence(phase, previous)

    def test_existing_local_demo_is_separate_from_ground_fdir(self):
        evidence = ready()
        self.assertEqual(evidence.demo_seen, [row[1] for row in reader.DEMO])
        self.assertEqual((evidence.requests, evidence.histories), ([], []))
        self.assertTrue(finish(evidence).complete(70))

    def test_unknown_repeated_or_late_demo_is_rejected(self):
        for marker in (reader.DEMO[0][0], "INJECTOR: Sending APID 0x030, CMD 82, CRC 0x0000"):
            with self.assertRaises(reader.EvidenceError):
                ready().observe(marker, 41)

    def test_local_demo_cannot_supply_fdir_crc_or_hide_wrong_route(self):
        rows = boot_rows(12)
        first = rows.index(reader.DEMO[0][0])
        for changed in (reader.ROUTE, reader.DEMO[1][1]):
            evidence = reader.PersistentEvidence("restore", 11)
            for line in rows[:first+2]:
                evidence.observe(line, 0)
            with self.assertRaises(reader.EvidenceError):
                evidence.observe(changed, 0)

    def test_demo_crc_required_and_missing_demo_cannot_pass(self):
        rows = boot_rows(12)
        first = rows.index(reader.DEMO[0][0])
        evidence = reader.PersistentEvidence("restore", 11)
        for line in rows[:first+1]:
            evidence.observe(line, 0)
        with self.assertRaises(reader.EvidenceError):
            evidence.observe(reader.DEMO[0][1], 0)
        evidence = finish(ready())
        evidence.demo_seen.pop()
        self.assertFalse(evidence.complete(70))

    def test_fallback_can_restore_fresh_normal_but_not_claim_panic(self):
        evidence = reader.PersistentEvidence("fallback")
        for line in boot_rows(1, held=0, flags=1):
            evidence.observe(line, 0)
        evidence.observe(reader.DEPLOYED, 10)
        for now in (10, 15, 20, 40):
            evidence.observe(reader.NOMINAL, now)
        evidence.observe(status(boot=1, held=0, flags=0), 40)
        self.assertIsNone(evidence.next_request(40))
        self.assertTrue(evidence.complete(40))
        self.assertFalse(evidence.aborted)

    def test_fault_baseline_requires_specific_approval(self):
        for history, approved in ((2, False), (34, False), (0, True), (32, True), (2, 1)):
            with self.subTest(history=history, approved=approved), self.assertRaises(ValueError):
                reader.PersistentEvidence("panic", expected_history=history, allow_fault_history=approved)

    def test_unsupported_baseline_and_exhausted_initial_counter(self):
        for history in (-1, True, 1, 4, 8, 16, 64, 127, 0x100000000):
            with self.assertRaises(ValueError):
                reader.PersistentEvidence("panic", expected_history=history)
        for boot in (True, 0, -1, 0xFFFFFFFD):
            with self.assertRaises(ValueError):
                reader.PersistentEvidence("panic", initial_boot=boot)

    def test_explicit_nonfatal_restore_keeps_both_history_bits(self):
        evidence = finish(ready(reader.PersistentEvidence("restore", 11, expected_history=2, allow_fault_history=True)))
        self.assertTrue(evidence.complete(70))
        self.assertEqual(evidence.journals[0]["flags"], 35)
        self.assertEqual(evidence.snapshot(70, "PASS")["expected_baseline_history"], 2)

    def test_selected_panic_all_exact_supported_baselines(self):
        for history in reader.BASE_HISTORIES:
            with self.subTest(history=history):
                evidence = reader.PersistentEvidence("panic", expected_history=history, initial_boot=10,
                                                     allow_fault_history=bool(history & 2))
                for line in [reader.BOOT, journal(11, held=int(bool(history)), flags=history | 1), sdk(3), reader.ARCHIVE,
                             "TEST: STARTUP profile=hk_init injection=armed reset_reason=3.",
                             "TEST: STARTUP injected kind=hk_init return=false real_call=skipped.",
                             "E (10) MAIN: Failed to initialize HK service", "abort() was called at PC 0x4001234 on core 0"]:
                    evidence.observe(line, 0)
                for line in boot_rows(12, flags=history | 33, failed=1, reason=4, profile=True):
                    evidence.observe(line, 1)
                evidence.observe(reader.DEPLOYED, 10)
                for now in (10, 15, 20): evidence.observe(reader.SAFE, now)
                evidence.observe(status(flags=history | 32), 40)
                self.assertTrue(finish(evidence).complete(70))
                self.assertEqual([row["boot"] for row in evidence.journals], [11, 12])

    def test_first_panic_counter_and_exact_history_cannot_reset_or_expand(self):
        for row in (journal(1, flags=3), journal(10, flags=3), journal(12, flags=3),
                    journal(11, failed=1, flags=3), journal(11, held=0, flags=1), journal(11, flags=35)):
            evidence = reader.PersistentEvidence("panic", expected_history=2, initial_boot=10, allow_fault_history=True)
            evidence.observe(reader.BOOT, 0)
            with self.assertRaises(reader.EvidenceError): evidence.observe(row, 0)
            self.assertIsNotNone(evidence.snapshot(0, "FAIL")["rejected_journal"])
            self.assertEqual(evidence.requests, [])

    def test_restore_cannot_drop_old_fault_add_fault_or_drop_bad_reset(self):
        for flags in (1, 3, 33, 39, 43, 51, 99):
            evidence = reader.PersistentEvidence("restore", 11, expected_history=2, allow_fault_history=True)
            evidence.observe(reader.BOOT, 0)
            with self.assertRaises(reader.EvidenceError): evidence.observe(journal(12, flags=flags), 0)
        evidence = reader.PersistentEvidence("restore", 11)
        evidence.observe(reader.BOOT, 0)
        with self.assertRaises(reader.EvidenceError): evidence.observe(journal(12, flags=35), 0)

    def test_fallback_fault_baseline_bound_without_claiming_selected_panic(self):
        evidence = finish(ready(reader.PersistentEvidence("fallback", initial_boot=10, expected_history=2, allow_fault_history=True)))
        self.assertTrue(evidence.complete(70))
        self.assertFalse(evidence.aborted)
        for boot in (1, 10):
            evidence = reader.PersistentEvidence("fallback", initial_boot=10, expected_history=2, allow_fault_history=True)
            evidence.observe(reader.BOOT, 0)
            with self.assertRaises(reader.EvidenceError): evidence.observe(journal(boot, flags=3), 0)


class BaselineTests(unittest.TestCase):
    def test_passive_baseline_does_not_claim_health_or_approval(self):
        evidence = reader.BaselineEvidence()
        evidence.observe(status(boot=3, flags=3, stable=0, ready=0))
        self.assertIsNone(evidence.status)
        evidence.observe(status(boot=3, held=0, flags=2, ready=0))
        self.assertEqual(evidence.status["boot"], 3)
        self.assertEqual(evidence.status["history_flags"], 2)
        self.assertEqual(evidence.status["ready"], 0)

    def test_unsupported_or_failed_baseline_is_refused(self):
        for line in (status(flags=4), status(flags=8), status(flags=16), status(flags=64), status(flags=36),
                     status(storage=0), status(stack=511), status(boot=0), status(boot=0xFFFFFFFD),
                     status(flags=0), "RECOVERY: boot=bad", reader.BOOT, "Guru Meditation Error"):
            with self.subTest(line=line), self.assertRaises(reader.EvidenceError):
                reader.BaselineEvidence().observe(line)

    def test_passive_cli_has_no_reset_http_or_tc(self):
        import json
        class Port:
            rts = dtr = None
            def __enter__(self): return self
            def __exit__(self, *_): pass
            def open(self):
                self.opened = True
                assert self.rts is False and self.dtr is False
            def readline(self, _): return (status(boot=3, held=0, flags=2) + "\n").encode()
        with tempfile.TemporaryDirectory() as directory:
            out, port = Path(directory) / "baseline.json", Port()
            with patch.object(reader, "JsonHttp") as api, patch.dict(sys.modules, {"serial": SimpleNamespace(Serial=lambda: port)}), redirect_stdout(io.StringIO()):
                self.assertEqual(reader.main(["--inspect-current", "--output", str(out)]), 0)
            api.assert_not_called()
            self.assertEqual(json.loads(out.read_text())["status"]["history_flags"], 2)
            self.assertEqual((port.rts, port.dtr), (False, False))

    def test_passive_cli_rejects_actions_before_port_or_api(self):
        with tempfile.TemporaryDirectory() as directory:
            for flag in ("--reset", "--preflight", "--allow-fault-history", "--send-recovery-commands", "--allow-persistent-writes"):
                serial = SimpleNamespace(Serial=unittest.mock.Mock())
                with patch.object(reader, "JsonHttp") as api, patch.dict(sys.modules, {"serial": serial}), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                    reader.main(["--inspect-current", "--output", str(Path(directory) / "unused.json"), flag])
                api.assert_not_called()
                serial.Serial.assert_not_called()

    def test_missing_fault_consent_fails_before_any_api(self):
        with patch.object(reader, "JsonHttp") as api, redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            reader.main(["--phase", "panic", "--reset", "--allow-persistent-writes", "--send-recovery-commands",
                         "--expected-history", "2", "--output", "unused.json"])
        api.assert_not_called()

    def test_passive_existing_output_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "baseline.json"
            out.write_text("original")
            serial = SimpleNamespace(Serial=unittest.mock.Mock())
            with patch.dict(sys.modules, {"serial": serial}), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                reader.main(["--inspect-current", "--output", str(out)])
            serial.Serial.assert_not_called()
            self.assertEqual(out.read_text(), "original")

    def test_passive_read_error_saved_without_raw_details(self):
        import json
        class Port:
            def __enter__(self): return self
            def __exit__(self, *_): pass
            def open(self): raise OSError("private UART details")
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory) / "baseline.json"
            with patch.dict(sys.modules, {"serial": SimpleNamespace(Serial=Port)}), redirect_stdout(io.StringIO()) as output:
                self.assertEqual(reader.main(["--inspect-current", "--output", str(out)]), 1)
            self.assertEqual(json.loads(out.read_text())["result"], "FAIL")
            self.assertNotIn("private", out.read_text() + output.getvalue())


class Clock:
    def __init__(self): self.value = 0
    def __call__(self): return self.value
    def sleep(self, seconds): self.value += seconds


class FakeApi:
    def __init__(self, change=None): self.calls, self.change = [], change
    def __call__(self, path, body=None, timeout=4):
        self.calls.append((path, body))
        if "/links/" in path:
            outgoing = path.endswith("udp-out")
            result = dict(disabled=False, status="OK", type="org.yamcs.tctm." + ("UdpTcDataLink" if outgoing else "UdpTmDataLink"),
                          detailedStatus="sending to 192.168.1.5:10025")
        elif path.startswith("/api/processors/"):
            name = "/myproject/FDIR_RearmBoot" if "RearmBoot" in path else "/myproject/FDIR_SetMode"
            payload = (b"\x53" + struct.pack("!I", body["args"]["BootCounter"]) + b"\xA5" if "RearmBoot" in name else b"\x52\x02")
            self.payload, self.name = payload, name
            raw = struct.pack("!3H", 0x1030, 0xC000, 0) + payload
            result = dict(commandName=name, id="command-id", generationTime="2026-10-17T09:00:00Z",
                          unprocessedBinary=base64.b64encode(raw).decode())
        else:
            raw = struct.pack("!3H", 0x1030, 0xC001, len(self.payload) - 1) + self.payload
            result = dict(id="command-id", commandName=self.name, attr=[
                dict(name="Acknowledge_Sent_Status", value=dict(stringValue="OK")),
                dict(name="binary", value=dict(binaryValue=base64.b64encode(raw).decode()))])
        if self.change:
            self.change(path, body, result)
        return result


class SenderTests(unittest.TestCase):
    def sender(self, api):
        clock = Clock()
        return reader.RecoverySender(api, clock=clock, sleep=clock.sleep)

    def test_preflight_only_generates_no_actual_tc(self):
        api = FakeApi()
        self.sender(api).preflight()
        self.assertEqual(len(api.calls), 4)
        self.assertTrue(all(body is None or body["dryRun"] is True for _, body in api.calls))

    def test_fixed_bodies_and_unsigned_big_endian(self):
        for boot in (2, 0x80000000, 0xFFFFFFFF):
            for kind in reader.KINDS:
                with self.subTest(boot=boot, kind=kind):
                    api = FakeApi()
                    self.sender(api).send(kind, boot, threading.Event())
                    issued = [body for _, body in api.calls if body and not body["dryRun"]]
                    self.assertEqual(len(issued), 1)
                    if kind in ("approve", "stale"):
                        self.assertEqual(issued[0]["args"], dict(BootCounter=boot - (kind == "stale"), ConfirmKey=165))

    def test_invalid_kind_counter_or_ip_before_io(self):
        api = FakeApi()
        for kind, boot in (("erase", 12), ("approve", 0), ("approve", 1), ("approve", True), ("approve", 0x100000000)):
            with self.assertRaises(ValueError):
                self.sender(api).send(kind, boot, threading.Event())
        with self.assertRaises(ValueError):
            reader.RecoverySender(api, "not-an-ip")
        self.assertEqual(api.calls, [])

    def test_wrong_links_or_preview_fail_before_actual_post(self):
        for mutation in (lambda p, b, r: r.update(status="FAILED") if "/links/" in p else None,
                         lambda p, b, r: r.update(detailedStatus="sending to 192.168.1.50:10025") if p.endswith("udp-out") else None,
                         lambda p, b, r: r.update(commandName="wrong") if b else None,
                         lambda p, b, r: r.update(unprocessedBinary=base64.b64encode(b"wrong").decode()) if b else None):
            api = FakeApi(mutation)
            with self.assertRaises(ValueError):
                self.sender(api).send("approve", 12, threading.Event())
            self.assertFalse(any(body and body["dryRun"] is False for _, body in api.calls))

    def test_mismatching_sent_binary_is_not_success_and_never_retry(self):
        def mutate(path, body, result):
            if "/archive/" in path:
                result["attr"][1]["value"]["binaryValue"] = base64.b64encode(b"invalid").decode()
        api, stop = FakeApi(mutate), threading.Event()
        sender = self.sender(api)
        with self.assertRaises(ValueError):
            sender.send("approve", 12, stop)
        with self.assertRaises(ValueError):
            sender.send("approve", 12, stop)
        self.assertEqual(sum(bool(body) and not body["dryRun"] for _, body in api.calls), 1)

    def test_ambiguous_post_is_never_retried(self):
        def mutate(path, body, result):
            if body and body["dryRun"] is False:
                raise TimeoutError("Private details must not be logged")
        api, stop = FakeApi(mutate), threading.Event()
        sender = self.sender(api)
        with self.assertRaises(TimeoutError): sender.send("approve", 12, stop)
        with self.assertRaises(ValueError): sender.send("approve", 12, stop)
        self.assertEqual(sum(bool(body) and not body["dryRun"] for _, body in api.calls), 1)

    def test_history_rejection_or_timeout(self):
        for result_status in ("NOK", "TIMEOUT", "PENDING"):
            def mutate(path, body, result):
                if "/archive/" in path:
                    result["attr"][0]["value"]["stringValue"] = result_status
            api = FakeApi(mutate)
            with self.assertRaises(RuntimeError):
                self.sender(api).send("approve", 12, threading.Event())
            self.assertEqual(sum(bool(body) and not body["dryRun"] for _, body in api.calls), 1)

    def test_stop_prevents_any_io(self):
        api, stop = FakeApi(), threading.Event()
        stop.set()
        with self.assertRaises(RuntimeError): self.sender(api).send("approve", 12, stop)
        self.assertEqual(api.calls, [])

    def test_binary_mutations_and_postprocessed_length(self):
        body = reader.command("approve", 12)[2]
        raw = struct.pack("!3H", 0x1030, 0xC000, 0) + body
        reader.validate_binary(raw, body)
        for changed in (raw[:-1], raw + b"\x00", b"\x10\x31" + raw[2:], raw[:2] + b"\x00\x00" + raw[4:],
                        raw[:4] + b"\x00\x05" + raw[6:], raw[:-1] + b"\x00"):
            with self.assertRaises(ValueError): reader.validate_binary(changed, body)
        with self.assertRaises(ValueError): reader.validate_binary(raw, body, True)


class CliTests(unittest.TestCase):
    def test_missing_approvals_never_construct_serial_or_http(self):
        for arguments in ([], ["--phase", "panic"], ["--reset"], ["--phase", "restore", "--reset"],
                          ["--preflight", "--reset"], ["--preflight", "--output", "must-not-exist.json"]):
            with patch.object(reader, "JsonHttp") as api, redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                reader.main(arguments)
            api.assert_not_called()

    def test_preflight_cli_success_or_sanitized_failure(self):
        api = FakeApi()
        with patch.object(reader, "JsonHttp", return_value=api), redirect_stdout(io.StringIO()) as output:
            self.assertEqual(reader.main(["--preflight"]), 0)
        self.assertIn("no serial, actual TC", output.getvalue())
        with patch.object(reader, "JsonHttp", side_effect=RuntimeError("secret")), redirect_stdout(io.StringIO()) as output:
            self.assertEqual(reader.main(["--preflight"]), 1)
        self.assertNotIn("secret", output.getvalue())

    def active_run(self, directory, *, failure=None, api_change=None, phase="restore", history=0):
        clock = Clock()
        holder = {}
        evidence_class = reader.PersistentEvidence
        def factory(*args, **kwargs):
            holder["evidence"] = evidence_class(*args, **kwargs)
            return holder["evidence"]

        class InlineThread:
            def __init__(self, *, target, args, daemon): self.target, self.args = target, args
            def start(self): self.target(*self.args)
            def is_alive(self): return False
            def join(self, timeout): pass

        class Port:
            def __init__(self):
                self.rts = self.dtr = None
                self.opened = self.closed = False
                self.rows = [(i, line) for i, line in enumerate(boot_rows(12, flags=history | 33), 1)] + [
                    (20, reader.DEPLOYED), (21, reader.SAFE), (26, reader.SAFE), (31, reader.SAFE), (50, status(flags=history | 32))]
                if phase == "boot-loop":
                    from test_boot_loop import rows as loop_rows
                    lines = [line for index in range(1, 5) for line in loop_rows(index, history)]
                    self.rows = list(enumerate(lines, 1)) + [
                        (70, reader.DEPLOYED), (71, reader.SAFE), (76, reader.SAFE), (81, reader.SAFE),
                        (110, status(boot=11, flags=history | 96))]
                self.delivered, self.post = set(), False
            def __enter__(self): return self
            def __exit__(self, *args): self.closed = True
            def open(self):
                assert self.rts is False and self.dtr is False
                self.opened = True
            def reset_input_buffer(self): pass
            def readline(self, limit):
                if failure == "interrupt": raise KeyboardInterrupt()
                if failure == "serial": raise OSError("secret serial failure")
                if not self.rows:
                    evidence = holder["evidence"]
                    kind = evidence.pending
                    if kind and kind not in self.delivered:
                        self.delivered.add(kind)
                        lines = [crc(kind), reader.ROUTE] + (
                            [reader.QUEUED, reader.COMPLETE] if kind == "approve" else
                            [{"blocked": reader.NACK, "stale": reader.REJECTED, "nominal": reader.ACK}[kind]])
                        self.rows = [(clock.value + i + 1, line) for i, line in enumerate(lines)]
                    elif kind == "nominal" and not self.post:
                        self.post = True
                        self.rows = [(clock.value + i, reader.NOMINAL) for i in (5, 10, 15, 30)]
                        self.rows.insert(-1, (clock.value + 30, status(boot=11 if phase == "boot-loop" else 12,
                                             held=0, flags=history | (96 if phase == "boot-loop" else 32))))
                    else:
                        self.rows = [(clock.value + 3, reader.SAFE)]
                clock.value, line = self.rows.pop(0)
                return (line + "\n").encode()

        port = Port()
        proof = Path(directory) / "proof.json"
        args = ["--phase", phase, "--reset", "--allow-persistent-writes",
                "--send-recovery-commands", "--output", str(proof)]
        if phase == "restore":
            args += ["--previous-boot", "11"]
        if phase == "boot-loop":
            args += ["--initial-boot", "7", "--allow-boot-loop-test"]
        if history:
            args += ["--expected-history", str(history)]
        if history & 2:
            args += ["--allow-fault-history"]
        if history & 64:
            args += ["--allow-boot-loop-history"]
        with (patch.object(reader, "PersistentEvidence", side_effect=factory),
              patch.object(reader, "JsonHttp", return_value=FakeApi(api_change)),
              patch.object(reader.threading, "Thread", InlineThread),
              patch.object(reader.time, "monotonic", clock), patch.object(reader.time, "sleep", clock.sleep),
              patch.dict(sys.modules, {"serial": SimpleNamespace(Serial=lambda: port)}),
              redirect_stdout(io.StringIO()) as output):
            result = reader.main(args)
        return result, proof, port, output.getvalue()

    def test_full_active_cli_with_fake_serial_http_and_reset_only(self):
        import json
        for phase, history in (("restore", 0), ("restore", 2), ("fallback", 0), ("fallback", 2)):
            with self.subTest(phase=phase, history=history), tempfile.TemporaryDirectory() as directory:
                result, proof, port, output = self.active_run(directory, phase=phase, history=history)
                self.assertEqual(result, 0, output)
                self.assertTrue(port.opened and port.closed)
                saved = json.loads(proof.read_text())
                self.assertEqual(saved["result"], "PASS")
                self.assertEqual(saved["phase"], phase)
                self.assertEqual(saved["expected_baseline_history"], history)
                self.assertEqual(saved["sent_history_verified"], list(reader.KINDS))
                self.assertTrue(saved["released_status_seen"])
                self.assertNotIn("sending to", proof.read_text())
                self.assertEqual("NOT cross-upload history retention" in output, phase == "fallback")

    def test_active_history_failure_stays_fail_without_retries(self):
        import json
        def mutate(path, body, result):
            if "/archive/" in path:
                raise TimeoutError("secret HTTP details")
        with tempfile.TemporaryDirectory() as directory:
            result, proof, port, output = self.active_run(directory, api_change=mutate)
            self.assertEqual(result, 1)
            self.assertEqual(json.loads(proof.read_text())["result"], "FAIL")
            self.assertTrue(port.closed)
            self.assertNotIn("secret", output + proof.read_text())

    def test_serial_failure_or_interrupt_saves_non_pass(self):
        import json
        for failure, result_code, result_name in (("serial", 1, "FAIL"), ("interrupt", 130, "INTERRUPTED")):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as directory:
                result, proof, port, output = self.active_run(directory, failure=failure)
                self.assertEqual(result, result_code)
                self.assertEqual(json.loads(proof.read_text())["result"], result_name)
                self.assertTrue(port.closed)
                self.assertNotIn("secret", output + proof.read_text())

    def test_existing_proof_never_opens_serial_or_overwrites(self):
        with tempfile.TemporaryDirectory() as directory:
            proof = Path(directory) / "proof.json"
            proof.write_text("original")
            args = ["--phase", "restore", "--previous-boot", "11", "--reset", "--allow-persistent-writes",
                    "--send-recovery-commands", "--output", str(proof)]
            serial_mock = SimpleNamespace(Serial=unittest.mock.Mock())
            with (patch.object(reader, "JsonHttp", return_value=FakeApi()),
                  patch.dict(sys.modules, {"serial": serial_mock}), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit)):
                reader.main(args)
            serial_mock.Serial.assert_not_called()
            self.assertEqual(proof.read_text(), "original")


if __name__ == "__main__":
    unittest.main()
