"""Independent Python wire/CRC checks and explicit saved-file confirmation guard."""
import binascii
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import archive_transfer as export
import archive_transfer_wire as wire


def tm(op=wire.READ, status=0, payload=b"123456789", index=1, version=1, record_id=0xABCD, crc=None):
    manifest = (0x0123456789ABCDEF, 1, 0x12345678, 0x417EE9BC, 2)
    if crc is None:
        crc = binascii.crc_hqx(payload, 0xFFFF)
    body = wire.REPLY.pack(version, op, status, *manifest, index, record_id, len(payload), crc) + payload
    return struct.pack("!3H", 97, 0xC123, len(body) - 1) + body


class TransferWireTests(unittest.TestCase):
    def setUp(self):
        self.transfer = export.Transfer(0x0123456789ABCDEF, 1, 0x12345678, 0x417EE9BC, 2)
        self.records = [{"index": 0, "record_id": 0x1234, "length": 0, "crc": 0xFFFF, "payload": ""},
                        {"index": 1, "record_id": 0xABCD, "length": 9, "crc": 0x29B1, "payload": b"123456789".hex()}]

    def test_c_manifest_golden(self):
        body = bytes.fromhex("0131000123456789abcdef0000000112345678417ee9bc0002ffff000000000000")
        packet = struct.pack("!3H", 97, 0xC000, len(body) - 1) + body
        result = wire.decode_reply(packet)
        self.assertEqual(result["manifest"], self.transfer.as_dict())
        self.assertEqual((result["operation"], result["status"], result["index"], result["record"]), (0x31, 0, 0xFFFF, None))

    def test_record_zero_nine_max_payload(self):
        for payload in (b"", b"123456789", bytes(range(128))):
            result = wire.decode_reply(tm(payload=payload))
            self.assertEqual(result["manifest"], self.transfer.as_dict())
            self.assertEqual(bytes.fromhex(result["record"]["payload"]), payload)
            self.assertEqual(result["record"]["crc"], binascii.crc_hqx(payload, 0xFFFF))

    def test_corruption_and_size(self):
        packet = tm()
        for changed in (packet[:-1], packet + b"x", packet[:-1] + bytes([packet[-1] ^ 1]), tm(crc=0), tm(index=2)):
            with self.subTest(packet=changed.hex()), self.assertRaises(ValueError):
                wire.decode_reply(changed)

    def test_primary_header_rejection(self):
        for offset, value in ((0, 0x20), (0, 0x10), (0, 0x08), (1, 96), (2, 0), (2, 0x40), (2, 0x80), (4, 0xFF), (5, 0xFF)):
            packet = bytearray(tm()); packet[offset] = value
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                wire.decode_reply(bytes(packet))

    def test_version_opcode_status_rejection(self):
        for packet in (tm(version=2), tm(op=0x30), tm(op=0x36), tm(status=11)):
            with self.assertRaises(ValueError):
                wire.decode_reply(packet)

    def test_error_has_no_record_or_success_claim(self):
        packet = tm(status=8, payload=b"", index=1, record_id=0, crc=0)
        result = wire.decode_reply(packet)
        self.assertEqual(result["status"], 8)
        self.assertIsNone(result["record"])
        for packet in (tm(status=8), tm(op=wire.PENDING, payload=b"", index=0, record_id=0, crc=0)):
            with self.assertRaises(ValueError):
                wire.decode_reply(packet)

    def test_command_golden_and_lengths(self):
        self.assertEqual(wire.begin_command(0x12345678), bytes.fromhex("3112345678"))
        self.assertEqual(wire.pending_command(), b"\x32")
        manifest = bytes.fromhex("0123456789abcdef0000000112345678417ee9bc0002")
        self.assertEqual(wire.read_command(self.transfer, 1), b"\x33" + manifest + b"\x00\x01")
        self.assertEqual(wire.cancel_command(self.transfer), b"\x35" + manifest)
        for body in (wire.begin_command(0), wire.pending_command(), wire.read_command(self.transfer, 1), wire.cancel_command(self.transfer)):
            packet = wire.tc_packet(body, 0x3FFF)
            self.assertEqual(struct.unpack_from("!3H", packet), (0x1060, 0xFFFF, len(body) - 1))
            self.assertEqual(packet[6:], body)

    def test_command_range_guards(self):
        for value in (-1, 1 << 32, True):
            with self.assertRaises(ValueError): wire.begin_command(value)
        for value in (-1, 2, 65536, True):
            with self.assertRaises(ValueError): wire.read_command(self.transfer, value)
        for data in (b"", b"x" * 49, bytearray(b"x")):
            with self.assertRaises(ValueError): wire.tc_packet(data)
        for value in (-1, 16384, True):
            with self.assertRaises(ValueError): wire.tc_packet(b"x", value)

    def test_manifest_max_unsigned64(self):
        transfer = export.Transfer((1 << 64) - 1, (1 << 32) - 1, (1 << 32) - 1, (1 << 32) - 1, 30)
        self.assertEqual(wire.read_command(transfer, 29), b"\x33" + b"\xFF" * 20 + b"\x00\x1E\x00\x1D")

    def test_confirm_requires_explicit_synced_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "records.json"
            saved = export.save_verified_export(path, self.transfer.as_dict(), self.records)
            for value in (False, None, 1):
                with self.assertRaises(ValueError): wire.confirm_command(saved, confirm_clear=value)
            with self.assertRaises(ValueError): wire.confirm_command(self.transfer, confirm_clear=True)
            body = wire.confirm_command(saved, confirm_clear=True)
            self.assertEqual(body, b"\x34" + wire.manifest_bytes(self.transfer) + b"\xA5")
            self.assertEqual(len(body), 24)

    def test_confirm_rechecks_export_before_encoding(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "records.json"
            saved = export.save_verified_export(path, self.transfer.as_dict(), self.records)
            with path.open("wb") as stream: stream.write(b"{}")
            with self.assertRaises(ValueError): wire.confirm_command(saved, confirm_clear=True)


if __name__ == "__main__": unittest.main()
