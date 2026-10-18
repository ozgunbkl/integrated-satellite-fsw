import binascii
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import archive_transfer as transfer


def record(index, record_id, payload):
    return {"index": index, "record_id": record_id, "length": len(payload),
            "crc": binascii.crc_hqx(payload, 0xFFFF), "payload": payload.hex()}


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.records = [record(0, 0x1234, b""), record(1, 0xABCD, b"123456789")]
        self.manifest = {"boot_id": 0x0123456789ABCDEF, "transfer_id": 1, "request_id": 0,
                         "digest": 0x417EE9BC, "record_count": 2}

    def test_c_golden_and_out_of_order(self):
        self.assertEqual(transfer.digest_records(self.records), 0x417EE9BC)
        self.assertEqual(transfer.digest_records(self.records[::-1]), 0x417EE9BC)
        doc = transfer.verified_document(self.manifest, self.records[::-1])
        self.assertEqual(doc["records"], self.records)

    def test_missing_duplicate_and_extra(self):
        for records in ([], self.records[:1], [self.records[0]] * 2, self.records + [record(2, 1, b"a")]):
            with self.subTest(records=records), self.assertRaises(ValueError):
                transfer.verified_document(self.manifest, records)

    def test_metadata_integrity(self):
        for field in ("record_id", "crc", "length", "index"):
            changed = copy.deepcopy(self.records)
            changed[0][field] ^= 1
            with self.subTest(field=field), self.assertRaises(ValueError):
                transfer.verified_document(self.manifest, changed)

    def test_payload_integrity_and_encoding(self):
        for value in ("00" * 9, "zz" * 9, "3132", " 1" * 9, None):
            changed = copy.deepcopy(self.records)
            changed[1]["payload"] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                transfer.verified_document(self.manifest, changed)

    def test_bad_manifests(self):
        for field, value in (("boot_id", 0), ("boot_id", 1 << 64), ("transfer_id", 0), ("request_id", -1),
                             ("record_count", 0), ("record_count", 31), ("digest", -1), ("digest", 1 << 32),
                             ("digest", True), ("record_count", 1.0), ("boot_id", "123")):
            changed = dict(self.manifest, **{field: value})
            with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                transfer.verified_document(changed, self.records)

    def test_maximum_export(self):
        records = [record(i, i, bytes([i]) * 128) for i in range(30)]
        manifest = dict(self.manifest, record_count=30, digest=transfer.digest_records(records))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "records.json"
            receipt = transfer.save_verified_export(path, manifest, records)
            self.assertEqual(receipt.transfer.record_count, 30)
            self.assertLess(path.stat().st_size, transfer.MAX_EXPORT_BYTES)

    def test_save_sync_reread_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "records.json"
            with patch.object(transfer.os, "fsync", wraps=transfer.os.fsync) as sync:
                receipt = transfer.save_verified_export(path, self.manifest, self.records)
                sync.assert_called_once()
            self.assertIsInstance(receipt, transfer.SavedTransfer)
            self.assertEqual(receipt.transfer, transfer.Transfer.from_dict(self.manifest))
            loaded = transfer.load_verified_export(path)
            self.assertNotIsInstance(loaded, transfer.SavedTransfer)  # Loading alone is not a synced-save receipt.
            self.assertEqual(loaded, receipt.transfer)
            before = path.read_bytes()
            with self.assertRaises(FileExistsError):
                transfer.save_verified_export(path, self.manifest, self.records)
            self.assertEqual(path.read_bytes(), before)
            with self.assertRaises(Exception):
                receipt.digest = 0  # Frozen identity, no mutable post-verification manifest.

    def test_verification_failure_creates_no_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "records.json"
            with self.assertRaises(ValueError):
                transfer.save_verified_export(path, dict(self.manifest, digest=0), self.records)
            self.assertFalse(path.exists())

    def test_sync_failure_returns_no_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(transfer.os, "fsync", side_effect=OSError("disk sync failure")):
                with self.assertRaises(OSError):
                    transfer.save_verified_export(Path(directory) / "records.json", self.manifest, self.records)

    def test_readback_failure_returns_no_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(transfer, "load_verified_export", side_effect=OSError("read failure")):
                with self.assertRaises(OSError):
                    transfer.save_verified_export(Path(directory) / "records.json", self.manifest, self.records)

    def test_truncated_wrong_format_and_duplicate_json(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "records.json"
            # Test fixtures use normal serialization; no production files are overwritten.
            for text in ('{', '{}', '{"format":"a","format":"b"}', '[]',
                         json.dumps({"format": "wrong", "manifest": self.manifest, "records": self.records})):
                with path.open("w", encoding="utf-8") as stream:
                    stream.write(text)
                with self.subTest(text=text), self.assertRaises(ValueError):
                    transfer.load_verified_export(path)

    def test_short_write_no_receipt(self):
        from unittest.mock import mock_open
        stream = mock_open()
        stream.return_value.write.return_value = 0
        with patch.object(transfer.Path, "open", stream), patch.object(transfer.os, "fsync") as sync:
            with self.assertRaises(OSError):
                transfer.save_verified_export("unused.json", self.manifest, self.records)
            sync.assert_not_called()

    def test_resync_existing_export_preserves_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "records.json"
            saved = transfer.save_verified_export(path, self.manifest, self.records)
            raw = path.read_bytes()
            with patch.object(transfer.os, "fsync", wraps=transfer.os.fsync) as sync:
                resumed = transfer.resync_verified_export(path)
                sync.assert_called_once()
            self.assertEqual(resumed, saved)
            self.assertEqual(path.read_bytes(), raw)

    def test_resync_invalid_file_does_not_yield_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "records.json"
            with path.open("wb") as stream: stream.write(b"{}")
            with patch.object(transfer.os, "fsync") as sync:
                with self.assertRaises(ValueError): transfer.resync_verified_export(path)
                sync.assert_not_called()

    def test_resync_io_failure_no_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "records.json"
            transfer.save_verified_export(path, self.manifest, self.records)
            with patch.object(transfer.os, "fsync", side_effect=OSError("sync failed")):
                with self.assertRaises(OSError): transfer.resync_verified_export(path)


if __name__ == "__main__":
    unittest.main()
