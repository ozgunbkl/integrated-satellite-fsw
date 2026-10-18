"""Independent Python receiver checks; no board, serial access or network writes."""
import binascii
from pathlib import Path
import runpy
import struct
import unittest

receiver = runpy.run_path(str(Path(__file__).resolve().parents[3] /
                            "tools" / "archive_downlink_check.py"))
decode = receiver["decode_archive_packet"]


def packet(payload=b"123456789", index=0x1234, record_id=0xABCD):
    body = struct.pack("!BHHHH", 1, index, record_id, len(payload),
                       binascii.crc_hqx(payload, 0xFFFF)) + payload
    return struct.pack("!HHH", 0x0060, 0xC007, len(body) - 1) + body


class ArchiveDecoderChecks(unittest.TestCase):
    def test_golden_crc_and_metadata(self):
        result = decode(packet())
        self.assertEqual(result, {"index": 0x1234, "record_id": 0xABCD, "length": 9,
                                  "crc": 0x29B1, "payload": b"123456789", "sequence": 7})

    def test_empty_and_maximum(self):
        self.assertEqual(decode(packet(b""))["crc"], 0xFFFF)
        self.assertEqual(decode(packet(bytes(range(128))))["length"], 128)
        with self.assertRaises(ValueError):
            decode(packet(bytes(range(129))))

    def test_truncated_and_extra_bytes(self):
        valid = packet()
        for length in range(len(valid)):
            with self.subTest(length=length), self.assertRaises(ValueError):
                decode(valid[:length])
        with self.assertRaises(ValueError):
            decode(valid + b"\0")

    def test_invalid_headers(self):
        # Byte/bit positions: TM type, APID, segmentation, CCSDS length,
        # archive version, archive payload length, CRC, and payload.
        for position, bit in ((0, 0x10), (1, 1), (2, 0x40), (5, 1),
                              (6, 1), (12, 1), (14, 1), (15, 1)):
            corrupt = bytearray(packet())
            corrupt[position] ^= bit
            with self.subTest(position=position), self.assertRaises(ValueError):
                decode(corrupt)

    def test_command_bytes(self):
        self.assertEqual(receiver["make_get_command"](0x1234),
                         bytes.fromhex("1060c0000002301234"))


if __name__ == "__main__":
    unittest.main()
