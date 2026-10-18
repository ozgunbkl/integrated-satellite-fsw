"""Opt-in loader READS only: compare NVS bytes without storing raw credentials.

Entering the loader destroys current RAM. Preserve it first. This does NOT
upload firmware, erase/write flash, start the app, or prove functional recovery.
The chip is left in the flasher stub; a separate controlled boot is required.
"""
import argparse
from contextlib import redirect_stderr, redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import sys

from resource_monitor_check import write_snapshot

NVS_OFFSET, NVS_SIZE = 0x9000, 0x6000
TABLE_OFFSET, TABLE_SIZE = 0x8000, 3072
LAYOUT = (
    (1, 2, 0x9000, 0x6000, b"nvs"),
    (1, 0, 0xF000, 0x2000, b"otadata"),
    (1, 1, 0x11000, 0x1000, b"phy_init"),
    (0, 0x10, 0x20000, 1740800, b"ota_0"),
    (0, 0x11, 0x1D0000, 1740800, b"ota_1"),
)


class ProbeError(ValueError):
    pass


def digest(raw):
    return hashlib.sha256(raw).hexdigest().upper()


def validate_table(raw):
    if len(raw) != TABLE_SIZE:
        raise ProbeError("Wrong project partition table length")
    for i, (kind, subtype, offset, size, label) in enumerate(LAYOUT):
        expected = struct.pack("<HBBII16sI", 0x50AA, kind, subtype, offset, size, label, 0)
        if raw[i * 32:(i + 1) * 32] != expected:
            raise ProbeError("Unexpected partition layout/flags; refusing loader access")
    boundary = 32 * len(LAYOUT)
    md5 = b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(raw[:boundary]).digest()
    if raw[boundary:boundary + 32] != md5 or raw[boundary + 32:] != b"\xff" * (TABLE_SIZE - boundary - 32):
        raise ProbeError("Invalid partition MD5/padding")


def sample(read_flash, expected_table):
    validate_table(expected_table)
    resident = read_flash(TABLE_OFFSET, 4096)
    if resident != expected_table + b"\xff" * (4096 - TABLE_SIZE):
        raise ProbeError("Resident partition table differs; no NVS read or upload allowed")
    first = read_flash(NVS_OFFSET, NVS_SIZE)
    second = read_flash(NVS_OFFSET, NVS_SIZE)
    if not isinstance(first, bytes) or len(first) != NVS_SIZE or first != second:
        raise ProbeError("NVS reads are short or unstable while app is stopped")
    return dict(table_sha256=digest(expected_table), nvs_sha256=digest(first),
                nvs_all_erased=first == b"\xff" * NVS_SIZE,
                page_sha256=[digest(first[i:i + 4096]) for i in range(0, NVS_SIZE, 4096)],
                read_verified_twice=True)


def compare(current, previous):
    if (previous.get("result") != "PASS" or previous.get("board_mac") != current["board_mac"] or
            previous.get("table_sha256") != current["table_sha256"] or
            previous.get("read_verified_twice") is not True or
            not isinstance(previous.get("nvs_sha256"), str) or
            not re.fullmatch(r"[0-9A-F]{64}", previous["nvs_sha256"])):
        raise ProbeError("Comparison proof is not a compatible successful probe")
    if current["nvs_sha256"] != previous["nvs_sha256"]:
        raise ProbeError("NVS bytes CHANGED between loader probes; do not claim retained history")


def read_target(port, baud, mac, table, esptool_dir):
    # Local supported tool only; no install/download or persistent configuration.
    # PlatformIO bundles dependencies beside the package, not in its Python
    # environment. Use the same local _contrib directory as esptool.py.
    sys.path[:0] = [str(esptool_dir), str(esptool_dir / "_contrib")]
    import esptool
    if esptool.__version__ != "4.9.0":
        raise ProbeError("This probe is pinned to local esptool 4.9.0")
    from esptool.targets.esp32 import ESP32ROM
    with ESP32ROM(port, 115200, trace_enabled=False) as rom:
        rom.connect(attempts=3)
        if bytes(rom.read_mac()).hex(":") != mac or rom.secure_download_mode:
            raise ProbeError("Wrong board identity or unsupported secure download mode")
        stub = rom.run_stub()
        if baud != 115200:
            stub.change_baud(baud)
        stub.flash_spi_attach(0)
        stub.flash_set_parameters(4 * 1024 * 1024)
        return sample(stub.read_flash, table)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7")
    parser.add_argument("--baud", type=int, choices=(115200, 460800), default=460800)
    parser.add_argument("--expected-mac", required=True)
    parser.add_argument("--partition-table", type=Path, required=True)
    parser.add_argument("--esptool-dir", type=Path, default=Path("C:/.platformio/packages/tool-esptoolpy"))
    parser.add_argument("--allow-ram-reset", action="store_true")
    parser.add_argument("--compare", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    if not args.allow_ram_reset or not re.fullmatch(r"COM[1-9][0-9]*", args.port):
        parser.error("Requires --allow-ram-reset after fresh archive preservation and an explicit COM port")
    mac = args.expected_mac.lower()
    if not re.fullmatch(r"(?:[0-9a-f]{2}:){5}[0-9a-f]{2}", mac):
        parser.error("An explicit expected board MAC is required")
    if args.output.exists() or args.output.is_symlink() or not args.output.parent.is_dir():
        parser.error("Output must be a new file in an existing directory")
    try:
        if args.partition_table.stat().st_size != TABLE_SIZE:
            raise ProbeError("Wrong project partition table length")
        table = args.partition_table.read_bytes()
        validate_table(table)
        previous = None
        if args.compare:
            if args.compare.stat().st_size > 8192:
                raise ProbeError("Comparison proof is oversized")
            previous = json.loads(args.compare.read_text(encoding="utf-8"))
            if not isinstance(previous, dict):
                raise ProbeError("Comparison proof is not an object")
            # Reject incompatible evidence BEFORE resetting the board.
            compare(dict(board_mac=mac, table_sha256=digest(table), nvs_sha256=previous.get("nvs_sha256")), previous)
    except (OSError, ValueError) as error:
        parser.error(str(error) if isinstance(error, ProbeError) else "Invalid local probe inputs; no port access")
    report = dict(result="RUNNING", board_mac=mac, compared=args.compare is not None,
                  limits="Loader read comparison only; RAM reset, no raw NVS saved; app not started; not a functional recovery test")
    try:
        stream = args.output.open("x", encoding="utf-8")
    except OSError:
        parser.error("Cannot exclusively create output; no port access")
    with stream:
        try:
            write_snapshot(stream, report)
            # esptool exceptions can include raw flash bytes. Never expose them.
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                report.update(read_target(args.port, args.baud, mac, table, args.esptool_dir))
            if previous is not None:
                compare(report, previous)
            report["result"] = "PASS"
            write_snapshot(stream, report)
            print("PASS: two identical NVS reads" + ("; unchanged from the comparison probe" if previous else "") + "; app remains in loader, no functional recovery claim.")
            return 0
        except (Exception, KeyboardInterrupt) as error:
            report.update(result="INTERRUPTED" if isinstance(error, KeyboardInterrupt) else "FAIL",
                          reason=str(error) if isinstance(error, ProbeError) else "Loader/read/output failure; raw details omitted")
            try:
                write_snapshot(stream, report)
            except OSError:
                print("FAIL: could not save final proof; RUNNING is not success.")
            print(report["result"] + ": " + report["reason"])
            return 130 if report["result"] == "INTERRUPTED" else 1


if __name__ == "__main__":
    raise SystemExit(main())
