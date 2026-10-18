"""TC/archive read checks against the ESP32; board reset requires --reset."""
import argparse
import ipaddress
import re
import socket
import struct
import time

import serial


def make_tc(payload):
    if not 1 <= len(payload) <= 48:
        raise ValueError("Wire TC payload must contain 1..48 bytes")
    return struct.pack("!HHH", 0x1060, 0xC000, len(payload) - 1) + payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="COM7", help="ESP32 serial port (default: COM7)")
    parser.add_argument("--baud", type=int, default=115200)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("--ip", type=ipaddress.IPv4Address,
                        help="Running board IP; test without requesting a reset")
    target.add_argument("--reset", action="store_true",
                        help="Restart the board and discover IP from boot log; RAM archive is cleared")
    args = parser.parse_args()

    with serial.Serial() as port, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        port.port = args.port
        port.baudrate = args.baud
        port.timeout = 0.2
        port.dtr = False
        port.rts = False
        port.open()
        board_ip = str(args.ip) if args.ip is not None else None

        def wait_for(expected, timeout=6):
            nonlocal board_ip
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                line = port.readline().decode("utf-8", errors="replace").strip()
                match = re.search(r"ESP32 IP: (\d+\.\d+\.\d+\.\d+)", line)
                if match and args.reset:
                    board_ip = match.group(1)
                if any(marker in line for marker in ("Guru Meditation", "Backtrace:", "abort()")):
                    raise RuntimeError("Unexpected board failure: " + line)
                if line.startswith(("MAIN: Wi-Fi", "SYSTEM MODE:", "YAMCS:", "TC PROC:", "ARCHIVE:", "ARCHIVE ERROR:", "TC_RX:")):
                    print(line, flush=True)
                if expected in line:
                    return
            raise RuntimeError("Timed out waiting for: " + expected)

        if args.reset:
            port.rts = True
            time.sleep(0.1)
            port.rts = False
            wait_for("TM GEN: -- Running NOMINAL Mission Cycle --", timeout=35)
            if board_ip is None:
                raise RuntimeError("Board IP was not confirmed from the boot log")
            time.sleep(0.5)
        else:
            port.reset_input_buffer()

        def send(payload):
            udp.sendto(make_tc(payload), (board_ip, 10025))

        print("CHECK: valid 3-byte archive GET_INDEX", flush=True)
        send(bytes([0x30, 0, 0]))
        wait_for("ARCHIVE: Read success at index 0.")

        print("CHECK: missing index byte is rejected as 2-byte command data", flush=True)
        send(bytes([0x30, 0]))
        wait_for("ARCHIVE ERROR: GET_INDEX expects 3 bytes, got 2")

        print("CHECK: empty command is rejected at wire ingress, before the internal queue", flush=True)
        udp.sendto(struct.pack("!HHH", 0x1060, 0xC000, 0), (board_ip, 10025))
        wait_for("TC_RX: invalid wire TC header/length: 6")

        for index in (32768, 65535):
            print(f"CHECK: absent high archive index {index} is rejected", flush=True)
            send(bytes([0x30, index >> 8, index & 0xFF]))
            wait_for("ARCHIVE ERROR: Read failed (Status: 4)")

        print("CHECK: record is still readable after rejected commands", flush=True)
        send(bytes([0x30, 0, 0]))
        wait_for("ARCHIVE: Read success at index 0.")
        print("PASS: live archive read, malformed commands and high indices rejected, record retained.", flush=True)


if __name__ == "__main__":
    main()
