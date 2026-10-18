import binascii
from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import archive_transfer as export
import archive_transfer_wire as wire
import recovery_archive_guard as guard


class RetainedBoard:
    def __init__(self, count=30, pending=False):
        self.records = [dict(index=i, record_id=i + 100, length=1,
                             crc=binascii.crc_hqx(bytes([i]), 0xFFFF), payload=bytes([i]).hex()) for i in range(count)]
        self.active, self.sent, self.corrupt = None, [], False
        if pending: self.begin(42)

    def begin(self, request):
        self.active = export.Transfer(0xFEDCBA9876543210, 1, request, export.digest_records(self.records), len(self.records))

    def exchange(self, body):
        self.sent.append(body[0])
        if body[0] == wire.BEGIN:
            if not self.records: return dict(status=5)
            self.begin(struct.unpack("!I", body[1:])[0])
        if body[0] == wire.PENDING and not self.active: return dict(status=4)
        result = dict(status=0, manifest=self.active.as_dict())
        if body[0] == wire.READ:
            index = struct.unpack("!H", body[-2:])[0]
            result["record"] = dict(self.records[index])
            if self.corrupt: result["record"]["payload"] = "ff"
        return result


class GuardTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.directory = Path(temp.name)
        self.paths = [self.directory / name for name in ("first.json", "retained.json", "receipt.json")]

    def test_live_full_capacity_pair_and_readback(self):
        board = RetainedBoard()
        proof = guard.preserve(board, *self.paths)
        self.assertTrue(proof["whole_capacity_verified"])
        self.assertEqual(proof["manifest"]["record_count"], 30)
        self.assertEqual(self.paths[0].read_bytes(), self.paths[1].read_bytes())
        self.assertEqual(json.loads(self.paths[2].read_text())["result"], "PASS")
        self.assertEqual(board.sent.count(wire.READ), 60)
        self.assertEqual(board.sent.count(wire.BEGIN), 1)
        self.assertTrue(set(board.sent) <= {wire.BEGIN, wire.PENDING, wire.READ})

    def test_full_existing_pending_resumed_without_new_begin(self):
        board = RetainedBoard(pending=True)
        guard.preserve(board, *self.paths)
        self.assertNotIn(wire.BEGIN, board.sent)
        self.assertEqual(board.active.request_id, 42)

    def test_partial_pending_is_not_proof_for_whole_ram(self):
        board = RetainedBoard(count=2, pending=True)
        with self.assertRaises(ValueError): guard.preserve(board, *self.paths)
        self.assertEqual(board.sent, [wire.PENDING])
        self.assertFalse(any(path.exists() for path in self.paths))

    def test_empty_and_partial_archives_never_authorize_reset(self):
        for count in (0, 1, 29):
            with self.subTest(count=count), tempfile.TemporaryDirectory() as directory:
                paths = [Path(directory) / str(i) for i in range(3)]
                board = RetainedBoard(count=count)
                with self.assertRaises(ValueError): guard.preserve(board, *paths)
                self.assertFalse(paths[2].exists())
                self.assertNotIn(wire.CONFIRM, board.sent)

    def test_corrupt_record_never_yields_receipt(self):
        board = RetainedBoard()
        board.corrupt = True
        with self.assertRaises(ValueError): guard.preserve(board, *self.paths)
        self.assertFalse(self.paths[2].exists())

    def test_output_overwrite_duplicate_or_missing_parent_rejected_before_io(self):
        existing = self.directory / "existing"
        existing.touch()
        for paths in ((existing, self.paths[1], self.paths[2]), (self.paths[0], self.paths[0], self.paths[2]),
                      (self.directory / "missing" / "first", self.paths[1], self.paths[2])):
            board = RetainedBoard()
            with self.assertRaises(ValueError): guard.preserve(board, *paths)
            self.assertEqual(board.sent, [])

    def test_verifier_rejects_tampering_or_same_file(self):
        guard.preserve(RetainedBoard(), *self.paths)
        with self.assertRaises(ValueError): guard.verify_pair(self.paths[0], self.paths[0])
        document = json.loads(self.paths[1].read_text())
        document["records"][0]["payload"] = "fe"
        self.paths[1].write_text(json.dumps(document))
        with self.assertRaises(ValueError): guard.verify_pair(self.paths[0], self.paths[1])

    def test_non_reading_transport_operations_denied(self):
        board = RetainedBoard()
        retained = guard.RetainOnly(board)
        for body in (b"\x34", b"\x35", b"\x3f", b"", None):
            with self.assertRaises(ValueError): retained.exchange(body)
        with self.assertRaises(ValueError): retained.exchange(b"\x32", confirm_clear=True)
        self.assertEqual(board.sent, [])

    def test_second_copy_failure_does_not_grant_receipt_or_clear(self):
        board = RetainedBoard()
        original = board.exchange
        def failing(body):
            if body[0] == wire.PENDING and board.sent.count(wire.READ) == 30:
                raise TimeoutError("Second live read unavailable")
            return original(body)
        board.exchange = failing
        with self.assertRaises(TimeoutError): guard.preserve(board, *self.paths)
        self.assertTrue(self.paths[0].exists())
        self.assertFalse(self.paths[2].exists())
        self.assertTrue(set(board.sent) <= {wire.BEGIN, wire.PENDING, wire.READ})

    def test_cli_refuses_missing_approval_without_network(self):
        with patch.object(guard, "JsonHttp") as api, redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            guard.main(["--first", "first", "--second", "second", "--receipt", "receipt"])
        api.assert_not_called()

    def test_cli_error_details_are_not_exposed(self):
        with patch.object(guard, "JsonHttp", side_effect=RuntimeError("secret")), redirect_stdout(io.StringIO()) as output:
            self.assertEqual(guard.main(["--send-retained-reads", "--first", "first", "--second", "second", "--receipt", "receipt"]), 1)
        self.assertNotIn("secret", output.getvalue())


if __name__ == "__main__":
    unittest.main()
