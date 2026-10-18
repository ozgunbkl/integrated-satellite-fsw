"""Offline checks for the command guard; no HTTP requests or serial access."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from yamcs_archive_command_check import output_count, validate_command_binary


class YamcsCommandGuardChecks(unittest.TestCase):
    def test_pre_link_golden_bytes(self):
        raw = bytes.fromhex("1060c0000000301234")
        self.assertEqual(validate_command_binary(raw, 0x1234, postprocessed=False), 0)

    def test_postprocessed_packet(self):
        raw = bytes.fromhex("1060c0050002300000")
        self.assertEqual(validate_command_binary(raw, 0, postprocessed=True), 5)

    def test_zero_and_maximum_index(self):
        for index in (0, 65535):
            raw = bytes.fromhex("1060ffff000230") + index.to_bytes(2, "big")
            self.assertEqual(validate_command_binary(raw, index, postprocessed=True), 16383)

    def test_wrong_target_action_flags_and_length(self):
        raw = bytes.fromhex("1060c0000002301234")
        for position, bit in ((0, 0x10), (0, 0x08), (1, 1), (2, 0x40),
                              (5, 1), (6, 1), (7, 1), (8, 1)):
            corrupt = bytearray(raw)
            corrupt[position] ^= bit
            with self.subTest(position=position), self.assertRaises(ValueError):
                validate_command_binary(corrupt, 0x1234, postprocessed=True)

    def test_truncated_or_extra_packet(self):
        raw = bytes.fromhex("1060c0000002301234")
        for invalid in (raw[:8], raw + b"\0"):
            with self.assertRaises(ValueError):
                validate_command_binary(invalid, 0x1234, postprocessed=True)

    def test_integer_and_protobuf_uint64_counter(self):
        self.assertEqual(output_count({}), 0)
        self.assertEqual(output_count({"dataOutCount": 2}), 2)
        self.assertEqual(output_count({"dataOutCount": "4"}), 4)
        self.assertEqual(output_count({"dataOutCount": str(2**64 - 1)}), 2**64 - 1)

    def test_invalid_counter(self):
        for value in (-1, "-1", "invalid", None):
            with self.subTest(value=value), self.assertRaises(ValueError):
                output_count({"dataOutCount": value})


if __name__ == "__main__":
    unittest.main()
