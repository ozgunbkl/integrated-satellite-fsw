"""No sockets/card: actual client/HTTP request serialization with a bounded fake Yamcs/board."""
import base64
import binascii
from contextlib import redirect_stderr, redirect_stdout
from datetime import datetime, timezone
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
from urllib import error, parse

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import archive_transfer as export
import archive_transfer_wire as wire
import yamcs_archive_transfer as client


class Clock:
    def __init__(self): self.now = 0
    def __call__(self): return self.now
    def sleep(self, seconds): self.now += seconds


def record(index, ident, payload):
    return dict(index=index, record_id=ident, length=len(payload), crc=binascii.crc_hqx(payload, 0xFFFF), payload=payload.hex())


def binary_args(name, args):
    """Independent reference encoding of the installed XTCE's five public commands."""
    op = {"BEGIN": 0x31, "PENDING": 0x32, "READ": 0x33, "CONFIRM": 0x34, "CANCEL": 0x35}[name.split("_")[-2]]
    if op == 0x31: return struct.pack("!BI", op, args["RequestId"])
    if op == 0x32: return bytes([op])
    manifest = struct.pack("!QIIIH", int(args["BootId"]), args["TransferId"], args["RequestId"], args["Digest"], args["RecordCount"])
    return bytes([op]) + manifest + (struct.pack("!H", args["RecordIndex"]) if op == 0x33 else bytes([args["ConfirmKey"]]) if op == 0x34 else b"")


class FakeYamcs:
    def __init__(self):
        self.clock = Clock()
        self.records = [record(0, 0x1234, b""), record(1, 0xABCD, b"123456789")]
        self.active, self.receipt = None, None
        self.reads = set()
        self.transfer_id = 0
        self.histories, self.packets, self.sent, self.calls = {}, [], [], []
        self.drop, self.http_lost = {}, {}
        self.wrong_target = self.bad_binary = self.wrong_dry = False
        self.no_history = self.wrong_history_id = self.no_reply = self.corrupt = False
        self.cycle_token = self.old_alias = self.nok = self.bad_link = False
        self.other_manifest = self.old_timestamp = self.inconsistent = False
        self.append_on_read = self.appended = False
        self.sequence = self.counter = 0
        self.time = "2026-10-15T10:00:00Z"

    def _manifest(self, body):
        boot, tid, req, digest, count = struct.unpack_from("!QIIIH", body, 1)
        return export.Transfer(boot, tid, req, digest, count)

    def _packet(self, op, status, transfer, index=0xFFFF, value=None):
        if transfer is None: transfer = export.Transfer(0, 0, 0, 0, 0)  # Error response fields may be zero.
        payload = bytes.fromhex(value["payload"]) if value else b""
        body = struct.pack("!BBBQIIIH4H", 1, op, status, transfer.boot_id, transfer.transfer_id,
            transfer.request_id, transfer.digest, transfer.record_count, index,
            value["record_id"] if value else 0, len(payload), value["crc"] if value else 0) + payload
        self.sequence += 1
        raw = struct.pack("!3H", 97, 0xC000 | self.sequence, len(body) - 1) + body
        if self.corrupt and op == 0x33: raw = raw[:-1] + bytes([raw[-1] ^ 1])
        if self.other_manifest:
            changed = bytearray(raw); changed[9] ^= 1; raw = bytes(changed)
        self.packets.append(dict(packet=base64.b64encode(raw).decode(), link="udp-in",
                                 generationTime="2026-10-14T00:00:00Z" if self.old_timestamp else self.time))
        if self.inconsistent and op == 0x33:
            changed = bytearray(raw); changed[6 + 27] ^= 1  # Different ID, same valid payload CRC.
            self.packets.append(dict(self.packets[-1], packet=base64.b64encode(changed).decode()))

    def _process(self, body):
        op, status, index, value = body[0], 0, 0xFFFF, None
        transfer = None
        if op == 0x31:
            req = struct.unpack_from("!I", body, 1)[0]
            transfer = export.Transfer(0, 0, req, 0, 0)
            if not self.records: status = 5
            elif self.active and self.active.request_id != req: status = 7
            elif self.active: transfer = self.active
            else:
                self.transfer_id += 1
                self.active = export.Transfer(0xFEDCBA9876543210, self.transfer_id, req,
                    export.digest_records(self.records), len(self.records))
                transfer = self.active; self.reads = set()
        elif op == 0x32:
            transfer = self.active
            if transfer is None: status = 4
        else:
            transfer = self._manifest(body)
            if op == 0x33: index = struct.unpack_from("!H", body, 23)[0]
            if op == 0x34 and self.receipt == (transfer, "confirmed"): pass
            elif op == 0x35 and self.receipt == (transfer, "cancelled"): pass
            elif transfer != self.active: status = 8
            elif op == 0x33:
                index = struct.unpack_from("!H", body, 23)[0]
                if index >= transfer.record_count: status = 4
                else:
                    value = self.records[index]; self.reads.add(index)
                    if self.append_on_read and not self.appended:
                        self.records.append(record(2, 0xEEEE, b"new")); self.appended = True
            elif op == 0x34:
                if self.reads != set(range(transfer.record_count)): status = 9
                else:
                    self.records = self.records[transfer.record_count:]
                    for i, item in enumerate(self.records): item["index"] = i
                    self.receipt = (transfer, "confirmed"); self.active = None
            else:
                self.receipt = (transfer, "cancelled"); self.active = None
        if self.no_reply: return
        if self.drop.get(op, 0): self.drop[op] -= 1; return
        self._packet(op, status, transfer, index, value)

    def __call__(self, path, body=None, timeout=4):
        self.calls.append((path, body))
        parsed = parse.urlsplit(path)
        if parsed.path.startswith("/api/links/"):
            outgoing = parsed.path.endswith("udp-out")
            return dict(disabled=self.bad_link, status="OK", type="org.yamcs.tctm." + ("UdpTcDataLink" if outgoing else "UdpTmDataLink"),
                        detailedStatus="sending to " + ("192.168.1.50" if self.wrong_target else "192.168.1.5") + ":10025")
        if parsed.path.startswith("/api/processors/"):
            name = parse.unquote(parsed.path.split("/commands/", 1)[1])
            raw = binary_args(name, body["args"])
            pre = struct.pack("!3H", 0x1060, 0xC000, 0) + raw
            result = dict(commandName=name, unprocessedBinary=base64.b64encode(pre).decode())
            if body["dryRun"]:
                if self.wrong_dry: result["unprocessedBinary"] = base64.b64encode(pre[:-1] + b"\xFF").decode()
                return result
            self.sent.append(raw); self.counter += 1
            ident = str(self.counter); result.update(id=ident, generationTime=self.time)
            post = wire.tc_packet(raw, self.counter)
            if self.bad_binary: post = post[:-1] + bytes([post[-1] ^ 1])
            self.histories[ident] = dict(id="wrong" if self.wrong_history_id else ident, commandName=name,
                attr=[dict(name="Acknowledge_Sent_Status", value=dict(stringValue="NOK" if self.nok else "OK")),
                      dict(name="binary", value=dict(binaryValue=base64.b64encode(post).decode()))])
            self._process(raw)
            if self.http_lost.get(raw[0], 0): self.http_lost[raw[0]] -= 1; raise TimeoutError("POST response lost after processing")
            return result
        if "/commands/" in parsed.path:
            if self.no_history: raise error.HTTPError(path, 404, "not published", {}, None)
            return self.histories[parse.unquote(parsed.path.rsplit("/", 1)[1])]
        if parsed.path.endswith("/packets"):
            query = parse.parse_qs(parsed.query)
            offset = int(query.get("next", ["0"])[0])
            items = [item for item in self.packets if "start" not in query or client.timestamp(item["generationTime"]) >= client.timestamp(query["start"][0])]
            items = sorted(items, key=lambda item: client.timestamp(item["generationTime"]), reverse=query["order"][0] == "desc")
            page = {"packet" if self.old_alias else "packets": items[offset:offset + 100]}
            if offset + 100 < len(items): page["continuationToken"] = str(offset + 100)
            if self.cycle_token: page["continuationToken"] = "0"
            return page
        raise AssertionError("Unexpected API call " + path)


class ClientTests(unittest.TestCase):
    def setUp(self):
        self.fake = FakeYamcs()
        self.transport = client.YamcsTransport(self.fake, allow_send=True, timeout=0.3, retries=1,
                                               clock=self.fake.clock, sleep=self.fake.clock.sleep)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "export.json"

    def download(self, **kwargs):
        return client.download(self.transport, self.path, request_id=1234, progress=lambda msg: None, **kwargs)

    def test_dry_run_zero_board_commands(self):
        transport = client.YamcsTransport(self.fake)
        transport.preview(wire.begin_command(1)); transport.preview(wire.pending_command())
        with self.assertRaises(client.TransferError): transport.exchange(wire.begin_command(1))
        self.assertEqual(self.fake.sent, [])

    def test_default_save_retains_records_and_session(self):
        result = self.download()
        self.assertEqual(result["outcome"], "saved-retained")
        self.assertEqual(len(self.fake.records), 2)
        self.assertIsNotNone(self.fake.active)
        self.assertEqual([b[0] for b in self.fake.sent], [0x31, 0x33, 0x33])
        self.assertEqual(export.load_verified_export(self.path), result["saved"].transfer)
        packet_queries = [parse.parse_qs(parse.urlsplit(path).query) for path, _ in self.fake.calls if "/packets?" in path]
        self.assertTrue(packet_queries)
        self.assertTrue(all(q.get("name") == ["/myproject/ArchiveTransferContainer"] and q.get("link") == ["udp-in"] for q in packet_queries))
        args = next(body["args"] for path, body in self.fake.calls if body and "BootId" in body["args"])
        self.assertIsInstance(args["BootId"], str)
        self.assertEqual(args["BootId"], str(0xFEDCBA9876543210))

    def test_confirm_only_after_save_retains_tail(self):
        self.fake.append_on_read = True
        def guard(body, **kwargs):
            if body[0] == wire.CONFIRM:
                self.assertTrue(self.path.exists())
                self.assertEqual(export.load_verified_export(self.path), kwargs["saved"].transfer)
            return exchange(body, **kwargs)
        exchange = self.transport.exchange
        with patch.object(self.transport, "exchange", side_effect=guard): result = self.download(confirm_clear=True)
        self.assertEqual(result["outcome"], "confirmed")
        self.assertEqual([item["record_id"] for item in self.fake.records], [0xEEEE])

    def test_dropped_read_reply_retries_exact_same_body(self):
        self.fake.drop[0x33] = 1
        self.download()
        reads = [body for body in self.fake.sent if body[0] == 0x33]
        self.assertEqual(reads[0], reads[1])
        self.assertFalse(any(b[0] == 0x34 for b in self.fake.sent))

    def test_lost_begin_http_response_is_idempotent(self):
        self.fake.http_lost[0x31] = 1
        self.download()
        self.assertEqual(self.fake.sent[0], self.fake.sent[1])
        self.assertEqual(self.fake.transfer_id, 1)

    def test_lost_confirm_receipt_does_not_clear_tail_twice(self):
        self.fake.drop[0x34] = 1; self.fake.append_on_read = True
        self.assertEqual(self.download(confirm_clear=True)["outcome"], "confirmed")
        confirmations = [body for body in self.fake.sent if body[0] == 0x34]
        self.assertEqual(confirmations[0], confirmations[1])
        self.assertEqual(len(self.fake.records), 1)

    def test_lost_confirm_http_response(self):
        self.fake.http_lost[0x34] = 1; self.fake.append_on_read = True
        self.assertEqual(self.download(confirm_clear=True)["outcome"], "confirmed")
        self.assertEqual(len(self.fake.records), 1)

    def test_unanswered_command_retry_bound(self):
        self.fake.no_reply = True
        with self.assertRaises(client.OutcomeUnknown): self.download()
        self.assertEqual(len(self.fake.sent), 2)
        self.assertFalse(self.path.exists())

    def test_sent_history_alone_is_not_board_confirmation(self):
        self.fake.no_reply = True
        with self.assertRaises(client.OutcomeUnknown): self.download(confirm_clear=True)
        self.assertEqual([body[0] for body in self.fake.sent], [0x31, 0x31])

    def test_board_reply_without_history_not_enough(self):
        self.fake.no_history = True
        with self.assertRaises(client.OutcomeUnknown): self.download()
        self.assertFalse(self.path.exists())

    def test_wrong_target_blocks_before_issue(self):
        self.fake.wrong_target = True
        with self.assertRaises(client.TransferError): self.download()
        self.assertEqual(self.fake.sent, [])

    def test_disabled_link_blocks_before_issue(self):
        self.fake.bad_link = True
        with self.assertRaises(client.TransferError): self.download()
        self.assertEqual(self.fake.sent, [])

    def test_dry_binary_mismatch_blocks_before_issue(self):
        self.fake.wrong_dry = True
        with self.assertRaises(ValueError): self.download()
        self.assertEqual(self.fake.sent, [])

    def test_postprocessed_binary_mismatch_stops(self):
        self.fake.bad_binary = True
        with self.assertRaises(ValueError): self.download(confirm_clear=True)
        self.assertEqual(len(self.fake.sent), 1)

    def test_history_identity_mismatch_stops(self):
        self.fake.wrong_history_id = True
        with self.assertRaises(ValueError): self.download()

    def test_sent_nok_stops(self):
        self.fake.nok = True
        with self.assertRaises(client.TransferError): self.download()
        self.assertEqual(len(self.fake.sent), 1)

    def test_corrupt_record_no_export_or_confirm(self):
        self.fake.corrupt = True
        with self.assertRaises(ValueError): self.download(confirm_clear=True)
        self.assertFalse(self.path.exists())
        self.assertFalse(any(body[0] == 0x34 for body in self.fake.sent))

    def test_different_manifest_ignored_not_confirmed(self):
        self.fake.other_manifest = True
        with self.assertRaises(client.OutcomeUnknown): self.download(confirm_clear=True)
        self.assertFalse(self.path.exists())

    def test_inconsistent_duplicate_records_stop(self):
        self.fake.inconsistent = True
        with self.assertRaises(ValueError): self.download(confirm_clear=True)
        self.assertFalse(any(body[0] == 0x34 for body in self.fake.sent))

    def test_pagination_preserves_all_filters(self):
        self.fake.packets = [dict(packet=base64.b64encode(b"\0\x05\xC0\0\0\0X").decode(), link="udp-in", generationTime=self.fake.time)] * 205
        self.download()
        queries = [parse.parse_qs(parse.urlsplit(path).query) for path, _ in self.fake.calls if "next=" in path]
        self.assertTrue(queries)
        for query in queries:
            if query["order"] == ["asc"]: self.assertEqual(query["start"], [self.fake.time])
            else: self.assertNotIn("start", query)
            self.assertEqual(query["link"], ["udp-in"]); self.assertEqual(query["limit"], ["100"])

    def test_repeated_page_token_stops(self):
        self.fake.cycle_token = True
        with self.assertRaises(ValueError): self.download(confirm_clear=True)
        self.assertFalse(any(body[0] == 0x34 for body in self.fake.sent))

    def test_old_packet_alias_supported(self):
        self.fake.old_alias = True
        self.assertEqual(self.download()["outcome"], "saved-retained")

    def test_old_generation_time_not_accepted(self):
        self.fake.old_timestamp = True
        with self.assertRaises(client.OutcomeUnknown): self.download()

    def test_existing_output_never_overwritten_or_commanded(self):
        self.path.write_bytes(b"user file")
        with self.assertRaises(ValueError): self.download(confirm_clear=True)
        self.assertEqual(self.path.read_bytes(), b"user file"); self.assertEqual(self.fake.sent, [])

    def test_file_failure_no_confirm_or_automatic_cancel(self):
        with patch.object(client, "save_verified_export", side_effect=OSError("disk full")):
            with self.assertRaises(OSError): self.download(confirm_clear=True)
        self.assertEqual([b[0] for b in self.fake.sent], [0x31, 0x33, 0x33])
        self.assertEqual(len(self.fake.records), 2); self.assertIsNotNone(self.fake.active)

    def test_resume_pending_after_failed_export(self):
        with patch.object(client, "save_verified_export", side_effect=OSError("disk")):
            with self.assertRaises(OSError): self.download()
        self.assertEqual(client.download(self.transport, self.path, resume_pending=True, progress=lambda msg: None)["outcome"], "saved-retained")
        self.assertEqual(len([b for b in self.fake.sent if b[0] == 0x31]), 1)

    def test_busy_does_not_cancel_other_session(self):
        self.transport.exchange(wire.begin_command(999))
        with self.assertRaisesRegex(client.BoardRejected, "BUSY"): self.download()
        self.assertEqual(self.fake.active.request_id, 999)

    def test_empty_no_file_or_confirm(self):
        self.fake.records = []
        self.assertEqual(self.download(confirm_clear=True)["outcome"], "empty")
        self.assertFalse(self.path.exists()); self.assertEqual([b[0] for b in self.fake.sent], [0x31])

    def test_confirm_saved_file_later_and_repeat(self):
        self.download()
        self.assertEqual(client.control_export(self.transport, self.path, confirm_clear=True)["outcome"], "confirmed")
        self.assertEqual(client.control_export(self.transport, self.path, confirm_clear=True)["outcome"], "confirmed")
        self.assertEqual(self.fake.records, [])

    def test_cancel_saved_file_retains_records(self):
        self.download()
        self.assertEqual(client.control_export(self.transport, self.path, cancel=True)["outcome"], "cancelled-retained")
        self.assertEqual(len(self.fake.records), 2); self.assertIsNone(self.fake.active)

    def test_tampered_saved_file_no_confirmation_issue(self):
        self.download(); sent = len(self.fake.sent)
        self.path.write_bytes(b"{}")
        with self.assertRaises(ValueError): client.control_export(self.transport, self.path, confirm_clear=True)
        self.assertEqual(len(self.fake.sent), sent)

    def test_stale_saved_manifest_is_not_replaced(self):
        self.download(); self.fake.active = None
        with self.assertRaisesRegex(client.BoardRejected, "STALE"):
            client.control_export(self.transport, self.path, confirm_clear=True)
        self.assertEqual(len(self.fake.records), 2)

    def test_confirmation_unknown_explicit_warning(self):
        self.download(); self.fake.drop[0x34] = 2
        with self.assertRaisesRegex(client.OutcomeUnknown, "may already have committed"):
            client.control_export(self.transport, self.path, confirm_clear=True)
        self.assertEqual(self.fake.records, [])  # No false claim that a missing receipt means retained.

    def test_fatal_verification_after_confirm_warns_unknown(self):
        self.download(); self.fake.bad_binary = True
        with self.assertRaisesRegex(client.OutcomeUnknown, "may already have committed"):
            client.control_export(self.transport, self.path, confirm_clear=True)
        self.assertEqual(self.fake.records, [])

    def test_resume_missing_session_no_new_begin(self):
        with self.assertRaisesRegex(client.TransferError, "No pending"):
            client.download(self.transport, self.path, resume_pending=True, progress=lambda msg: None)
        self.assertEqual([body[0] for body in self.fake.sent], [0x32])

    def test_cancel_then_confirm_stale_without_reopening(self):
        self.download(); client.control_export(self.transport, self.path, cancel=True)
        with self.assertRaises(client.BoardRejected): client.control_export(self.transport, self.path, confirm_clear=True)
        self.assertEqual(len([b for b in self.fake.sent if b[0] == 0x31]), 1)
        self.assertEqual(len(self.fake.records), 2)

    def test_control_without_intent_does_not_sync_or_send(self):
        with patch.object(client, "resync_verified_export") as sync:
            with self.assertRaises(ValueError): client.control_export(self.transport, self.path)
            sync.assert_not_called()
        self.assertEqual(self.fake.sent, [])

    def test_page_bound_not_silently_first_hundred(self):
        self.fake.packets = [dict(packet=base64.b64encode(b"\0\x05\xC0\0\0\0X").decode(), link="udp-in", generationTime=self.fake.time)] * 1000
        with self.assertRaisesRegex(client.TransferError, "page bound"):
            self.download(confirm_clear=True)
        self.assertFalse(self.path.exists())

    def test_transient_http_retry_and_auth_fail_fast(self):
        api = self.transport.api
        failed = []
        def transient(path, body=None, timeout=4):
            if not failed:
                failed.append(True); raise error.HTTPError(path, 503, "temporarily unavailable", {}, None)
            return api(path, body, timeout)
        self.transport.api = transient
        self.assertEqual(self.download()["outcome"], "saved-retained")
        self.transport.api = lambda *a, **k: (_ for _ in ()).throw(error.HTTPError("x", 403, "forbidden", {}, None))
        count = len(self.fake.sent)
        with self.assertRaises(error.HTTPError): self.transport.exchange(wire.pending_command())
        self.assertEqual(len(self.fake.sent), count)

    def test_invalid_alias_and_token_fail_closed(self):
        api = self.transport.api
        for page in ({"packets": {}}, {"packets": [], "continuationToken": False},
                     {"packets": [], "continuationToken": 1}):
            self.transport.api = lambda path, body=None, timeout=4: page if "/packets?" in path else api(path, body, timeout)
            with self.subTest(page=page), self.assertRaises(ValueError):
                self.transport.exchange(wire.pending_command())

    def test_nonzero_status_requires_exact_session(self):
        self.download(); old = self.fake.active
        self.fake.active = export.Transfer(old.boot_id, old.transfer_id + 1, old.request_id, old.digest, old.record_count)
        with self.assertRaisesRegex(client.BoardRejected, "STALE"):
            self.transport.exchange(wire.read_command(old, 0))
        self.assertEqual(len(self.fake.records), 2)

    def test_already_archived_read_not_reused_at_same_timestamp(self):
        self.download(); transfer = self.fake.active
        self.fake.no_reply = True
        with self.assertRaises(client.OutcomeUnknown):
            self.transport.exchange(wire.read_command(transfer, 0))
        self.assertEqual(len(self.fake.records), 2)

    def test_server_clock_backwards_fails_closed(self):
        self.download(); self.fake.time = "2026-10-14T10:00:00Z"
        with self.assertRaisesRegex(ValueError, "precedes"):
            self.transport.exchange(wire.pending_command())

    def test_file_changed_during_baseline_no_confirm_sent(self):
        self.download(); count = len(self.fake.sent)
        baseline = self.transport._baseline
        def change(deadline):
            result = baseline(deadline)
            self.path.write_bytes(b"{}")
            return result
        with patch.object(self.transport, "_baseline", side_effect=change):
            with self.assertRaises(ValueError): client.control_export(self.transport, self.path, confirm_clear=True)
        self.assertEqual(len(self.fake.sent), count)

    def test_confirm_exchange_requires_saved_receipt(self):
        self.download()
        raw = b"\x34" + wire.manifest_bytes(self.fake.active) + b"\xA5"
        sent = len(self.fake.sent)
        with self.assertRaises(ValueError): self.transport.exchange(raw)
        self.assertEqual(len(self.fake.sent), sent)

    def test_cli_misuse_stops_without_network(self):
        cases = [["--confirm-clear"], ["--send"], ["--output", "x"], ["--send", "--confirm-export", "x"],
                 ["--send", "--cancel-export", "x", "--confirm-clear"],
                 ["--send", "--output", "x", "--resume-pending", "--request-id", "1"]]
        for argv in cases:
            with self.subTest(argv=argv), redirect_stderr(io.StringIO()), self.assertRaises(SystemExit): client.arguments(argv)

    def test_cli_default_dryrun(self):
        with patch.object(client, "JsonHttp", return_value=self.fake), redirect_stdout(io.StringIO()): client.main([])
        self.assertEqual(self.fake.sent, [])

    def test_transport_bounds(self):
        for kwargs in (dict(timeout=0), dict(timeout=float("inf")), dict(retries=-1), dict(retries=4), dict(instance="x/y")):
            with self.assertRaises(ValueError): client.YamcsTransport(self.fake, **kwargs)
        for body in (b"\x3F\xAA", b"\x30\0\0", b"\x33", b"\x34" + b"\0" * 23):
            with self.assertRaises(ValueError): client.command_details(body)


class HttpTests(unittest.TestCase):
    class Opener:
        def __init__(self, raw): self.raw, self.requests = raw, []
        def open(self, req, timeout): self.requests.append((req, timeout)); return io.BytesIO(self.raw)

    def test_serialization_and_size_bound(self):
        opener = self.Opener(b'{"ok":true}')
        api = client.JsonHttp("http://localhost:8090", opener)
        self.assertEqual(api("/api/test", {"args": {"BootId": "18446744073709551615"}, "dryRun": True}), {"ok": True})
        req, timeout = opener.requests[0]
        self.assertEqual(req.get_method(), "POST"); self.assertEqual(timeout, 4)
        self.assertEqual(json.loads(req.data)["args"]["BootId"], "18446744073709551615")
        for raw in (b'[]', b'{"x":1,"x":2}', b'x' * (client.MAX_JSON + 1)):
            with self.assertRaises(ValueError): client.JsonHttp("http://localhost", self.Opener(raw))("/api/test")

    def test_url_and_redirect_guards(self):
        for url in ("file:///x", "http://u:p@localhost", "http://localhost/x", "http://localhost?x=1", "http://localhost#x"):
            with self.assertRaises(ValueError): client.JsonHttp(url)
        self.assertIsNone(client.NoRedirect().redirect_request(None, None, 307, "", {}, "http://other"))

    def test_trickling_http_body_has_deadline(self):
        clock = Clock()
        class SlowBody(io.BytesIO):
            def read1(self, size):
                clock.sleep(2)
                return super().read1(1)
        class SlowOpener:
            def open(self, req, timeout): return SlowBody(b'{"ok":true}')
        with self.assertRaises(TimeoutError):
            client.JsonHttp("http://localhost", SlowOpener(), clock=clock)("/api/test", timeout=4)


if __name__ == "__main__": unittest.main()
