"""Ground drain ordering using the existing fake Yamcs/board, never a card."""
from contextlib import redirect_stderr
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from test_client import FakeYamcs, client, wire
import archive_drain as drain
from archive_transfer import load_verified_export


class DrainTests(unittest.TestCase):
    def setUp(self):
        self.fake = FakeYamcs()
        self.transport = client.YamcsTransport(self.fake, allow_send=True, retries=0,
            timeout=.3, clock=self.fake.clock, sleep=self.fake.clock.sleep)
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)

    def once(self, **kw):
        return drain.drain_once(self.transport, self.directory, 0, confirm_clear=True, **kw)

    def test_no_clear_permission_refuses_before_io(self):
        with self.assertRaises(ValueError): drain.drain_once(self.transport, self.directory, 0)
        self.assertEqual(self.fake.sent, [])

    def test_dual_save_and_checkpoint_precede_exact_confirmation(self):
        checkpoints = []
        def checkpoint(proof):
            first, second = self.directory / proof["primary"], self.directory / proof["backup"]
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(load_verified_export(first), load_verified_export(second))
            self.assertFalse(proof["confirmed"])
            self.assertEqual(len(self.fake.records), 2)
            self.assertNotIn(wire.CONFIRM, [body[0] for body in self.fake.sent])
            checkpoints.append(proof)
        proof = self.once(before_confirm=checkpoint)
        self.assertTrue(proof["confirmed"])
        self.assertEqual(len(checkpoints), 1)
        self.assertEqual(self.fake.records, [])
        self.assertEqual([b[0] for b in self.fake.sent], [wire.PENDING, wire.BEGIN, wire.READ, wire.READ, wire.CONFIRM])

    def test_appended_tail_is_preserved(self):
        self.fake.append_on_read = True
        self.once()
        self.assertEqual(len(self.fake.records), 1)
        self.assertEqual(self.fake.records[0]["payload"], b"new".hex())

    def test_existing_pending_resumed_without_new_begin(self):
        self.transport.exchange(wire.begin_command(123))
        self.once()
        self.assertEqual(sum(body[0] == wire.BEGIN for body in self.fake.sent), 1)

    def test_empty_does_not_create_files_or_confirm(self):
        self.fake.records = []
        self.assertIsNone(self.once())
        self.assertEqual(list(self.directory.iterdir()), [])
        self.assertNotIn(wire.CONFIRM, [body[0] for body in self.fake.sent])

    def test_save_failure_retains_records(self):
        with patch.object(drain, "download", side_effect=OSError("local failure")):
            with self.assertRaises(OSError): self.once()
        self.assertEqual(len(self.fake.records), 2)
        self.assertNotIn(wire.CONFIRM, [body[0] for body in self.fake.sent])

    def test_backup_failure_retains_records(self):
        real_open = Path.open
        def fail_backup(path, *args, **kw):
            if path.name.endswith(".backup.json"): raise OSError("backup failed")
            return real_open(path, *args, **kw)
        with patch.object(Path, "open", fail_backup):
            with self.assertRaises(OSError): self.once()
        self.assertEqual(len(self.fake.records), 2)
        self.assertNotIn(wire.CONFIRM, [body[0] for body in self.fake.sent])

    def test_checkpoint_failure_retains_both_files_and_board_records(self):
        def fail(_): raise OSError("checkpoint failed")
        with self.assertRaises(OSError): self.once(before_confirm=fail)
        self.assertEqual(len(list(self.directory.iterdir())), 2)
        self.assertEqual(len(self.fake.records), 2)
        self.assertNotIn(wire.CONFIRM, [body[0] for body in self.fake.sent])

    def test_ambiguous_confirm_stops_without_retry(self):
        self.fake.drop[wire.CONFIRM] = 1
        with self.assertRaises(client.OutcomeUnknown): self.once()
        self.assertEqual(sum(b[0] == wire.CONFIRM for b in self.fake.sent), 1)
        self.assertEqual(len(list(self.directory.iterdir())), 2)

    def test_existing_path_never_overwritten(self):
        path = self.directory / "batch-000000.backup.json"
        path.write_text("existing")
        with self.assertRaises(ValueError): self.once()
        self.assertEqual(path.read_text(), "existing")
        self.assertEqual(len(self.fake.records), 2)

    def test_cli_missing_permissions_refuses_before_transport(self):
        for args in (["--send"], ["--confirm-clear"], ["--send", "--confirm-clear", "--once"],
                     ["--send", "--confirm-clear", "--duration", "1"]):
            with self.subTest(args=args), patch.object(drain, "YamcsTransport") as transport, redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit): drain.main(args)
                transport.assert_not_called()

    def test_existing_stop_marker_refuses_before_io(self):
        marker = self.directory / "STOP"
        marker.touch()
        args = ["--send", "--confirm-clear", "--duration", "300", "--directory", str(self.directory / "new"), "--stop-file", str(marker)]
        with patch.object(drain, "YamcsTransport") as transport, redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit): drain.main(args)
            transport.assert_not_called()

    def test_stop_marker_finishes_batch_without_truncating_confirmation(self):
        marker = self.directory / "STOP"
        output = self.directory / "new"
        original = drain.drain_once
        def cycle(*args, **kwargs):
            proof = original(*args, **kwargs)
            marker.touch()
            return proof
        args = ["--send", "--confirm-clear", "--duration", "300", "--directory", str(output), "--stop-file", str(marker)]
        with patch.object(drain, "YamcsTransport", return_value=self.transport), patch.object(drain, "drain_once", side_effect=cycle):
            self.assertEqual(drain.main(args), 0)
        self.assertEqual(sum(b[0] == wire.CONFIRM for b in self.fake.sent), 1)
        self.assertEqual(self.fake.records, [])


if __name__ == "__main__":
    unittest.main()
