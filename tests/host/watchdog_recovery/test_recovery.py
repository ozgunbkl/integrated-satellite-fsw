from pathlib import Path
import base64
import struct
import sys
import threading
import unittest
from urllib.error import HTTPError

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from watchdog_recovery_check import (RecoveryEvidence, ModeSender, validate_mode_binary,
    BOOT, SOFTWARE, RESOURCES, CORE, ARM, NOMINAL, PROFILE, CLEAR, CRC, ROUTE, ACK, NACK)


def state(stage, mode, active=0, revision=0):
    return f"TEST: WDT recovery stage={stage} mode={mode} active={active} revision={revision} count={active} uncertain=0 overflow=0 feed=ok."


def trace():
    events = [(0, BOOT), (.1, "WATCHDOG: SDK recovery configured timeout=15000 idle_mask=3 panic=1 reset_reason=3."),
        (.2, PROFILE), (1, RESOURCES), (2, ARM), (2.1, CORE), (2.2, SOFTWARE),
        (2.3, "TEST: WDT stack_free_min_bytes=1400."), (3, "TEST: WDT recovery drop=1 target=0."),
        (3.1, state(0, 0)), (12, NOMINAL), (20, "[20] FDIR: Fault from Source 4 (Severity: 2) Code: 0xC200"),
        (20.1, "WATCHDOG: !!! CRITICAL FAILURE: Task 0 timed out !!! Cause retained; no task-triggered reset."),
        (20.2, "TEST: WDT recovery resume=1."), (20.3, state(1, 1, 1, 11)),
        (21, CRC), (21.1, ROUTE), (21.2, NACK), (21.3, "verified:blocked"),
        (55.3, CLEAR), (55.4, "TEST: WDT recovery ready_ground=1."), (55.5, state(2, 1)),
        (56, CRC), (56.1, ROUTE), (56.2, ACK), (56.3, "verified:first"),
        (60, "TEST: WDT recovery operating=1 mode=2."), (60.1, state(3, 2)),
        (61, "TEST: WDT recovery drop=2 target=0."),
        (80, "[80] FDIR: Fault from Source 4 (Severity: 2) Code: 0xC200"),
        (80.1, "WATCHDOG: !!! CRITICAL FAILURE: Task 0 timed out !!! Cause retained; no task-triggered reset."),
        (80.2, "TEST: WDT recovery resume=2."), (80.3, state(4, 1, 1, 12)),
        (115.3, CLEAR), (115.4, "TEST: WDT recovery ready_ground=2."), (115.5, state(5, 1)),
        (116, CRC), (116.1, ROUTE), (116.2, ACK), (116.3, "verified:second"),
        (120, "TEST: WDT recovery operating=2 mode=3."), (120.1, state(6, 3)),
        (122, NOMINAL), (125, state(6, 3)), (127, NOMINAL), (130, state(6, 3)),
        (132, NOMINAL), (135, state(6, 3))]
    return events


def feed(events):
    evidence = RecoveryEvidence()
    for when, line in events:
        if line.startswith("verified:"): evidence.verified.append(line.split(":")[1])
        else: evidence.observe(line, when)
    return evidence


class EvidenceTests(unittest.TestCase):
    def test_complete(self):
        evidence = feed(trace())
        self.assertTrue(evidence.complete())
        self.assertEqual(evidence.requests, ["blocked", "first", "second"])
        self.assertEqual(evidence.revisions, [11, 12])

    def test_missing_proof(self):
        for missing in ("verified:first", CRC, NOMINAL):
            events = [(t, s) for t, s in trace() if s != missing]
            try: evidence = feed(events)
            except RuntimeError: continue
            self.assertFalse(evidence.complete())

    def test_no_post_recovery_mission(self):
        self.assertFalse(feed([(t, s) for t, s in trace() if not (s == NOMINAL and t > 120)]).complete())

    def test_initial_loss_may_precede_first_nominal_log(self):
        # Normal baseline is a separate image/run. Immediate startup loss may
        # win over the lower-priority first NOMINAL print; final mission is required.
        self.assertTrue(feed([(t, s) for t, s in trace() if not (s == NOMINAL and t < 20)]).complete())

    def test_fault_still_requires_complete_startup(self):
        events = trace()[:4] + [(3, "TEST: WDT recovery drop=1 target=0."),
            (20, "FDIR: Fault from Source 4 (Severity: 2) Code: 0xC200")]
        with self.assertRaises(RuntimeError): feed(events)

    def test_no_premature_clear(self):
        events = trace()
        events[18] = (30, CLEAR)
        with self.assertRaises(RuntimeError): feed(events)

    def test_reject_auto_mode(self):
        with self.assertRaises(RuntimeError): feed(trace()[:18] + [(22, ACK)])

    def test_reject_changed_mode_on_clear(self):
        with self.assertRaises(RuntimeError): feed(trace()[:20] + [(55.5, state(2, 2))])

    def test_new_revision_required(self):
        events = [(t, state(4, 1, 1, 11) if s == state(4, 1, 1, 12) else s) for t, s in trace()]
        with self.assertRaises(RuntimeError): feed(events)

    def test_reject_other_fault_and_timeout(self):
        for line in ("FDIR: Fault from Source 3 (Severity: 2) Code: 0xC200", "FDIR: Fault from Source 4 (Severity: 3) Code: 0xC200",
                     "WATCHDOG: !!! CRITICAL FAILURE: Task 1 timed out !!!"):
            with self.subTest(line=line), self.assertRaises(RuntimeError): feed(trace()[:11] + [(19, line)])

    def test_panic_reset_bad_stack(self):
        for line in (BOOT, "Guru Meditation", "Task watchdog got triggered", "abort()", "TEST ERROR: bad",
                     "TEST: WDT stack_free_min_bytes=511.", "x" * 1025):
            with self.subTest(line=line), self.assertRaises(RuntimeError): feed(trace()[:11] + [(19, line)])

    def test_foreign_or_duplicate_profile(self):
        for line in (PROFILE, "TEST: WDT profile=task_loss target=0."):
            with self.assertRaises(RuntimeError): feed(trace()[:11] + [(19, line)])

    def test_bad_ledger_or_phase(self):
        for line in (state(3, 2), state(1, 1, 1, 0), state(1, 2, 1, 11), state(1, 1, 1, 11).replace("uncertain=0", "uncertain=1")):
            with self.assertRaises(RuntimeError): feed(trace()[:14] + [(20.3, line)])

    def test_startup_and_clock(self):
        with self.assertRaises(RuntimeError): RecoveryEvidence().observe(ARM, 0)
        evidence = RecoveryEvidence(); evidence.observe("ignored", 2)
        with self.assertRaises(RuntimeError): evidence.observe("ignored", 1)

    def test_demo_crc_not_counted_as_ground(self):
        events = trace()
        events[11:11] = [(13, CRC), (13.1, "CDHS: Routing packet to EPS...")]
        self.assertTrue(feed(events).complete())

    def test_ignored_text_not_retained(self):
        evidence = RecoveryEvidence(); evidence.observe("private arbitrary text", 0)
        self.assertNotIn("private arbitrary text", repr(vars(evidence)))

    def test_console_contract_matches_production_sources(self):
        root = Path(__file__).resolve().parents[3]
        contracts = {"components/cdh/tc_proc.c": [CRC.replace("18", "%d")],
            "components/cdh/cdhs_router.c": [ROUTE], "components/cdh/tm_gen.c": [NOMINAL],
            "components/watchdog/watchdog.c": [CLEAR.replace("Task 0", "Task %u"), SOFTWARE,
                ARM.replace("7 critical", "%u critical")],
            "components/fdir/fdir_service.c": [ACK.replace("NOMINAL", "%s"), NACK.replace("NOMINAL", "%s")]}
        for relative, templates in contracts.items():
            source = (root / relative).read_text(encoding="utf-8")
            for template in templates:
                self.assertIn('"' + template + r'\n"', source, (relative, template))

    def test_smp_status_can_precede_handler_ack_and_drop_can_precede_status(self):
        events = []
        for when, line in trace():
            if line == ACK: when = 61.1 if when < 100 else 120.2
            if line == "TEST: WDT recovery drop=2 target=0.": when = 59.9
            events.append((when, line))
        self.assertTrue(feed(sorted(events, key=lambda item: item[0])).complete())


def binary(post=False):
    return struct.pack("!3H", 0x1030, 0xC000, 1 if post else 0) + b"\x52\x02"


class FakeApi:
    def __init__(self):
        self.now = self.posts = 0
        self.history_calls = 0
        self.bad = None
    def clock(self): self.now += .02; return self.now
    def sleep(self, delay): self.now += delay
    def __call__(self, path, body=None, timeout=4):
        if "/links/" in path:
            outgoing = path.endswith("udp-out")
            return {"status": "NOK" if self.bad == "link" else "OK", "type": "org.yamcs.tctm." + ("UdpTcDataLink" if outgoing else "UdpTmDataLink"),
                    "detailedStatus": "sending to " + ("192.168.1.50" if self.bad == "target" else "192.168.1.5") + ":10025"}
        identity = {"id": "known-command", "commandName": "/myproject/FDIR_SetMode", "generationTime": "2026-10-16T15:00:00Z",
                    "unprocessedBinary": base64.b64encode(binary()).decode()}
        if body is not None:
            assert body["args"] == {"CmdParam": 2}
            if body["dryRun"]:
                if self.bad == "dry": identity["unprocessedBinary"] = base64.b64encode(binary()[:-1] + b"\x01").decode()
                return identity
            self.posts += 1
            if self.bad == "post_lost": raise TimeoutError("accepted but reply lost")
            return identity
        self.history_calls += 1
        if self.bad == "not_yet" and self.history_calls == 1: raise HTTPError(path, 404, "pending", {}, None)
        attrs = [{"name": "Acknowledge_Sent_Status", "value": {"stringValue": "NOK" if self.bad == "rejected" else "OK"}},
                 {"name": "binary", "value": {"binaryValue": base64.b64encode(binary(True) if self.bad != "wire" else binary()).decode()}}]
        if self.bad == "duplicate": attrs.append(attrs[0])
        if self.bad == "identity": identity["id"] = "another-command"
        return {**identity, "attr": attrs}


class SenderTests(unittest.TestCase):
    def run_sender(self, api, stop=None):
        return ModeSender(api, clock=api.clock, sleep=api.sleep).send(1, stop or threading.Event())
    def test_valid_fixed_request(self):
        api = FakeApi(); self.assertEqual(self.run_sender(api), "known-command"); self.assertEqual(api.posts, 1)
    def test_eventual_history_only(self):
        api = FakeApi(); api.bad = "not_yet"; self.run_sender(api)
        self.assertEqual(api.posts, 1); self.assertEqual(api.history_calls, 2)
    def test_fail_before_send(self):
        for bad in ("link", "target", "dry"):
            api = FakeApi(); api.bad = bad
            with self.subTest(bad=bad), self.assertRaises(ValueError): self.run_sender(api)
            self.assertEqual(api.posts, 0)
    def test_bad_sent_proof(self):
        for bad in ("rejected", "wire", "duplicate", "identity"):
            api = FakeApi(); api.bad = bad
            with self.subTest(bad=bad), self.assertRaises((RuntimeError, ValueError)): self.run_sender(api)
            self.assertEqual(api.posts, 1)
    def test_post_not_retried(self):
        api = FakeApi(); api.bad = "post_lost"
        with self.assertRaises(TimeoutError): self.run_sender(api)
        self.assertEqual(api.posts, 1)
    def test_cancel_before_post(self):
        api = FakeApi(); stop = threading.Event(); stop.set()
        with self.assertRaises(RuntimeError): self.run_sender(api, stop)
        self.assertEqual(api.posts, 0)
    def test_invalid_sequence(self):
        for sequence in (0, 4, True):
            with self.assertRaises(ValueError): ModeSender(FakeApi()).send(sequence, threading.Event())
    def test_binary_guards(self):
        validate_mode_binary(binary()); validate_mode_binary(binary(True), True)
        for raw in (binary() + b"x", b"", binary()[:-1] + b"\x01", b"\x10\x60" + binary()[2:]):
            with self.assertRaises(ValueError): validate_mode_binary(raw)


if __name__ == "__main__": unittest.main()
