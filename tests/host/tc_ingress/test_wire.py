"""Offline counterpart of the real TC wire builder. No socket/serial is opened."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from tc_udp_read_check import make_tc


class WireTcChecks(unittest.TestCase):
    def test_golden_archive_get(self):
        self.assertEqual(make_tc(bytes.fromhex("301234")), bytes.fromhex("1060c0000002301234"))

    def test_all_supported_sizes(self):
        for length in range(1, 49):
            payload = bytes(range(length))
            packet = make_tc(payload)
            with self.subTest(length=length):
                self.assertEqual(struct.unpack("!HHH", packet[:6]), (0x1060, 0xC000, length - 1))
                self.assertEqual(packet[6:], payload)
                self.assertLessEqual(len(packet), 54)

    def test_reject_empty_or_oversized(self):
        for length in (0, 49, 54, 64, 65535):
            with self.subTest(length=length), self.assertRaises(ValueError):
                make_tc(bytes(length))


if __name__ == "__main__":
    unittest.main()
