from contextlib import redirect_stderr, redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
import flash_nvs_probe as probe


def table():
    raw = b"".join(struct.pack("<HBBII16sI", 0x50AA, *entry, 0) for entry in probe.LAYOUT)
    raw += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(raw).digest()
    return raw + b"\xff" * (probe.TABLE_SIZE - len(raw))


class ProbeTests(unittest.TestCase):
    def test_exact_layout(self):
        probe.validate_table(table())

    def test_reject_lengths_flags_offsets_md5_padding(self):
        raw = table()
        for changed in (raw[:-1], raw + b"\xff", *(
                raw[:i] + bytes((raw[i] ^ 1,)) + raw[i + 1:]
                for i in (0, 2, 3, 4, 8, 12, 28, 160, 176, 192, 3071))):
            with self.assertRaises(probe.ProbeError):
                probe.validate_table(changed)

    def test_only_bounded_reads_and_hashes_no_secret(self):
        calls = []
        nvs = b"private-wifi-password".ljust(probe.NVS_SIZE, b"\xff")
        def read(offset, size):
            calls.append((offset, size))
            return table() + b"\xff" * 1024 if offset == probe.TABLE_OFFSET else nvs
        report = probe.sample(read, table())
        self.assertEqual(calls, [(0x8000, 4096), (0x9000, 24576), (0x9000, 24576)])
        self.assertEqual(report["nvs_sha256"], probe.digest(nvs))
        self.assertEqual(len(report["page_sha256"]), 6)
        self.assertNotIn("private", json.dumps(report))
        self.assertFalse(report["nvs_all_erased"])

    def test_resident_mismatch_stops_before_nvs(self):
        calls = []
        def read(offset, size):
            calls.append(offset)
            return b"\xff" * size
        with self.assertRaises(probe.ProbeError):
            probe.sample(read, table())
        self.assertEqual(calls, [0x8000])

    def test_short_or_unstable_nvs_is_not_pass(self):
        for first, second in ((bytes(10), bytes(10)), (bytes(probe.NVS_SIZE), b"\xff" * probe.NVS_SIZE)):
            rows = iter((table() + b"\xff" * 1024, first, second))
            with self.assertRaises(probe.ProbeError):
                probe.sample(lambda *_: next(rows), table())

    def test_all_erased_is_reported_not_treated_as_history(self):
        report = probe.sample(lambda offset, size: table() + b"\xff" * 1024 if offset == 0x8000 else b"\xff" * size, table())
        self.assertTrue(report["nvs_all_erased"])

    def test_compare_exact_digest(self):
        row = dict(result="PASS", board_mac="aa:bb:cc:dd:ee:ff", table_sha256="A" * 64,
                   nvs_sha256="B" * 64, read_verified_twice=True)
        probe.compare(row, row)
        for key, value in (("result", "FAIL"), ("board_mac", "wrong"), ("table_sha256", "C" * 64),
                           ("nvs_sha256", "bad"), ("read_verified_twice", False)):
            with self.assertRaises(probe.ProbeError):
                probe.compare(row, dict(row, **{key: value}))
        with self.assertRaisesRegex(probe.ProbeError, "CHANGED"):
            probe.compare(dict(row, nvs_sha256="C" * 64), row)

    def test_supported_loader_backend_only_configures_and_reads(self):
        calls = []
        class Stub:
            def change_baud(self, baud): calls.append(("baud", baud))
            def flash_spi_attach(self, pins): calls.append(("pins", pins))
            def flash_set_parameters(self, size): calls.append(("size", size))
            def read_flash(self, offset, size):
                calls.append(("read", offset, size))
                return table() + b"\xff" * 1024 if offset == 0x8000 else bytes(size)
        class Rom:
            secure_download_mode = False
            def __init__(self, port, baud, trace_enabled):
                self_args = (port, baud, trace_enabled)
                calls.append(("open", self_args))
            def __enter__(self): return self
            def __exit__(self, *_): calls.append(("close",))
            def connect(self, attempts): calls.append(("connect", attempts))
            def read_mac(self): return bytes.fromhex("aabbccddeeff")
            def run_stub(self): calls.append(("stub",)); return Stub()
        modules = {"esptool": SimpleNamespace(__version__="4.9.0"),
                   "esptool.targets": SimpleNamespace(),
                   "esptool.targets.esp32": SimpleNamespace(ESP32ROM=Rom)}
        with patch.dict(sys.modules, modules), patch.object(sys, "path", sys.path.copy()):
            result = probe.read_target("COM7", 460800, "aa:bb:cc:dd:ee:ff", table(), Path("unused"))
            self.assertTrue(result["read_verified_twice"])
            self.assertEqual(calls[-1], ("close",))
            self.assertEqual(sum(row[0] == "read" for row in calls), 3)
            calls.clear()
            with self.assertRaises(probe.ProbeError):
                probe.read_target("COM7", 460800, "00:00:00:00:00:00", table(), Path("unused"))
            self.assertNotIn(("stub",), calls)
            self.assertEqual(calls[-1], ("close",))

    def arguments(self, directory):
        root = Path(directory)
        (root / "partitions.bin").write_bytes(table())
        return ["--allow-ram-reset", "--expected-mac", "aa:bb:cc:dd:ee:ff", "--partition-table",
                str(root / "partitions.bin"), "--output", str(root / "proof.json")]

    def test_missing_approval_bad_port_or_mac_never_connects(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.arguments(directory)
            for changed in (args[1:], args + ["--port", "COM0"], args + ["--expected-mac", "bad"]):
                with patch.object(probe, "read_target") as target, redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                    probe.main(changed)
                target.assert_not_called()

    def test_existing_output_not_overwritten_or_connected(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.arguments(directory)
            out = Path(directory) / "proof.json"
            out.write_text("original")
            with patch.object(probe, "read_target") as target, redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                probe.main(args)
            target.assert_not_called()
            self.assertEqual(out.read_text(), "original")

    def test_bad_local_table_never_connects(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.arguments(directory)
            (Path(directory) / "partitions.bin").write_bytes(bytes(probe.TABLE_SIZE))
            with patch.object(probe, "read_target") as target, redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                probe.main(args)
            target.assert_not_called()

    def test_cli_writes_hash_only_success(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.arguments(directory)
            result = dict(table_sha256=probe.digest(table()), nvs_sha256="B" * 64, read_verified_twice=True)
            with patch.object(probe, "read_target", return_value=result), redirect_stdout(io.StringIO()) as output:
                self.assertEqual(probe.main(args), 0)
            self.assertEqual(json.loads((Path(directory) / "proof.json").read_text())["result"], "PASS")
            self.assertIn("no functional recovery claim", output.getvalue())

    def test_read_error_sanitized(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.arguments(directory)
            def fail(*_):
                print("private flash contents")
                raise RuntimeError("secret password")
            with patch.object(probe, "read_target", side_effect=fail), redirect_stdout(io.StringIO()) as output:
                self.assertEqual(probe.main(args), 1)
            raw = output.getvalue() + (Path(directory) / "proof.json").read_text()
            self.assertNotIn("private", raw)
            self.assertNotIn("password", raw)
            self.assertEqual(json.loads((Path(directory) / "proof.json").read_text())["result"], "FAIL")

    def test_changed_nvs_saved_as_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.arguments(directory)
            before = dict(result="PASS", board_mac="aa:bb:cc:dd:ee:ff", table_sha256=probe.digest(table()),
                          nvs_sha256="B" * 64, read_verified_twice=True)
            previous = Path(directory) / "before.json"
            previous.write_text(json.dumps(before))
            with patch.object(probe, "read_target", return_value=dict(before, nvs_sha256="C" * 64)), redirect_stdout(io.StringIO()):
                self.assertEqual(probe.main(args + ["--compare", str(previous)]), 1)
            saved = json.loads((Path(directory) / "proof.json").read_text())
            self.assertEqual(saved["result"], "FAIL")
            self.assertIn("CHANGED", saved["reason"])

    def test_incompatible_comparison_rejected_before_reset(self):
        with tempfile.TemporaryDirectory() as directory:
            args = self.arguments(directory)
            previous = Path(directory) / "before.json"
            previous.write_text('{"result":"RUNNING"}')
            with patch.object(probe, "read_target") as target, redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                probe.main(args + ["--compare", str(previous)])
            target.assert_not_called()


if __name__ == "__main__":
    unittest.main()
