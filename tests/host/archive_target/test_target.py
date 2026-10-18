"""No real sockets/board: target-check orchestration, legacy proof and loss adapter."""
import base64
from contextlib import redirect_stderr, redirect_stdout
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
from urllib import parse

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests/host/archive_client"))
from test_client import FakeYamcs
import archive_target_check as target
import yamcs_archive_transfer as client
import archive_transfer_wire as wire


class Model(FakeYamcs):
    def __init__(self):
        super().__init__()
        self.bad_legacy_dry = self.bad_legacy_crc = self.conflicting_legacy = False
        self.drop_legacy_reply = False

    def __call__(self, path, body=None, timeout=4):
        name = parse.unquote(path.rsplit("/commands/", 1)[-1])
        if path.startswith("/api/processors/") and name.endswith("/ARCHIVE_GET_BY_INDEX"):
            index = body["args"]["RecordIndex"]
            raw = struct.pack("!BH", 0x30, index)
            pre = struct.pack("!3H", 0x1060, 0xC000, 0) + raw
            if self.bad_legacy_dry:
                pre = pre[:-1] + bytes([pre[-1] ^ 1])
            result = dict(commandName=name, unprocessedBinary=base64.b64encode(pre).decode())
            if body["dryRun"]:
                return result
            self.sent.append(raw)
            self.counter += 1
            ident = str(self.counter)
            result.update(id=ident, generationTime=self.time)
            self.histories[ident] = dict(id=ident, commandName=name, attr=[
                dict(name="Acknowledge_Sent_Status", value=dict(stringValue="OK")),
                dict(name="binary", value=dict(binaryValue=base64.b64encode(wire.tc_packet(raw, self.counter)).decode()))])
            record = self.records[index]
            data = bytes.fromhex(record["payload"])
            self.sequence += 1
            reply_body = struct.pack("!B4H", 1, index, record["record_id"], len(data), record["crc"]) + data
            reply = struct.pack("!3H", 96, 0xC000 | self.sequence, len(reply_body) - 1) + reply_body
            if self.bad_legacy_crc:
                reply = reply[:-1] + bytes([reply[-1] ^ 1])
            self.packets.append(dict(packet=base64.b64encode(reply).decode(), link="udp-in", generationTime=self.time))
            if self.drop_legacy_reply:
                self.packets.pop()
            if self.conflicting_legacy:
                modified = bytearray(reply)
                modified[10] ^= 1  # Different record ID, still a valid payload CRC.
                self.packets.append(dict(self.packets[-1], packet=base64.b64encode(modified).decode()))
            return result
        return super().__call__(path, body, timeout)


class TargetTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.export = self.root / "prefix.json"
        self.fake = Model()
        self.fake.append_on_read = True
        self.transport = client.YamcsTransport(self.fake, allow_send=True, timeout=0.3, retries=0,
                                               clock=self.fake.clock, sleep=self.fake.clock.sleep)
        client.download(self.transport, self.export, request_id=1234, progress=lambda _: None)

    def run_check(self):
        return target.check(self.transport, self.export, self.root, 3, confirm_clear=True, progress=lambda _: None)

    def test_full_scenario(self):
        report = self.run_check()
        self.assertEqual(report["result"], "PASS")
        self.assertEqual(report["tail_count"], 1)
        self.assertEqual(self.fake.records, [])
        self.assertIsNone(self.fake.active)
        self.assertTrue(all(Path(path).exists() for path in report["paths"].values()))

    def test_clear_requires_explicit_intent(self):
        before = len(self.fake.sent)
        with self.assertRaises(ValueError):
            target.check(self.transport, self.export, self.root, 3)
        self.assertEqual(len(self.fake.sent), before)

    def test_send_requires_explicit_intent(self):
        self.transport.allow_send = False
        with self.assertRaises(ValueError):
            self.run_check()

    def test_existing_proof_blocks_before_commands(self):
        (self.root / "prefix.tail-before.json").write_text("user file")
        before = len(self.fake.sent)
        with self.assertRaises(ValueError):
            self.run_check()
        self.assertEqual(len(self.fake.sent), before)

    def test_total_bounds_before_commands(self):
        before = len(self.fake.sent)
        for total in (2, 31, True):
            with self.subTest(total=total), self.assertRaises(ValueError):
                target.check(self.transport, self.export, self.root, total, confirm_clear=True)
        self.assertEqual(len(self.fake.sent), before)

    def test_missing_pending_does_not_confirm(self):
        self.fake.active = None
        with self.assertRaises(ValueError):
            self.run_check()
        self.assertFalse(any(raw[0] == wire.CONFIRM for raw in self.fake.sent))

    def test_bad_tail_crc_does_not_confirm(self):
        self.fake.bad_legacy_crc = True
        with self.assertRaises(ValueError):
            self.run_check()
        self.assertFalse(any(raw[0] == wire.CONFIRM for raw in self.fake.sent))

    def test_wrong_dry_binary_does_not_send_legacy(self):
        self.fake.bad_legacy_dry = True
        before = len(self.fake.sent)
        with self.assertRaises(ValueError):
            target.legacy_read(self.transport, 2)
        self.assertEqual(len(self.fake.sent), before)

    def test_conflicting_legacy_replies_fail(self):
        self.fake.conflicting_legacy = True
        with self.assertRaises(ValueError):
            target.legacy_read(self.transport, 2)

    def test_legacy_index_guard(self):
        before = len(self.fake.sent)
        for index in (-1, 30, True):
            with self.subTest(index=index), self.assertRaises(ValueError):
                target.legacy_read(self.transport, index)
        self.assertEqual(len(self.fake.sent), before)

    def test_already_archived_same_time_legacy_reply_not_reused(self):
        target.legacy_read(self.transport, 2)
        self.fake.drop_legacy_reply = True
        with self.assertRaises(TimeoutError):
            target.legacy_read(self.transport, 2)

    def test_corrupt_export_blocks_before_commands(self):
        self.export.write_bytes(b"broken export")
        before = len(self.fake.sent)
        with self.assertRaises(ValueError):
            self.run_check()
        self.assertEqual(len(self.fake.sent), before)

    def test_baseline_sync_failure_prevents_confirmation(self):
        with patch.object(target.os, "fsync", side_effect=OSError("sync failed")), self.assertRaises(OSError):
            self.run_check()
        self.assertFalse(any(raw[0] == wire.CONFIRM for raw in self.fake.sent))
        self.assertEqual(len(self.fake.records), 3)

    def test_response_loss_wrapper_restores_transport_on_failure(self):
        api, retries = self.transport.api, self.transport.retries

        def fail():
            raise ValueError("verification failed")

        with self.assertRaises(ValueError):
            target.with_response_loss(self.transport, wire.READ, fail)
        self.assertIs(self.transport.api, api)
        self.assertEqual(self.transport.retries, retries)

    def test_export_failure_stops_without_cancel_or_next_clear(self):
        with patch.object(target, "download", side_effect=OSError("disk full")), self.assertRaises(OSError):
            self.run_check()
        self.assertEqual(len(self.fake.records), 1)
        self.assertIsNotNone(self.fake.active)
        self.assertEqual(sum(raw[0] == wire.CANCEL for raw in self.fake.sent), 0)
        self.assertEqual(len({raw for raw in self.fake.sent if raw[0] == wire.CONFIRM}), 1)

    def test_default_dry_run_sends_nothing_and_writes_nothing(self):
        before, paths = len(self.fake.sent), set(self.root.iterdir())
        with patch.object(target, "JsonHttp", return_value=self.fake), redirect_stdout(io.StringIO()):
            target.main(["--export", str(self.export), "--output-dir", str(self.root)])
        self.assertEqual(len(self.fake.sent), before)
        self.assertEqual(set(self.root.iterdir()), paths)

    def test_invalid_flags_stop_before_http(self):
        for flag in ("--send", "--confirm-clear"):
            with patch.object(target, "JsonHttp") as api, redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                target.main(["--export", str(self.export), "--output-dir", str(self.root), flag])
            api.assert_not_called()

    def test_retry_changed_arguments_blocked_before_post(self):
        calls = []
        name = "/myproject/ARCHIVE_READ_TRANSFER"

        def api(path, body, timeout):
            calls.append(body)
            return dict(id="one", commandName=name)

        loss = target.AcceptedResponseLoss(api, name)
        path = "/api/processors/myproject/realtime/commands/" + parse.quote(name, safe="")
        with self.assertRaises(TimeoutError):
            loss(path, dict(dryRun=False, args={"index": 0}))
        with self.assertRaises(ValueError):
            loss(path, dict(dryRun=False, args={"index": 1}))
        self.assertEqual(len(calls), 1)

    def test_request_collision_bound(self):
        with patch.object(target.secrets, "randbits", return_value=7) as random, self.assertRaises(RuntimeError):
            target.next_request({7})
        self.assertEqual(random.call_count, 8)


if __name__ == "__main__":
    unittest.main()
